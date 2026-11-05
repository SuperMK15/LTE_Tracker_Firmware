#pragma once

#include <Arduino.h>
#include <ArduinoMqttClient.h>

#include "ModemDriver.h"
#include "Telemetry.h"

class MqttPublisher {
 public:
  explicit MqttPublisher(ModemDriver& modem);

  bool publish(const LocationSample& sample, const String& sessionId);
  void poll();

 private:
  MqttClient client_;

  bool connect(const String& sessionId);
  String topicForSession(const String& sessionId) const;
};
