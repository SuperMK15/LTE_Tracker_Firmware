#pragma once

#include <Arduino.h>
#include <TinyGsmClient.h>

class ModemDriver {
 public:
  ModemDriver();

  bool begin();
  bool retryInitialization();
  bool ensureDataConnection();
  bool isNetworkRegistered();
  bool isPacketDataConnected();
  TinyGsmClientSecure& secureClient();

 private:
  HardwareSerial serial_;
  TinyGsm modem_;
  TinyGsmClientSecure secureClient_;

  bool powerOn();
};
