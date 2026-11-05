#include "GnssDriver.h"

#include <stdio.h>

#include "BoardConfig.h"

namespace {
bool isValidDate(int year, int month, int day) {
  if (month < 1 || month > 12 || day < 1) {
    return false;
  }

  const bool leapYear =
      (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  const int daysInMonth[] = {31, leapYear ? 29 : 28, 31, 30, 31, 30,
                             31, 31, 30, 31, 30, 31};
  return day <= daysInMonth[month - 1];
}

bool isValidTime(int hour, int minute, int second) {
  return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 &&
         second >= 0 && second <= 60;
}
}  // namespace

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

  const int year = parser_.date.year();
  const int month = parser_.date.month();
  const int day = parser_.date.day();
  const int hour = parser_.time.hour();
  const int minute = parser_.time.minute();
  const int second = parser_.time.second();
  if (parser_.date.isValid() && parser_.time.isValid() &&
      isValidDate(year, month, day) && isValidTime(hour, minute, second)) {
    char timestamp[21];
    snprintf(timestamp, sizeof(timestamp),
             "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, day, hour,
             minute, second);
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
