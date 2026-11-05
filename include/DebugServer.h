#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Telemetry.h"

class DebugServer {
 public:
  DebugServer();

  bool begin(QueueHandle_t testQueue);
  void update(const AppStatus& status);
  void handleClient();

 private:
  WebServer server_;
  AppStatus status_;
  String ssid_;
  QueueHandle_t testQueue_ = nullptr;

  void serveIndex();
  void serveStatus();
  void queueTestLocation();
  void handleNotFound();
};
