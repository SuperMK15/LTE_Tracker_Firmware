#pragma once

#include <Arduino.h>
#include <TinyGPSPlus.h>

#include "Telemetry.h"

class GnssDriver {
 public:
  GnssDriver();

  void begin();
  void update();
  bool readLocation(LocationSample& sample);
  uint32_t charsProcessed() const;
  uint32_t passedChecksum() const;
  uint32_t failedChecksum() const;

 private:
  HardwareSerial serial_;
  TinyGPSPlus parser_;
};
