#ifdef TAS_ENABLE_CHECK
#undef TAS_ENABLE_CHECK
#endif

#ifdef TAS_ENABLED_VLOG_LEVEL
#undef TAS_ENABLED_VLOG_LEVEL
#endif

#include <Arduino.h>
#include <TinyAlpacaServer.h>

void setup() {
  // Setup serial, wait for it to be ready so that our logging messages can be
  // read.
  Serial.begin(9600);
  // Wait for serial port to connect, or at least some minimum amount of time
  // (TBD), else the initial output gets lost.
  while (!Serial) {
  }

  Serial.println("calling test function");
  LogExperimentTestFunction();
}

void loop() {
  Serial.println("loop entry");
  
  delay(1000);  
}