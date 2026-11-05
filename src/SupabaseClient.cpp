#include "SupabaseClient.h"

#include <ArduinoJson.h>
#include <stdlib.h>

#include "secrets.h"

namespace {
String decodeChunkedBody(const String& body) {
  String decoded;
  size_t position = 0;
  while (position < body.length()) {
    const int lineEnd = body.indexOf("\r\n", position);
    if (lineEnd < 0) {
      return body;
    }

    String sizeLine = body.substring(position, lineEnd);
    const int extension = sizeLine.indexOf(';');
    if (extension >= 0) {
      sizeLine.remove(extension);
    }
    char* end = nullptr;
    const unsigned long chunkSize = strtoul(sizeLine.c_str(), &end, 16);
    if (end == sizeLine.c_str() || *end != '\0') {
      return body;
    }

    position = static_cast<size_t>(lineEnd) + 2;
    if (chunkSize == 0) {
      return decoded;
    }
    if (chunkSize > body.length() - position) {
      return body;
    }

    decoded += body.substring(position, position + chunkSize);
    position += chunkSize;
    if (position + 2 > body.length() || body.substring(position, position + 2) != "\r\n") {
      return body;
    }
    position += 2;
  }
  return decoded;
}
}  // namespace

SupabaseClient::SupabaseClient(ModemDriver& modem) : modem_(modem) {}

bool SupabaseClient::parseEndpoint(String& host, String& basePath) const {
  String url = SUPABASE_URL;
  if (!url.startsWith("https://")) {
    Serial.println("SUPABASE_URL must use https://.");
    return false;
  }
  url.remove(0, 8);

  const int pathStart = url.indexOf('/');
  if (pathStart < 0) {
    host = url;
    basePath = "";
  } else {
    host = url.substring(0, pathStart);
    basePath = url.substring(pathStart);
    while (basePath.endsWith("/")) {
      basePath.remove(basePath.length() - 1);
    }
  }
  if (host.isEmpty()) {
    Serial.println("SUPABASE_URL does not contain a host.");
    return false;
  }
  return true;
}

bool SupabaseClient::publish(const LocationSample* samples, size_t count,
                             const String& sessionId, int& httpStatus) {
  httpStatus = 0;
  if (samples == nullptr || count == 0) {
    Serial.println("Supabase upload skipped: empty sample batch.");
    return false;
  }
  if (!modem_.isPacketDataConnected() && !modem_.ensureDataConnection()) {
    return false;
  }

  String host;
  String basePath;
  if (!parseEndpoint(host, basePath)) {
    return false;
  }

  JsonDocument document;
  JsonArray rows = document.to<JsonArray>();
  for (size_t i = 0; i < count; ++i) {
    const LocationSample& sample = samples[i];
    JsonObject row = rows.add<JsonObject>();
    row["session_id"] = sessionId;
    row["sequence"] = sample.sequence;
    row["elapsed_ms"] = sample.elapsedMs;
    row["fix"] = sample.hasFix;
    if (sample.gnssTime.isEmpty()) {
      row["gnss_time"].set(nullptr);
    } else {
      row["gnss_time"] = sample.gnssTime;
    }
    if (sample.hasFix) {
      row["latitude"] = sample.latitude;
      row["longitude"] = sample.longitude;
      row["altitude_m"] = sample.altitudeM;
      row["speed_kmh"] = sample.speedKmh;
    } else {
      row["latitude"].set(nullptr);
      row["longitude"].set(nullptr);
      row["altitude_m"].set(nullptr);
      row["speed_kmh"].set(nullptr);
    }
  }

  String body;
  serializeJson(document, body);
  TinyGsmClientSecure& client = modem_.secureClient();
  client.setTimeout(2000);
  const uint32_t startedAt = millis();
  if (!client.connect(host.c_str(), 443, 5)) {
    Serial.print("Supabase TLS connection failed after ");
    Serial.print(millis() - startedAt);
    Serial.println(" ms.");
    return false;
  }

  const String path = basePath + "/rest/v1/tracker_locations";
  client.print("POST ");
  client.print(path);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(host);
  client.print("apikey: ");
  client.println(SUPABASE_ANON_KEY);
  client.print("Authorization: Bearer ");
  client.println(SUPABASE_ANON_KEY);
  client.println("Content-Type: application/json");
  client.println("Prefer: return=minimal");
  client.print("Content-Length: ");
  client.println(body.length());
  client.println("Connection: close");
  client.println();
  client.print(body);

  String responseLine = client.readStringUntil('\n');
  responseLine.trim();
  if (responseLine.isEmpty()) {
    client.stop(1000);
    httpStatus = -1;
    Serial.println("Supabase response timeout: no HTTP status line received.");
    return false;
  }
  const int statusStart = responseLine.indexOf(' ');
  if (statusStart < 0) {
    client.stop(1000);
    httpStatus = -1;
    Serial.print("Malformed Supabase HTTP status line: ");
    Serial.println(responseLine);
    return false;
  }
  httpStatus =
      responseLine.substring(statusStart + 1).toInt();
  if (httpStatus == 0) {
    client.stop(1000);
    httpStatus = -1;
    Serial.print("Malformed Supabase HTTP status line: ");
    Serial.println(responseLine);
    return false;
  }
  while (client.connected()) {
    const String headerLine = client.readStringUntil('\n');
    if (headerLine == "\r" || headerLine.isEmpty()) {
      break;
    }
  }
  String responseBody = client.readString();
  client.stop(1000);
  if (responseBody.length() > 0) {
    responseBody = decodeChunkedBody(responseBody);
  }
  if (httpStatus < 200 || httpStatus >= 300) {
    Serial.print("Supabase returned HTTP status ");
    Serial.print(httpStatus);
    Serial.println(".");
    responseBody.trim();
    if (!responseBody.isEmpty()) {
      if (responseBody.length() > 512) {
        responseBody.remove(512);
      }
      Serial.print("Supabase response: ");
      Serial.println(responseBody);
    }
    return false;
  }
  Serial.print("Supabase uploaded ");
  Serial.print(count);
  Serial.print(" samples through sequence ");
  Serial.print(samples[count - 1].sequence);
  Serial.print(" in ");
  Serial.print(millis() - startedAt);
  Serial.println(" ms.");
  return true;
}
