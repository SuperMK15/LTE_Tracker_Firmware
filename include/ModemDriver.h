#pragma once

#include <Arduino.h>
#include <TinyGsmClient.h>

#include "BoardConfig.h"

class MqttTcpClient : public TinyGsmClient {
 public:
  explicit MqttTcpClient(TinyGsm& modem) : TinyGsmClient(modem) {}

  int connect(const char* host, uint16_t port) override {
    return TinyGsmClient::connect(host, port,
                                  BoardConfig::MQTT_TCP_CONNECT_TIMEOUT_SECONDS);
  }

  int connect(IPAddress ip, uint16_t port) override {
    return TinyGsmClient::connect(ip, port,
                                  BoardConfig::MQTT_TCP_CONNECT_TIMEOUT_SECONDS);
  }
};

class ModemDriver {
 public:
  ModemDriver();

  bool begin();
  bool retryInitialization();
  bool ensureDataConnection();
  bool isNetworkRegistered();
  bool isPacketDataConnected();
  TinyGsmClientSecure& secureClient();
  MqttTcpClient& mqttClient();

 private:
  HardwareSerial serial_;
  TinyGsm modem_;
  TinyGsmClientSecure secureClient_;
  MqttTcpClient mqttClient_;

  bool powerOn();
};
