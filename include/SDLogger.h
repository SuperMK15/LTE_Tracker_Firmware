#pragma once

#include <Arduino.h>
#include <SPI.h>

#include "Telemetry.h"

class SDLogger {
 public:
  SDLogger();

  void begin();
  void updateCard();
  bool append(const LocationSample& sample, const String& sessionId);
  bool available() const;

 private:
  SPIClass spi_;
  bool mounted_;
  uint32_t nextMountAttempt_;
  String currentSession_;
  String filePath_;

  bool cardInserted() const;
  bool mountCard();
  bool openSessionFile(const String& sessionId);
};
