#include "GnssDriver.h"

#include <stdio.h>

#include "BoardConfig.h"

GnssDriver::GnssDriver() : serial_(0) {}

void GnssDriver::begin() {
  serial_.begin(BoardConfig::GPS_BAUD, SERIAL_8N1, BoardConfig::GPS_RX,
                BoardConfig::GPS_TX);
  Serial.println("Holybro M10 UART GNSS started at 115200 baud.");
}

void GnssDriver::update() {
  while (serial_.available() > 0) {
    parser_.encode(static_cast<char>(serial_.read()));
  }
}

bool GnssDriver::readLocation(LocationSample& sample) {
  sample.hasFix =
      parser_.location.isValid() && parser_.location.age() <= 2500;
  sample.gnssTime = "";

  if (parser_.date.isValid() && parser_.time.isValid()) {
    char timestamp[21];
    snprintf(timestamp, sizeof(timestamp),
             "%04d-%02d-%02dT%02d:%02d:%02dZ", parser_.date.year(),
             parser_.date.month(), parser_.date.day(), parser_.time.hour(),
             parser_.time.minute(), parser_.time.second());
    sample.gnssTime = timestamp;
  }

  if (!sample.hasFix) {
    return true;
  }

  sample.latitude = parser_.location.lat();
  sample.longitude = parser_.location.lng();
  sample.altitudeM = parser_.altitude.isValid() ? parser_.altitude.meters() : 0;
  sample.speedKmh = parser_.speed.isValid() ? parser_.speed.kmph() : 0;
  return true;
}

uint32_t GnssDriver::charsProcessed() const {
  return parser_.charsProcessed();
}

uint32_t GnssDriver::passedChecksum() const {
  return parser_.passedChecksum();
}

uint32_t GnssDriver::failedChecksum() const {
  return parser_.failedChecksum();
}
