#include "ModemDriver.h"

#include "BoardConfig.h"
#include "secrets.h"

ModemDriver::ModemDriver()
    : serial_(1),
      modem_(serial_),
      secureClient_(modem_),
      mqttClient_(modem_) {}

bool ModemDriver::powerOn() {
  pinMode(BoardConfig::MODEM_PWRKEY, OUTPUT);
  digitalWrite(BoardConfig::MODEM_PWRKEY, LOW);
  delay(100);
  digitalWrite(BoardConfig::MODEM_PWRKEY, HIGH);
  delay(1200);
  digitalWrite(BoardConfig::MODEM_PWRKEY, LOW);
  delay(5000);
  return true;
}

bool ModemDriver::begin() {
  serial_.begin(115200, SERIAL_8N1, BoardConfig::MODEM_RX,
                BoardConfig::MODEM_TX);
  powerOn();

  if (!modem_.init()) {
    Serial.println("SIM7080G did not respond to AT initialization.");
    return false;
  }

  Serial.print("Modem: ");
  Serial.println(modem_.getModemName());
  modem_.setPreferredMode(3);
  return true;
}

bool ModemDriver::retryInitialization() {
  if (!modem_.init()) {
    return false;
  }
  modem_.setPreferredMode(3);
  return true;
}

bool ModemDriver::isNetworkRegistered() {
  return modem_.isNetworkConnected();
}

bool ModemDriver::isPacketDataConnected() {
  return modem_.isGprsConnected();
}

bool ModemDriver::ensureDataConnection() {
  if (!modem_.isNetworkConnected() && !modem_.waitForNetwork(1000L)) {
    return false;
  }
  if (modem_.isGprsConnected()) {
    return true;
  }

  Serial.println("Activating SIM7080G packet data.");
  const bool connected =
      modem_.gprsConnect(CELLULAR_APN, CELLULAR_USER, CELLULAR_PASSWORD);
  if (!connected) {
    Serial.println("SIM7080G packet data activation failed.");
  }
  return connected;
}

TinyGsmClientSecure& ModemDriver::secureClient() {
  return secureClient_;
}

MqttTcpClient& ModemDriver::mqttClient() {
  return mqttClient_;
}
