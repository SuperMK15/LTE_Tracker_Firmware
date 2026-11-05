#pragma once

#include <Arduino.h>

#include "ModemDriver.h"
#include "Telemetry.h"

class SupabaseClient {
 public:
  explicit SupabaseClient(ModemDriver& modem);

  bool publish(const LocationSample* samples, size_t count,
               const String& sessionId, int& httpStatus);

 private:
  ModemDriver& modem_;

  bool parseEndpoint(String& host, String& basePath) const;
};
