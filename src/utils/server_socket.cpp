#include "utils/server_socket.h"

#include "utils/logging.h"
#include "utils/o_print_stream.h"
#include "utils/platform.h"
#include "utils/platform_ethernet.h"

namespace alpaca {
namespace {

constexpr MillisT kDisconnectMaxMillis = 5000;
constexpr uint8_t kWriteBufferSize = 255;

class TcpServerConnection : public WriteBufferedWrappedClientConnection {
 public:
  explicit TcpServerConnection(uint8_t *write_buffer,
                               uint8_t write_buffer_limit,
                               EthernetClient &client,
                               ServerSocket::DisconnectData &disconnect_data)
      : WriteBufferedWrappedClientConnection(write_buffer, write_buffer_limit),
        client_(client),
        disconnect_data_(disconnect_data) {
    TAS_VLOG(5) << TAS_FLASHSTR("TcpServerConnection@") << this
                << TAS_FLASHSTR(" ctor");
    disconnect_data_.Reset();
  }
  ~TcpServerConnection() {  // NOLINT
    TAS_VLOG(5) << TAS_FLASHSTR("TcpServerConnection@") << this
                << TAS_FLASHSTR(" dtor");
    flush();
  }

  void close() override {
    // The Ethernet3 library's EthernetClient::stop method bakes in a limit of 1
    // second for closing a connection, and spins in a loop waiting until the
    // connection closed, with a delay of 1 millisecond per loop. We avoid this
    // here by NOT delegating to the stop method. Instead we start the close
    // with a DISCONNECT operation (i.e. sending a FIN packet to the peer).
    // PerformIO below will complete the close at some time in the future.
    auto socket_number = sock_num();
    auto status = PlatformEthernet::SocketStatus(socket_number);
    TAS_VLOG(2) << TAS_FLASHSTR("TcpServerConnection::close, sock_num=")
                << socket_number << TAS_FLASHSTR(", status=") << BaseHex
                << status;
    if (status == SnSR::ESTABLISHED || status == SnSR::CLOSE_WAIT) {
      flush();
      status = PlatformEthernet::SocketStatus(socket_number);
      if (status == SnSR::ESTABLISHED || status == SnSR::CLOSE_WAIT) {
        PlatformEthernet::DisconnectSocket(socket_number);
        status = PlatformEthernet::SocketStatus(socket_number);
      }
    }
    // On the assumption that this is only called when there was a working
    // connection at the start of a call to the listener (e.g. OnHalfClosed), we
    // record this as a disconnect initiated by the listener so that we don't
    // later notify the listener of a disconnect
    disconnect_data_.RecordDisconnect();
  }

  bool connected() const override { return client_.connected(); }

  bool peer_half_closed() const override {
    return PlatformEthernet::StatusIsHalfOpen(sock_num());
  }

  uint8_t sock_num() const override { return client_.getSocketNumber(); }

 protected:
  Client &client() const override { return client_; }

 private:
  EthernetClient &client_;
  ServerSocket::DisconnectData &disconnect_data_;
};

MillisT ElapsedMillis(MillisT start_time) { return millis() - start_time; }

}  // namespace

ServerSocket::ServerSocket(uint16_t tcp_port, ServerSocketListener &listener)
    : sock_num_(MAX_SOCK_NUM),
      last_status_(SnSR::CLOSED),
      listener_(listener),
      tcp_port_(tcp_port) {}

bool ServerSocket::PickClosedSocket() {
  if (HasSocket()) {
    return false;
  }

  last_status_ = SnSR::CLOSED;

  int sock_num = PlatformEthernet::FindUnusedSocket();
  if (0 <= sock_num && sock_num < MAX_SOCK_NUM) {
    sock_num_ = sock_num & 0xff;
    if (BeginListening()) {
      last_status_ = PlatformEthernet::SocketStatus(sock_num_);
      return true;
    }
    TAS_VLOG(1) << TAS_FLASHSTR("listen for ") << tcp_port_
                << TAS_FLASHSTR(" failed with socket ") << sock_num_;
    sock_num_ = MAX_SOCK_NUM;
  } else {
    TAS_VLOG(1) << TAS_FLASHSTR("No free socket for ") << tcp_port_;
  }
  return false;
}

bool ServerSocket::HasSocket() const { return sock_num_ < MAX_SOCK_NUM; }

bool ServerSocket::IsConnected() const {
  return HasSocket() &&
         PlatformEthernet::SocketIsInTcpConnectionLifecycle(sock_num_);
}

bool ServerSocket::ReleaseSocket() {
  TAS_DCHECK(HasSocket());
  if (IsConnected()) {
    return false;
  }

  CloseHardwareSocket();
  sock_num_ = 0;
  return true;
}

#define STATUS_IS_UNEXPECTED_MESSAGE(expected_str, some_status,             \
                                     current_status)                        \
  BaseHex << TAS_FLASHSTR("Expected " #some_status " to be ")               \
          << TAS_FLASHSTR(expected_str) << TAS_FLASHSTR(", but is ")        \
          << BaseHex << some_status << TAS_FLASHSTR("; current status is ") \
          << current_status

#define VERIFY_STATUS_IS(expected_status, some_status)               \
  TAS_DCHECK_EQ(expected_status, some_status)                        \
      << BaseHex << TAS_FLASHSTR("Expected " #some_status " to be ") \
      << expected_status << TAS_FLASHSTR(", but is ") << some_status

bool ServerSocket::BeginListening() {
  if (!HasSocket()) {
    return false;
  } else if (PlatformEthernet::SocketIsTcpListener(sock_num_, tcp_port_)) {
    // Already listening.
    return true;
  } else if (IsConnected()) {
    return false;
  }

  CloseHardwareSocket();

  if (PlatformEthernet::InitializeTcpListenerSocket(sock_num_, tcp_port_)) {
    last_status_ = PlatformEthernet::SocketStatus(sock_num_);
    TAS_VLOG(1) << TAS_FLASHSTR("Listening to port ") << tcp_port_
                << TAS_FLASHSTR(" on socket ") << sock_num_
                << TAS_FLASHSTR(", last_status is ") << BaseHex << last_status_;
    VERIFY_STATUS_IS(SnSR::LISTEN, last_status_);
    return true;
  }
  TAS_VLOG(1) << TAS_FLASHSTR("listen for ") << tcp_port_
              << TAS_FLASHSTR(" failed with socket ") << sock_num_;
  return false;
}

// Notifies listener_ of relevant events/states of the socket (i.e. a new
// connection from a client, available data to read, room to write, client
// disconnect). The current implementation will make at most one of the
// On<Event> calls per call to PerformIO. This method is expected to be called
// from the loop() function of an Arduino sketch (i.e. typically hundreds or
// thousands of times a second).
// TODO(jamessynge): IFF this doesn't perform well enough, investigate using the
// interrupt features of the W5500 to learn which sockets have state changes
// more rapidly.
void ServerSocket::PerformIO() {
  if (!HasSocket()) {
    return;
  }
  const auto status = PlatformEthernet::SocketStatus(sock_num_);
  const bool is_open = PlatformEthernet::StatusIsOpen(status);
  const auto past_status = last_status_;
  const bool was_open = PlatformEthernet::StatusIsOpen(past_status);

  last_status_ = status;

  if (was_open && !is_open) {
    // Connection closed without us taking action. Let the listener know.
    TAS_VLOG(2) << TAS_FLASHSTR("was open, not now");
    if (!disconnect_data_.disconnected) {
      disconnect_data_.RecordDisconnect();
      listener_.OnDisconnect();
    } else {
      TAS_VLOG(2) << TAS_FLASHSTR("Disconnect already recorded");
    }
    // We'll deal with the new status next time (e.g. FIN_WAIT or closing)
    return;
  }

  switch (status) {
    case SnSR::CLOSED:
      BeginListening();
      break;

    case SnSR::LISTEN:
      VERIFY_STATUS_IS(SnSR::LISTEN, past_status);
      break;

    case SnSR::SYNRECV:
      // This is a transient state that the chip handles (i.e. responds with a
      // SYN/ACK, waits for an ACK from the client to complete the three step
      // TCP handshake). If that times out or a RST is recieved, then the W5500
      // socket will transition to to closed, and we'll have to call
      // BeginListening again.
      //
      // To keep the debug macros in the following states simple, we overwrite
      // last_status_ here.
      VERIFY_STATUS_IS(SnSR::LISTEN, past_status);
      last_status_ = SnSR::LISTEN;
      break;

    case SnSR::ESTABLISHED:
      if (!was_open) {
        VERIFY_STATUS_IS(SnSR::LISTEN, past_status)
            << TAS_FLASHSTR(" while handling ESTABLISHED");
        AnnounceConnected();
      } else {
        VERIFY_STATUS_IS(SnSR::ESTABLISHED, past_status)
            << TAS_FLASHSTR(" while handling ESTABLISHED");
        AnnounceCanRead();
      }
      break;

    case SnSR::CLOSE_WAIT:
      if (!was_open) {
        VERIFY_STATUS_IS(SnSR::LISTEN, past_status)
            << TAS_FLASHSTR(" while handling CLOSE_WAIT");
        AnnounceConnected();
      } else {
        TAS_DCHECK(past_status == SnSR::ESTABLISHED ||
                   past_status == SnSR::CLOSE_WAIT)
            << STATUS_IS_UNEXPECTED_MESSAGE("ESTABLISHED or CLOSE_WAIT",
                                            past_status, status);
        HandleCloseWait();
      }
      break;

    case SnSR::FIN_WAIT:
    case SnSR::CLOSING:
    case SnSR::TIME_WAIT:
    case SnSR::LAST_ACK:
      // Transient states after the connection is closed, but before the final
      // cleanup is complete.
      TAS_DCHECK(was_open || PlatformEthernet::StatusIsClosing(past_status))
          << STATUS_IS_UNEXPECTED_MESSAGE("a closing value", past_status,
                                          status);

      DetectCloseTimeout();
      break;

    case SnSR::INIT:
      // This is a transient state during setup of a TCP listener, and should
      // not be visible to us because BeginListening should make calls that
      // complete the process.
      TAS_DCHECK(false) << TAS_FLASHSTR(
                               "Socket in INIT state, incomplete LISTEN setup; "
                               "past_status is ")
                        << past_status;
      if (past_status == SnSR::INIT) {
        // Apparently stuck in this state.
        CloseHardwareSocket();
      }
      break;

    ///////////////////////////////////////////////////////////////////////////
    // States that the hardware socket should not be in if it is being used as a
    // TCP server socket.
    case SnSR::SYNSENT:
      // SYNSENT indicates we decided to use the socket as a client socket. Must
      // release the socket first.

    case SnSR::UDP:
    case SnSR::IPRAW:
    case SnSR::MACRAW:
    case SnSR::PPPOE:
      TAS_DCHECK(false) << TAS_FLASHSTR("Socket ") << sock_num_ << BaseHex
                        << TAS_FLASHSTR(" has unexpected status ") << status
                        << TAS_FLASHSTR(", past_status is ") << past_status;
      CloseHardwareSocket();
      break;

    default:
      // I noticed that status sometimes equals 0x11 after LISTEN, but 0x11 is
      // not a documented value. I asked on the WIZnet developer forum, and got
      // this response:
      //
      //   You can ignore it except for the state values specified in the
      //   datasheet. All status values are not disclosed due to company policy.
      //
      // Therefore, I'm restoring last_status_ to the value it had prior to this
      // undocumented status. Note that this works in this spot, but there are
      // other areas in the code where it will cause a problem. Sigh.
      last_status_ = past_status;
      break;
  }
}

// NOTE: Could choose to add another method that accepts a member function
// pointer to one of the SocketListener methods, and then delegate from the
// AnnounceX methods to that method. It may be worth doing if it notably reduces
// flash consumption.

void ServerSocket::AnnounceConnected() {
  EthernetClient client(sock_num_);
  uint8_t write_buffer[kWriteBufferSize];
  TcpServerConnection conn(write_buffer, kWriteBufferSize, client,
                           disconnect_data_);
  listener_.OnConnect(conn);
  DetectListenerInitiatedDisconnect();
}

void ServerSocket::AnnounceCanRead() {
  EthernetClient client(sock_num_);
  uint8_t write_buffer[kWriteBufferSize];
  TcpServerConnection conn(write_buffer, kWriteBufferSize, client,
                           disconnect_data_);
  listener_.OnCanRead(conn);
  DetectListenerInitiatedDisconnect();
}

void ServerSocket::HandleCloseWait() {
  EthernetClient client(sock_num_);
  uint8_t write_buffer[kWriteBufferSize];
  TcpServerConnection conn(write_buffer, kWriteBufferSize, client,
                           disconnect_data_);
  if (client.available() > 0) {
    // Still have data that we can read from the client (i.e. buffered up in the
    // network chip).
    // TODO(jamessynge): Determine whether we get the CLOSE_WAIT state before
    // we've read all the data from the client, or only once we've drained those
    // buffers.
    listener_.OnCanRead(conn);
  } else {
    listener_.OnHalfClosed(conn);
    TAS_VLOG(2) << TAS_FLASHSTR("HandleCloseWait ")
                << TAS_FLASHSTR("disconnected=")
                << disconnect_data_.disconnected;
  }
  DetectListenerInitiatedDisconnect();
}

void ServerSocket::DetectListenerInitiatedDisconnect() {
  TAS_VLOG(9) << TAS_FLASHSTR("DetectListenerInitiatedDisconnect ")
              << TAS_FLASHSTR("disconnected=") << disconnect_data_.disconnected;
  if (disconnect_data_.disconnected) {
    auto new_status = PlatformEthernet::SocketStatus(sock_num_);
    TAS_VLOG(2) << TAS_FLASHSTR("DetectListenerInitiatedDisconnect") << BaseHex
                << TAS_FLASHSTR(" last_status=") << last_status_
                << TAS_FLASHSTR(" new_status=") << new_status;
    last_status_ = new_status;
  }
}

void ServerSocket::DetectCloseTimeout() {
  // The Ethernet3 library baked in a limit of 1 second for closing a
  // connection, and did so by using a loop checking to see if the connection
  // closed. Since this implementation doesn't block in a loop, we can allow a
  // bit more time.
  if (disconnect_data_.disconnected &&
      disconnect_data_.ElapsedDisconnectTime() > kDisconnectMaxMillis) {
    // Time to give up.
    TAS_VLOG(2) << TAS_FLASHSTR("DetectCloseTimeout closing socket");
    CloseHardwareSocket();
  }
}

void ServerSocket::CloseHardwareSocket() {
  TAS_VLOG(2) << TAS_FLASHSTR("CloseHardwareSocket")
              << TAS_FLASHSTR(" last_status=") << BaseHex << last_status_;
  PlatformEthernet::CloseSocket(sock_num_);
  last_status_ = PlatformEthernet::SocketStatus(sock_num_);
  TAS_DCHECK_EQ(last_status_, SnSR::CLOSED);
}

void ServerSocket::DisconnectData::RecordDisconnect() {
  if (!disconnected) {
    TAS_VLOG(2) << TAS_FLASHSTR("DisconnectData::RecordDisconnect");
    disconnected = true;
    disconnect_time_millis = millis();
  }
}

void ServerSocket::DisconnectData::Reset() {
  disconnected = false;
  disconnect_time_millis = 0;
}

MillisT ServerSocket::DisconnectData::ElapsedDisconnectTime() {
  TAS_DCHECK(disconnected);
  return ElapsedMillis(disconnect_time_millis);
}

}  // namespace alpaca
