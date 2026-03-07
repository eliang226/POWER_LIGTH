#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

struct AppTelemetry {
  bool line1AcPresent = false;
  bool pzemValid = false;
  float pzemVoltage = 0.0f;
  float pzemCurrent = 0.0f;
  float pzemPower = 0.0f;
  float batteryVoltage = 0.0f;
  float batteryCurrent = 0.0f;
  uint8_t batteryCapacityPercent = 0;
};

class AppBridge {
 public:
  AppBridge();

  void begin();
  void update(uint32_t nowMs);

  void publishTelemetry(const AppTelemetry& telemetry, uint32_t nowMs);
  void publishAlert(const char* eventCode, uint32_t nowMs);

  bool enabled();
  bool mqttConnected();

 private:
  bool canRun_ = false;
  uint32_t lastWifiAttemptMs_ = 0;
  uint32_t lastMqttAttemptMs_ = 0;
  uint32_t lastTelemetryPublishMs_ = 0;

  char clientId_[48] = {0};
  char topicStatus_[96] = {0};
  char topicTelemetry_[96] = {0};
  char topicAlert_[96] = {0};

  WiFiClient wifiClient_;
  PubSubClient mqttClient_;

  void ensureWifi(uint32_t nowMs);
  void ensureMqtt(uint32_t nowMs);
  bool mqttReady();
};
