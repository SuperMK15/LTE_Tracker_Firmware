#include <Arduino.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "BoardConfig.h"
#include "DebugServer.h"
#include "GnssDriver.h"
#include "ModemDriver.h"
#include "SDLogger.h"
#include "MqttPublisher.h"
#include "Telemetry.h"

namespace {
ModemDriver modem;
GnssDriver gnss;
SDLogger sdLogger;
DebugServer debugServer;
MqttPublisher mqttPublisher(modem);
AppStatus status;
SemaphoreHandle_t stateMutex = nullptr;
QueueHandle_t testQueue = nullptr;
LocationSample pendingSamples[BoardConfig::PENDING_SAMPLE_CAPACITY];
size_t pendingHead = 0;
size_t pendingCount = 0;

String makeSessionId() {
  uint8_t bytes[16];
  esp_fill_random(bytes, sizeof(bytes));
  bytes[6] = (bytes[6] & 0x0F) | 0x40;
  bytes[8] = (bytes[8] & 0x3F) | 0x80;

  char id[37];
  snprintf(id, sizeof(id),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
           bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
           bytes[12], bytes[13], bytes[14], bytes[15]);
  return String(id);
}

AppStatus statusSnapshot() {
  AppStatus snapshot;
  if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
    snapshot = status;
    xSemaphoreGive(stateMutex);
  }
  return snapshot;
}

void updateNetworkStatus(const AppStatus& networkStatus) {
  if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
    status.modemAvailable = networkStatus.modemAvailable;
    status.networkRegistered = networkStatus.networkRegistered;
    status.packetDataConnected = networkStatus.packetDataConnected;
    status.lastUpload = networkStatus.lastUpload;
    status.uploadedSequence = networkStatus.uploadedSequence;
    xSemaphoreGive(stateMutex);
  }
}

void updateGnssStatus(const LocationSample& sample, bool sdAvailable,
                      uint32_t missedSamples, uint32_t charsProcessed,
                      uint32_t passedChecksum, uint32_t failedChecksum) {
  if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
    status.latestSample = sample;
    status.sdAvailable = sdAvailable;
    status.missedSamples = missedSamples;
    status.gpsCharsProcessed = charsProcessed;
    status.gpsPassedChecksum = passedChecksum;
    status.gpsFailedChecksum = failedChecksum;
    xSemaphoreGive(stateMutex);
  }
}

void enqueueSample(const LocationSample& sample) {
  if (xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE) {
    return;
  }
  if (pendingCount == BoardConfig::PENDING_SAMPLE_CAPACITY) {
    ++status.droppedSamples;
    Serial.print("Telemetry RAM queue full; new sample dropped. Total: ");
    Serial.println(status.droppedSamples);
    xSemaphoreGive(stateMutex);
    return;
  }
  const size_t tail =
      (pendingHead + pendingCount) % BoardConfig::PENDING_SAMPLE_CAPACITY;
  pendingSamples[tail] = sample;
  ++pendingCount;
  status.queuedSamples = pendingCount;
  xSemaphoreGive(stateMutex);
}

size_t copyPendingBatch(LocationSample* batch, size_t capacity) {
  if (xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE) {
    return 0;
  }
  const size_t count = pendingCount < capacity ? pendingCount : capacity;
  for (size_t i = 0; i < count; ++i) {
    batch[i] =
        pendingSamples[(pendingHead + i) % BoardConfig::PENDING_SAMPLE_CAPACITY];
  }
  xSemaphoreGive(stateMutex);
  return count;
}

void removeUploadedSamples(const LocationSample* batch, size_t count) {
  if (xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE) {
    return;
  }

  const size_t queuedBeforeAcknowledgement = pendingCount;
  size_t retainedCount = 0;
  for (size_t i = 0; i < queuedBeforeAcknowledgement; ++i) {
    const size_t sourceIndex =
        (pendingHead + i) % BoardConfig::PENDING_SAMPLE_CAPACITY;
    const LocationSample& queued = pendingSamples[sourceIndex];
    bool wasUploaded = false;
    for (size_t j = 0; j < count; ++j) {
      if (queued.sequence == batch[j].sequence &&
          queued.elapsedMs == batch[j].elapsedMs) {
        wasUploaded = true;
        break;
      }
    }
    if (!wasUploaded) {
      const size_t destinationIndex =
          (pendingHead + retainedCount) % BoardConfig::PENDING_SAMPLE_CAPACITY;
      if (destinationIndex != sourceIndex) {
        pendingSamples[destinationIndex] = queued;
      }
      ++retainedCount;
    }
  }
  pendingCount = retainedCount;
  status.queuedSamples = pendingCount;
  if (count > 0) {
    status.uploadedSequence = batch[count - 1].sequence;
    status.lastUpload =
        batch[count - 1].testSample
            ? "MQTT broker acknowledged test sequence " +
                  String(batch[count - 1].sequence)
            : "MQTT broker acknowledged through sequence " +
                  String(batch[count - 1].sequence);
  }
  xSemaphoreGive(stateMutex);
}

void setLastUpload(const String& message) {
  if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
    status.lastUpload = message;
    xSemaphoreGive(stateMutex);
  }
}

void gnssTask(void*) {
  const String sessionId = statusSnapshot().sessionId;
  gnss.begin();
  sdLogger.begin();

  uint32_t nextSampleAt = millis();
  uint32_t missedSamples = 0;
  int64_t sequence = 0;

  for (;;) {
    gnss.update();
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - nextSampleAt) >= 0) {
      const uint32_t missed =
          (now - nextSampleAt) / BoardConfig::SAMPLE_INTERVAL_MS;
      missedSamples += missed;
      nextSampleAt +=
          (missed + 1) * BoardConfig::SAMPLE_INTERVAL_MS;

      LocationSample sample;
      sample.sequence = ++sequence;
      sample.elapsedMs = now;
      gnss.readLocation(sample);
      sdLogger.append(sample, sessionId);
      enqueueSample(sample);
      updateGnssStatus(sample, sdLogger.available(), missedSamples,
                        gnss.charsProcessed(), gnss.passedChecksum(),
                        gnss.failedChecksum());

      Serial.print("Sample ");
      Serial.print(sample.sequence);
      Serial.print(": ");
      if (sample.hasFix) {
        Serial.print(sample.latitude, 6);
        Serial.print(", ");
        Serial.println(sample.longitude, 6);
      } else {
        Serial.print("no M10 fix (NMEA bytes=");
        Serial.print(gnss.charsProcessed());
        Serial.print(", valid=");
        Serial.print(gnss.passedChecksum());
        Serial.print(", checksum errors=");
        Serial.print(gnss.failedChecksum());
        Serial.println(")");
      }
    }

    sdLogger.updateCard();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void serviceCellular(AppStatus& networkStatus, uint32_t& nextAttemptAt) {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextAttemptAt) < 0) {
    return;
  }
  nextAttemptAt = now + BoardConfig::CELLULAR_RETRY_INTERVAL_MS;
  if (!networkStatus.modemAvailable) {
    networkStatus.modemAvailable = modem.retryInitialization();
    if (!networkStatus.modemAvailable) {
      Serial.println("SIM7080G is still unavailable; will retry.");
      return;
    }
    Serial.println("SIM7080G modem recovered.");
  }
  if (!networkStatus.packetDataConnected) {
    networkStatus.packetDataConnected = modem.ensureDataConnection();
  } else {
    networkStatus.packetDataConnected = modem.isPacketDataConnected();
  }
  networkStatus.networkRegistered = modem.isNetworkRegistered();
  updateNetworkStatus(networkStatus);
}

void serviceUpload(AppStatus& networkStatus, uint32_t& nextUploadAt,
                   uint8_t& failureCount) {
  if (!networkStatus.packetDataConnected ||
      static_cast<int32_t>(millis() - nextUploadAt) < 0) {
    return;
  }

  LocationSample sample;
  if (copyPendingBatch(&sample, 1) == 0) {
    return;
  }

  if (mqttPublisher.publish(sample, networkStatus.sessionId)) {
    removeUploadedSamples(&sample, 1);
    failureCount = 0;
    nextUploadAt = millis();
  } else {
    const String message = "MQTT publish failed; retry scheduled";
    networkStatus.lastUpload = message;
    setLastUpload(message);
    networkStatus.packetDataConnected = modem.isPacketDataConnected();
    updateNetworkStatus(networkStatus);
    const uint8_t exponent = failureCount < 4 ? failureCount : 4;
    uint32_t retryDelay = BoardConfig::UPLOAD_RETRY_INITIAL_MS << exponent;
    if (retryDelay > BoardConfig::UPLOAD_RETRY_MAX_MS) {
      retryDelay = BoardConfig::UPLOAD_RETRY_MAX_MS;
    }
    if (failureCount < 5) {
      ++failureCount;
    }
    nextUploadAt = millis() + retryDelay;
    Serial.print("Retrying MQTT publish in ");
    Serial.print(retryDelay);
    Serial.println(" ms.");
  }
}

void enqueueTestLocation(int64_t& nextTestSequence) {
  uint8_t request;
  if (xQueueReceive(testQueue, &request, 0) != pdTRUE) {
    return;
  }

  LocationSample sample;
  sample.sequence = nextTestSequence--;
  sample.elapsedMs = millis();
  sample.hasFix = true;
  sample.testSample = true;
  sample.latitude = 47.6553;
  sample.longitude = -122.3080;
  sample.altitudeM = 0;
  sample.speedKmh = 0;
  enqueueSample(sample);
  setLastUpload("Test coordinate queued (sequence " +
                String(sample.sequence) + ")");
  Serial.print("Queued fake test coordinate, sequence ");
  Serial.println(sample.sequence);
}

void telemetryTask(void*) {
  AppStatus networkStatus = statusSnapshot();
  networkStatus.modemAvailable = modem.begin();
  networkStatus.networkRegistered = modem.isNetworkRegistered();
  networkStatus.packetDataConnected = modem.isPacketDataConnected();
  updateNetworkStatus(networkStatus);

  uint32_t nextCellularAttemptAt = millis();
  uint32_t nextUploadAt = millis() + BoardConfig::UPLOAD_INTERVAL_MS;
  uint32_t nextMqttPollAt = millis();
  int64_t nextTestSequence = -1;
  uint8_t uploadFailureCount = 0;

  for (;;) {
    enqueueTestLocation(nextTestSequence);
    serviceCellular(networkStatus, nextCellularAttemptAt);
    if (static_cast<int32_t>(millis() - nextMqttPollAt) >= 0) {
      nextMqttPollAt = millis() + BoardConfig::MQTT_POLL_INTERVAL_MS;
      mqttPublisher.poll();
    }
    serviceUpload(networkStatus, nextUploadAt, uploadFailureCount);
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  stateMutex = xSemaphoreCreateMutex();
  testQueue = xQueueCreate(4, sizeof(uint8_t));
  if (stateMutex == nullptr || testQueue == nullptr) {
    Serial.println("Unable to initialize firmware task synchronization.");
    return;
  }

  status.sessionId = makeSessionId();
  Serial.println();
  Serial.println("WARG LTE tracker starting.");
  Serial.print("Session: ");
  Serial.println(status.sessionId);

  debugServer.begin(testQueue);
  debugServer.update(statusSnapshot());
  if (xTaskCreate(gnssTask, "gnss", 6144, nullptr, 2, nullptr) != pdPASS) {
    Serial.println("Unable to start external GNSS task.");
  }
  if (xTaskCreate(telemetryTask, "cellular", 12288, nullptr, 1, nullptr) !=
      pdPASS) {
    Serial.println("Unable to start cellular task.");
  }
}

void loop() {
  debugServer.handleClient();
  static uint32_t nextStatusRefresh = 0;
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextStatusRefresh) >= 0) {
    nextStatusRefresh = now + 250;
    debugServer.update(statusSnapshot());
  }
  delay(2);
}
