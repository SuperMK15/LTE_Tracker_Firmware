#include "DebugServer.h"

#include <LittleFS.h>
#include <WiFi.h>

#include <ArduinoJson.h>

#include "secrets.h"

namespace {
String makeAccessPointPassword() {
  const String configured = DEBUG_AP_PASSWORD;
  if (configured.length() >= 8) {
    return configured;
  }
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  String password;
  for (uint8_t i = 0; i < 10; ++i) {
    password += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  }
  return password;
}
}  // namespace

DebugServer::DebugServer() : server_(80) {}

bool DebugServer::begin(QueueHandle_t testQueue) {
  testQueue_ = testQueue;
  if (!LittleFS.begin(false)) {
    Serial.println("LittleFS mount failed; web page is unavailable.");
  }

  const uint64_t mac = ESP.getEfuseMac();
  char suffix[5];
  snprintf(suffix, sizeof(suffix), "%04X",
           static_cast<unsigned int>(mac & 0xFFFF));
  ssid_ = String("WARG-Tracker-") + suffix;
  const String password = makeAccessPointPassword();

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid_.c_str(), password.c_str())) {
    Serial.println("Unable to start debug access point.");
    return false;
  }

  server_.on("/", HTTP_GET, [this]() { serveIndex(); });
  server_.on("/style.css", HTTP_GET, [this]() {
    File file = LittleFS.open("/style.css", FILE_READ);
    if (!file) {
      server_.send(404, "text/plain", "Missing stylesheet");
      return;
    }
    server_.streamFile(file, "text/css");
    file.close();
  });
  server_.on("/app.js", HTTP_GET, [this]() {
    File file = LittleFS.open("/app.js", FILE_READ);
    if (!file) {
      server_.send(404, "text/plain", "Missing application script");
      return;
    }
    server_.streamFile(file, "application/javascript");
    file.close();
  });
  server_.on("/api/status", HTTP_GET, [this]() { serveStatus(); });
  server_.on("/api/test-location", HTTP_POST,
             [this]() { queueTestLocation(); });
  server_.onNotFound([this]() { handleNotFound(); });
  server_.begin();

  Serial.print("Debug AP SSID: ");
  Serial.println(ssid_);
  Serial.print("Debug AP password: ");
  Serial.println(password);
  Serial.print("Debug page: http://");
  Serial.println(WiFi.softAPIP());
  return true;
}

void DebugServer::update(const AppStatus& status) {
  status_ = status;
}

void DebugServer::handleClient() {
  server_.handleClient();
}

void DebugServer::serveIndex() {
  if (!LittleFS.exists("/index.html")) {
    server_.send(503, "text/plain", "LittleFS page is not installed.");
    return;
  }
  File page = LittleFS.open("/index.html", FILE_READ);
  server_.streamFile(page, "text/html");
  page.close();
}

void DebugServer::serveStatus() {
  JsonDocument document;
  document["session_id"] = status_.sessionId;
  document["sequence"] = status_.latestSample.sequence;
  document["uptime_ms"] = status_.latestSample.elapsedMs;
  document["fix"] = status_.latestSample.hasFix;
  document["gnss_time"] = status_.latestSample.gnssTime;
  if (status_.latestSample.hasFix) {
    document["latitude"] = status_.latestSample.latitude;
    document["longitude"] = status_.latestSample.longitude;
    document["altitude_m"] = status_.latestSample.altitudeM;
    document["speed_kmh"] = status_.latestSample.speedKmh;
  }
  document["modem_available"] = status_.modemAvailable;
  document["network_registered"] = status_.networkRegistered;
  document["packet_data_connected"] = status_.packetDataConnected;
  document["sd_available"] = status_.sdAvailable;
  document["last_upload"] = status_.lastUpload;
  document["uploaded_sequence"] = status_.uploadedSequence;
  document["dropped_samples"] = status_.droppedSamples;
  document["missed_samples"] = status_.missedSamples;
  document["queued_samples"] = status_.queuedSamples;
  document["gps_chars_processed"] = status_.gpsCharsProcessed;
  document["gps_nmea_valid"] = status_.gpsPassedChecksum;
  document["gps_nmea_checksum_errors"] = status_.gpsFailedChecksum;

  String payload;
  serializeJson(document, payload);
  server_.send(200, "application/json", payload);
}

void DebugServer::queueTestLocation() {
  const uint8_t request = 1;
  if (testQueue_ == nullptr ||
      xQueueSend(testQueue_, &request, 0) != pdTRUE) {
    server_.send(503, "application/json",
                 "{\"queued\":false,\"error\":\"Test queue is full\"}");
    return;
  }
  server_.send(202, "application/json", "{\"queued\":true}");
}

void DebugServer::handleNotFound() {
  server_.send(404, "text/plain", "Not found");
}
