#include "SDLogger.h"

#include <SD.h>

#include "BoardConfig.h"

SDLogger::SDLogger() : spi_(FSPI), mounted_(false), nextMountAttempt_(0) {}

void SDLogger::begin() {
  pinMode(BoardConfig::SD_CARD_DETECT, INPUT_PULLUP);
  spi_.begin(BoardConfig::SD_SCK, BoardConfig::SD_MISO, BoardConfig::SD_MOSI,
             BoardConfig::SD_CS);
  updateCard();
}

bool SDLogger::cardInserted() const {
  const bool level = digitalRead(BoardConfig::SD_CARD_DETECT) == HIGH;
  return BoardConfig::SD_CARD_DETECT_ACTIVE_LOW ? !level : level;
}

bool SDLogger::mountCard() {
  if (!cardInserted()) {
    return false;
  }
  if (!SD.begin(BoardConfig::SD_CS, spi_, 4000000)) {
    Serial.println("SD card mount failed.");
    return false;
  }
  mounted_ = true;
  Serial.println("SD card mounted.");
  return true;
}

void SDLogger::updateCard() {
  if (mounted_ && !cardInserted()) {
    SD.end();
    mounted_ = false;
    currentSession_ = "";
    filePath_ = "";
    Serial.println("SD card removed.");
    return;
  }
  if (!mounted_ && cardInserted()) {
    if (static_cast<int32_t>(millis() - nextMountAttempt_) >= 0) {
      nextMountAttempt_ = millis() + 2000;
      mountCard();
    }
  }
}

bool SDLogger::openSessionFile(const String& sessionId) {
  if (currentSession_ == sessionId) {
    return true;
  }

  currentSession_ = sessionId;
  filePath_ = "/track_" + sessionId + ".csv";
  const bool needsHeader = !SD.exists(filePath_);
  File file = SD.open(filePath_, FILE_APPEND);
  if (!file) {
    Serial.println("Unable to open session CSV on SD card.");
    currentSession_ = "";
    filePath_ = "";
    return false;
  }
  if (needsHeader) {
    file.println(
        "session_id,sequence,elapsed_ms,gnss_time,fix,latitude,longitude,"
        "altitude_m,speed_kmh");
  }
  file.close();
  return true;
}

bool SDLogger::append(const LocationSample& sample, const String& sessionId) {
  updateCard();
  if (!mounted_ || !openSessionFile(sessionId)) {
    return false;
  }

  File file = SD.open(filePath_, FILE_APPEND);
  if (!file) {
    Serial.println("Unable to append telemetry to SD card.");
    return false;
  }

  file.print(sessionId);
  file.print(',');
  file.print(sample.sequence);
  file.print(',');
  file.print(sample.elapsedMs);
  file.print(',');
  file.print(sample.gnssTime);
  file.print(',');
  file.print(sample.hasFix ? 1 : 0);
  if (sample.hasFix) {
    file.print(',');
    file.print(sample.latitude, 7);
    file.print(',');
    file.print(sample.longitude, 7);
    file.print(',');
    file.print(sample.altitudeM, 2);
    file.print(',');
    file.println(sample.speedKmh, 2);
  } else {
    file.println(",,,,");
  }
  file.close();
  return true;
}

bool SDLogger::available() const {
  return mounted_;
}
