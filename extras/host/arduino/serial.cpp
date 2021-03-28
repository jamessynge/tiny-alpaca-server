#include "extras/host/arduino/serial.h"

#include <iostream>

size_t FakeSerial::write(uint8_t b) {
  std::cout.put(static_cast<char>(b));
  return 1;
}

size_t FakeSerial::write(const uint8_t* buffer, size_t size) {
  std::cout.write(reinterpret_cast<const char*>(buffer), size);
  return size;
}

void FakeSerial::flush() { std::cout.flush(); }

FakeSerial Serial;  // NOLINT
