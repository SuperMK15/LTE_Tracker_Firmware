#pragma once

#include <Arduino.h>

namespace BoardConfig {
constexpr int MODEM_RX = 20;
constexpr int MODEM_TX = 21;
constexpr int MODEM_PWRKEY = 0;
constexpr int GPS_RX = 5;
constexpr int GPS_TX = 4;
constexpr uint32_t GPS_BAUD = 115200;
constexpr int SD_SCK = 6;
constexpr int SD_MISO = 2;
constexpr int SD_MOSI = 7;
constexpr int SD_CS = 10;
constexpr int SD_CARD_DETECT = 3;
constexpr bool SD_CARD_DETECT_ACTIVE_LOW = true;
constexpr uint32_t SAMPLE_INTERVAL_MS = 1000;
constexpr uint32_t WIFI_STATUS_INTERVAL_MS = 1000;
constexpr uint32_t CELLULAR_RETRY_INTERVAL_MS = 15000;
constexpr size_t PENDING_SAMPLE_CAPACITY = 32;
constexpr uint32_t UPLOAD_INTERVAL_MS = 1000;
constexpr uint32_t UPLOAD_RETRY_INITIAL_MS = 5000;
constexpr uint32_t UPLOAD_RETRY_MAX_MS = 60000;
}  // namespace BoardConfig
