#include <AppBridge.h>

#include "AppBridgeConfig.h"

namespace {
constexpr uint32_t kWifiRetryMs = 7000;
constexpr uint32_t kMqttRetryMs = 5000;
constexpr uint32_t kTelemetryPublishMs = 5000;

bool hasValue(const char* text) {
  return text && text[0] != '\0';
}
}  // namespace

AppBridge::AppBridge() : mqttClient_(wifiClient_) {}

void AppBridge::begin() {
  canRun_ = hasValue(APP_WIFI_SSID) && hasValue(APP_MQTT_HOST);
  if (!canRun_) {
    Serial.println("AppBridge: deshabilitado (faltan credenciales WiFi/MQTT).");
    return;
  }

  const uint32_t chipId = static_cast<uint32_t>(ESP.getEfuseMac());
  snprintf(clientId_, sizeof(clientId_), "%s-%08lX", APP_DEVICE_ID, static_cast<unsigned long>(chipId));
  snprintf(topicStatus_, sizeof(topicStatus_), "%s/status", APP_MQTT_TOPIC_BASE);
  snprintf(topicTelemetry_, sizeof(topicTelemetry_), "%s/telemetry", APP_MQTT_TOPIC_BASE);
  snprintf(topicAlert_, sizeof(topicAlert_), "%s/alert", APP_MQTT_TOPIC_BASE);

  WiFi.mode(WIFI_STA);
  mqttClient_.setServer(APP_MQTT_HOST, APP_MQTT_PORT);
}

void AppBridge::ensureWifi(uint32_t nowMs) {
  if (!canRun_ || WiFi.status() == WL_CONNECTED) {
    return;
  }

  if (nowMs - lastWifiAttemptMs_ < kWifiRetryMs) {
    return;
  }
  lastWifiAttemptMs_ = nowMs;

  Serial.print("AppBridge: conectando WiFi ");
  Serial.println(APP_WIFI_SSID);
  WiFi.begin(APP_WIFI_SSID, APP_WIFI_PASSWORD);
}

void AppBridge::ensureMqtt(uint32_t nowMs) {
  if (!canRun_ || WiFi.status() != WL_CONNECTED || mqttClient_.connected()) {
    return;
  }

  if (nowMs - lastMqttAttemptMs_ < kMqttRetryMs) {
    return;
  }
  lastMqttAttemptMs_ = nowMs;

  bool connected = false;
  if (hasValue(APP_MQTT_USERNAME)) {
    connected = mqttClient_.connect(
        clientId_,
        APP_MQTT_USERNAME,
        APP_MQTT_PASSWORD,
        topicStatus_,
        0,
        true,
        "offline");
  } else {
    connected = mqttClient_.connect(
        clientId_,
        topicStatus_,
        0,
        true,
        "offline");
  }

  if (connected) {
    mqttClient_.publish(topicStatus_, "online", true);
    Serial.println("AppBridge: MQTT conectado.");
  } else {
    Serial.print("AppBridge: MQTT fallo, estado=");
    Serial.println(mqttClient_.state());
  }
}

void AppBridge::update(uint32_t nowMs) {
  if (!canRun_) {
    return;
  }

  ensureWifi(nowMs);
  ensureMqtt(nowMs);

  if (mqttClient_.connected()) {
    mqttClient_.loop();
  }
}

void AppBridge::publishTelemetry(const AppTelemetry& telemetry, uint32_t nowMs) {
  if (!mqttReady()) {
    return;
  }

  if (nowMs - lastTelemetryPublishMs_ < kTelemetryPublishMs) {
    return;
  }
  lastTelemetryPublishMs_ = nowMs;

  char payload[320] = {0};
  snprintf(
      payload,
      sizeof(payload),
      "{\"device\":\"%s\",\"uptime_ms\":%lu,\"line1_ac\":%s,\"pzem_valid\":%s,\"pzem_v\":%.2f,\"pzem_a\":%.3f,\"pzem_w\":%.1f,\"bat_v\":%.2f,\"bat_a\":%.2f,\"bat_cap\":%u}",
      APP_DEVICE_ID,
      static_cast<unsigned long>(nowMs),
      telemetry.line1AcPresent ? "true" : "false",
      telemetry.pzemValid ? "true" : "false",
      telemetry.pzemVoltage,
      telemetry.pzemCurrent,
      telemetry.pzemPower,
      telemetry.batteryVoltage,
      telemetry.batteryCurrent,
      telemetry.batteryCapacityPercent);

  mqttClient_.publish(topicTelemetry_, payload, true);
}

void AppBridge::publishAlert(const char* eventCode, uint32_t nowMs) {
  if (!mqttReady() || !hasValue(eventCode)) {
    return;
  }

  char payload[196] = {0};
  snprintf(
      payload,
      sizeof(payload),
      "{\"device\":\"%s\",\"uptime_ms\":%lu,\"event\":\"%s\"}",
      APP_DEVICE_ID,
      static_cast<unsigned long>(nowMs),
      eventCode);

  mqttClient_.publish(topicAlert_, payload, false);
}

bool AppBridge::enabled() {
  return canRun_;
}

bool AppBridge::mqttConnected() {
  return canRun_ && mqttClient_.connected();
}

bool AppBridge::mqttReady() {
  return canRun_ && (WiFi.status() == WL_CONNECTED) && mqttClient_.connected();
}
