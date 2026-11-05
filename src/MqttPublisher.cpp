#include "MqttPublisher.h"

#include <ArduinoJson.h>

#include "BoardConfig.h"

MqttPublisher::MqttPublisher(ModemDriver& modem) : client_(modem.mqttClient()) {
  client_.setKeepAliveInterval(BoardConfig::MQTT_KEEPALIVE_SECONDS * 1000UL);
  client_.setConnectionTimeout(BoardConfig::MQTT_CONNECTION_TIMEOUT_MS);
  client_.setCleanSession(true);
}

String MqttPublisher::topicForSession(const String& sessionId) const {
  return String(BoardConfig::MQTT_TOPIC_ROOT) + "/" + sessionId + "/location";
}

bool MqttPublisher::connect(const String& sessionId) {
  if (client_.connected()) {
    return true;
  }

  const String clientId = "warg-" + sessionId;
  client_.setId(clientId);

  Serial.print("Connecting to MQTT broker ");
  Serial.print(BoardConfig::MQTT_BROKER_HOST);
  Serial.print(':');
  Serial.println(BoardConfig::MQTT_BROKER_PORT);

  const uint32_t startedAt = millis();
  if (!client_.connect(BoardConfig::MQTT_BROKER_HOST,
                       BoardConfig::MQTT_BROKER_PORT)) {
    Serial.print("MQTT connection failed after ");
    Serial.print(millis() - startedAt);
    Serial.print(" ms (error ");
    Serial.print(client_.connectError());
    Serial.println(").");
    return false;
  }

  Serial.print("MQTT connected in ");
  Serial.print(millis() - startedAt);
  Serial.print(" ms. Topic: ");
  Serial.println(topicForSession(sessionId));
  return true;
}

void MqttPublisher::poll() {
  if (client_.connected()) {
    client_.poll();
  }
}

bool MqttPublisher::publish(const LocationSample& sample,
                            const String& sessionId) {
  if (!connect(sessionId)) {
    return false;
  }

  client_.poll();

  JsonDocument document;
  document["session_id"] = sessionId;
  document["sequence"] = sample.sequence;
  document["elapsed_ms"] = sample.elapsedMs;
  document["fix"] = sample.hasFix;
  if (sample.gnssTime.isEmpty()) {
    document["gnss_time"].set(nullptr);
  } else {
    document["gnss_time"] = sample.gnssTime;
  }
  if (sample.hasFix) {
    document["latitude"] = sample.latitude;
    document["longitude"] = sample.longitude;
    document["altitude_m"] = sample.altitudeM;
    document["speed_kmh"] = sample.speedKmh;
  } else {
    document["latitude"].set(nullptr);
    document["longitude"].set(nullptr);
    document["altitude_m"].set(nullptr);
    document["speed_kmh"].set(nullptr);
  }

  String payload;
  serializeJson(document, payload);
  const String topic = topicForSession(sessionId);
  const uint32_t startedAt = millis();
  if (client_.beginMessage(topic, payload.length(), false, 1) != 1) {
    Serial.println("MQTT publish packet could not be started.");
    client_.stop();
    return false;
  }

  const size_t written =
      client_.write(reinterpret_cast<const uint8_t*>(payload.c_str()),
                    payload.length());
  if (written != payload.length()) {
    Serial.println("MQTT publish payload write was incomplete.");
    client_.stop();
    return false;
  }

  if (client_.endMessage() != 1) {
    Serial.print("MQTT broker did not acknowledge sequence ");
    Serial.print(sample.sequence);
    Serial.println(".");
    client_.stop();
    return false;
  }

  Serial.print("MQTT broker acknowledged sequence ");
  Serial.print(sample.sequence);
  Serial.print(" in ");
  Serial.print(millis() - startedAt);
  Serial.println(" ms.");
  return true;
}
