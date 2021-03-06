#include "tiny_alpaca_server.h"

#include "alpaca_response.h"
#include "constants.h"
#include "http_response_header.h"
#include "literals.h"
#include "utils/any_printable.h"
#include "utils/array_view.h"
#include "utils/counting_print.h"
#include "utils/json_encoder.h"
#include "utils/json_encoder_helpers.h"
#include "utils/printable_cat.h"

namespace alpaca {

TinyAlpacaServerBase::TinyAlpacaServerBase(
    const ServerDescription& server_description,
    ArrayView<DeviceInterface*> devices)
    : alpaca_devices_(devices),
      server_description_(server_description),
      server_transaction_id_(0) {}

bool TinyAlpacaServerBase::Initialize() { return alpaca_devices_.Initialize(); }

void TinyAlpacaServerBase::MaintainDevices() {
  alpaca_devices_.MaintainDevices();
}

void TinyAlpacaServerBase::OnStartDecoding(AlpacaRequest& request) {
  request.set_server_transaction_id(++server_transaction_id_);
}

bool TinyAlpacaServerBase::OnRequestDecoded(AlpacaRequest& request,
                                            Print& out) {
  switch (request.api) {
    case EAlpacaApi::kUnknown:
      break;

    case EAlpacaApi::kDeviceApi:
      TAS_FALLTHROUGH_INTENDED;
    case EAlpacaApi::kDeviceSetup:
      return alpaca_devices_.DispatchDeviceRequest(request, out);

    case EAlpacaApi::kManagementApiVersions:
      return HandleManagementApiVersions(request, out);

    case EAlpacaApi::kManagementDescription:
      return HandleManagementDescription(request, out);

    case EAlpacaApi::kManagementConfiguredDevices:
      return alpaca_devices_.HandleManagementConfiguredDevices(request, out);

    case EAlpacaApi::kServerSetup:
      return HandleServerSetup(request, out);
  }

  TAS_VLOG(5) << TAS_FLASHSTR("OnRequestDecoded: api=") << request.api;
  return WriteResponse::HttpErrorResponse(
      EHttpStatusCode::kHttpInternalServerError, Literals::ApiUnknown(), out);
}

void TinyAlpacaServerBase::OnRequestDecodingError(AlpacaRequest& request,
                                                  EHttpStatusCode status,
                                                  Print& out) {
  TAS_VLOG(3) << TAS_FLASHSTR("OnRequestDecodingError: status=") << status;
  WriteResponse::HttpErrorResponse(status, AnyPrintable(), out);
}

bool TinyAlpacaServerBase::HandleManagementApiVersions(AlpacaRequest& request,
                                                       Print& out) {
  TAS_VLOG(3) << TAS_FLASHSTR("HandleManagementApiVersions");
  uint32_t versions[] = {1};
  return WriteResponse::ArrayResponse(
      request, MakeArrayViewSource(versions, &JsonArrayEncoder::AddUIntElement),
      out);
}

bool TinyAlpacaServerBase::HandleManagementDescription(AlpacaRequest& request,
                                                       Print& out) {
  TAS_VLOG(3) << TAS_FLASHSTR(
      "TinyAlpacaServerBase::HandleManagementDescription");
  JsonPropertySourceAdapter<ServerDescription> description(server_description_);
  return WriteResponse::ObjectResponse(request, description, out);
}

bool TinyAlpacaServerBase::HandleServerSetup(AlpacaRequest& request,
                                             Print& out) {
  TAS_VLOG(3) << TAS_FLASHSTR("HandleServerSetup");

  auto body = TAS_FLASHSTR(
      "<html><body>"
      "<h1>Tiny Alpaca Server</h1>\n"
      "No setup\n"
      "</body></html>");

  return WriteResponse::OkResponse(request, EContentType::kTextHtml,
                                   AnyPrintable(body), out,
                                   /*append_http_newline=*/true);
}

TinyAlpacaServer::TinyAlpacaServer(uint16_t tcp_port,
                                   const ServerDescription& server_description,
                                   ArrayView<DeviceInterface*> devices)
    : TinyAlpacaServerBase(server_description, devices),
      sockets_(tcp_port, *this),
      discovery_server_(tcp_port) {}

bool TinyAlpacaServer::Initialize() {
  // Give everything a chance to initialize so that logs will contain relevant
  // info about any failures. Choosing to initialize the discovery server before
  // the sockets so that it occupies socket 0.
  bool result = TinyAlpacaServerBase::Initialize();
  if (!discovery_server_.Initialize()) {
    result = false;
  }
  if (!sockets_.Initialize()) {
    result = false;
  }
  return result;
}

void TinyAlpacaServer::PerformIO() {
  TinyAlpacaServerBase::MaintainDevices();
  discovery_server_.PerformIO();
  sockets_.PerformIO();
}

}  // namespace alpaca
