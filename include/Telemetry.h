#pragma once

#include <Arduino.h>

struct LocationSample {
  int64_t sequence = 0;
  uint32_t elapsedMs = 0;
  bool hasFix = false;
  bool testSample = false;
  String gnssTime;
  double latitude = 0;
  double longitude = 0;
  double altitudeM = 0;
  double speedKmh = 0;
};

struct AppStatus {
  String sessionId;
  LocationSample latestSample;
  bool modemAvailable = false;
  bool networkRegistered = false;
  bool packetDataConnected = false;
  bool sdAvailable = false;
  String lastUpload = "Not attempted";
  int64_t uploadedSequence = 0;
  uint32_t droppedSamples = 0;
  uint32_t missedSamples = 0;
  uint32_t queuedSamples = 0;
  uint32_t gpsCharsProcessed = 0;
  uint32_t gpsPassedChecksum = 0;
  uint32_t gpsFailedChecksum = 0;
};
