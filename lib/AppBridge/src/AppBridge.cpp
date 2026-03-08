#include <AppBridge.h>

#include <string.h>

#include "AppBridgeConfig.h"

namespace {
constexpr uint32_t kWifiRetryMs = 7000;
constexpr uint32_t kMqttRetryMs = 5000;
constexpr uint32_t kTelemetryPublishMs = 5000;
constexpr uint16_t kMqttBufferSize = 2048;
constexpr size_t kDiscoveryPayloadSize = 1900;

const char* kTplPzemVoltage = "{% if value_json.pzem_valid %}{{ value_json.pzem_v }}{% else %}unknown{% endif %}";
const char* kTplPzemCurrent = "{% if value_json.pzem_valid %}{{ value_json.pzem_a }}{% else %}unknown{% endif %}";
const char* kTplPzemPower = "{% if value_json.pzem_valid %}{{ value_json.pzem_w }}{% else %}unknown{% endif %}";
const char* kTplBatteryVoltage = "{{ value_json.bat_v }}";
const char* kTplBatteryCurrent = "{{ value_json.bat_a }}";
const char* kTplBatteryCapacity = "{{ value_json.bat_cap }}";
const char* kTplLine1 = "{{ 'ON' if value_json.line1_ac else 'OFF' }}";
const char* kTplLastEvent = "{{ value_json.event }}";

bool hasValue(const char* text) {
  return text && text[0] != '\0';
}
}  // namespace

AppBridge* AppBridge::instance_ = nullptr;

AppBridge::AppBridge() : mqttClient_(wifiClient_) {
  instance_ = this;
}

void AppBridge::mqttCallbackRouter(char* topic, uint8_t* payload, unsigned int length) {
  if (instance_ != nullptr) {
    instance_->handleMqttMessage(topic, payload, length);
  }
}

void AppBridge::begin() {
  snprintf(wifiSsid_, sizeof(wifiSsid_), "%s", APP_WIFI_SSID);
  snprintf(wifiPassword_, sizeof(wifiPassword_), "%s", APP_WIFI_PASSWORD);

#if APP_WIFI_ALLOW_SERIAL_INPUT
  if (!hasValue(wifiSsid_)) {
    promptWifiCredentialsFromSerial(APP_WIFI_SERIAL_TIMEOUT_MS);
  }
#endif

  canRun_ = hasValue(wifiSsid_) && hasValue(APP_MQTT_HOST);
  if (!canRun_) {
    Serial.println("AppBridge: deshabilitado (faltan credenciales WiFi/MQTT).");
    if (!hasValue(wifiSsid_)) {
      Serial.println("AppBridge: SSID vacio.");
    }
    if (!hasValue(APP_MQTT_HOST)) {
      Serial.println("AppBridge: MQTT host vacio.");
    }
    return;
  }

  const uint32_t chipId = static_cast<uint32_t>(ESP.getEfuseMac());
  snprintf(clientId_, sizeof(clientId_), "%s-%08lX", APP_DEVICE_ID, static_cast<unsigned long>(chipId));
  snprintf(topicStatus_, sizeof(topicStatus_), "%s/status", APP_MQTT_TOPIC_BASE);
  snprintf(topicTelemetry_, sizeof(topicTelemetry_), "%s/telemetry", APP_MQTT_TOPIC_BASE);
  snprintf(topicAlert_, sizeof(topicAlert_), "%s/alert", APP_MQTT_TOPIC_BASE);
  snprintf(topicHaStatus_, sizeof(topicHaStatus_), "%s/status", APP_HA_DISCOVERY_PREFIX);
  snprintf(
      topicDiscoveryConfig_,
      sizeof(topicDiscoveryConfig_),
      "%s/device/%s/config",
      APP_HA_DISCOVERY_PREFIX,
      APP_DEVICE_ID);

  discoveryPublished_ = false;
  wifiConnectedLogged_ = false;
  WiFi.mode(WIFI_STA);
  mqttClient_.setServer(APP_MQTT_HOST, APP_MQTT_PORT);
  mqttClient_.setCallback(AppBridge::mqttCallbackRouter);
  if (!mqttClient_.setBufferSize(kMqttBufferSize)) {
    Serial.println("AppBridge: no se pudo ampliar buffer MQTT, discovery puede fallar.");
  }
}

void AppBridge::ensureWifi(uint32_t nowMs) {
  if (!canRun_) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnectedLogged_) {
      const String connectedSsid = WiFi.SSID();
      const String ipText = WiFi.localIP().toString();
      Serial.print("AppBridge: WiFi conectado OK. SSID=");
      Serial.print(connectedSsid);
      Serial.print(" IP=");
      Serial.print(ipText);
      Serial.print(" RSSI=");
      Serial.println(WiFi.RSSI());

      if (connectedSsid != String(wifiSsid_)) {
        Serial.print("AppBridge: advertencia, SSID conectado distinto al configurado: ");
        Serial.println(wifiSsid_);
      }
      wifiConnectedLogged_ = true;
    }
    return;
  }

  if (wifiConnectedLogged_) {
    wifiConnectedLogged_ = false;
    Serial.println("AppBridge: WiFi desconectado, reintentando...");
  }

  if (hasValue(wifiSsid_) == false) {
    Serial.println("AppBridge: SSID no definido, no se puede reconectar WiFi.");
    return;
  }

  if (nowMs - lastWifiAttemptMs_ < kWifiRetryMs) {
    return;
  }
  lastWifiAttemptMs_ = nowMs;

  Serial.print("AppBridge: conectando WiFi ");
  Serial.println(wifiSsid_);
  WiFi.begin(wifiSsid_, wifiPassword_);
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
    onMqttConnected(nowMs);
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

void AppBridge::onMqttConnected(uint32_t nowMs) {
  discoveryPublished_ = false;
  mqttClient_.publish(topicStatus_, "online", true);
  subscribeHaStatus();
  publishDiscovery(nowMs);
}

void AppBridge::publishDiscovery(uint32_t nowMs) {
  (void)nowMs;
#if !APP_HA_ENABLE_DISCOVERY
  return;
#endif
  if (!mqttReady() || discoveryPublished_) {
    return;
  }

  char payload[kDiscoveryPayloadSize] = {0};
  const int written = snprintf(
      payload,
      sizeof(payload),
      "{\"dev\":{\"ids\":[\"%s\"],\"name\":\"%s\",\"mdl\":\"%s\",\"mf\":\"%s\",\"sw\":\"%s\"},"
      "\"o\":{\"name\":\"power_light_appbridge\",\"sw\":\"%s\"},"
      "\"availability\":[{\"topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"}],"
      "\"state_topic\":\"%s\","
      "\"cmps\":{"
      "\"pzem_v\":{\"p\":\"sensor\",\"name\":\"Voltaje AC\",\"unique_id\":\"%s_pzem_v\",\"value_template\":\"%s\",\"unit_of_measurement\":\"V\",\"device_class\":\"voltage\",\"state_class\":\"measurement\"},"
      "\"pzem_a\":{\"p\":\"sensor\",\"name\":\"Corriente AC\",\"unique_id\":\"%s_pzem_a\",\"value_template\":\"%s\",\"unit_of_measurement\":\"A\",\"device_class\":\"current\",\"state_class\":\"measurement\"},"
      "\"pzem_w\":{\"p\":\"sensor\",\"name\":\"Potencia AC\",\"unique_id\":\"%s_pzem_w\",\"value_template\":\"%s\",\"unit_of_measurement\":\"W\",\"device_class\":\"power\",\"state_class\":\"measurement\"},"
      "\"bat_v\":{\"p\":\"sensor\",\"name\":\"Voltaje Bateria\",\"unique_id\":\"%s_bat_v\",\"value_template\":\"%s\",\"unit_of_measurement\":\"V\",\"device_class\":\"voltage\",\"state_class\":\"measurement\"},"
      "\"bat_a\":{\"p\":\"sensor\",\"name\":\"Corriente Bateria\",\"unique_id\":\"%s_bat_a\",\"value_template\":\"%s\",\"unit_of_measurement\":\"A\",\"device_class\":\"current\",\"state_class\":\"measurement\"},"
      "\"bat_cap\":{\"p\":\"sensor\",\"name\":\"Capacidad Bateria\",\"unique_id\":\"%s_bat_cap\",\"value_template\":\"%s\",\"unit_of_measurement\":\"%%\",\"device_class\":\"battery\",\"state_class\":\"measurement\"},"
      "\"line1_ac\":{\"p\":\"binary_sensor\",\"name\":\"Linea 1\",\"unique_id\":\"%s_line1_ac\",\"value_template\":\"%s\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device_class\":\"power\"},"
      "\"last_event\":{\"p\":\"sensor\",\"name\":\"Evento\",\"unique_id\":\"%s_last_event\",\"state_topic\":\"%s\",\"value_template\":\"%s\"}"
      "}}",
      APP_DEVICE_ID,
      APP_HA_DEVICE_NAME,
      APP_HA_DEVICE_MODEL,
      APP_HA_DEVICE_MANUFACTURER,
      APP_HA_SW_VERSION,
      APP_HA_SW_VERSION,
      topicStatus_,
      topicTelemetry_,
      APP_DEVICE_ID,
      kTplPzemVoltage,
      APP_DEVICE_ID,
      kTplPzemCurrent,
      APP_DEVICE_ID,
      kTplPzemPower,
      APP_DEVICE_ID,
      kTplBatteryVoltage,
      APP_DEVICE_ID,
      kTplBatteryCurrent,
      APP_DEVICE_ID,
      kTplBatteryCapacity,
      APP_DEVICE_ID,
      kTplLine1,
      APP_DEVICE_ID,
      topicAlert_,
      kTplLastEvent);

  if (written <= 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    Serial.println("AppBridge: payload discovery truncado, aumenta buffer.");
    return;
  }

  if (mqttClient_.publish(topicDiscoveryConfig_, payload, true)) {
    discoveryPublished_ = true;
    Serial.println("AppBridge: discovery publicado.");
  } else {
    Serial.println("AppBridge: error publicando discovery.");
  }
}

void AppBridge::handleMqttMessage(char* topic, uint8_t* payload, unsigned int length) {
  if (!hasValue(topic) || payload == nullptr || !mqttClient_.connected()) {
    return;
  }
#if !APP_HA_ENABLE_DISCOVERY
  return;
#endif

  if (strcmp(topic, topicHaStatus_) != 0) {
    return;
  }

  char message[16] = {0};
  const unsigned int copyLen = (length < (sizeof(message) - 1)) ? length : (sizeof(message) - 1);
  if (copyLen > 0) {
    memcpy(message, payload, copyLen);
    message[copyLen] = '\0';
  }

  if (strcmp(message, "online") == 0) {
    discoveryPublished_ = false;
    publishDiscovery(millis());
  }
}

void AppBridge::subscribeHaStatus() {
#if !APP_HA_ENABLE_DISCOVERY
  return;
#endif
  if (!mqttClient_.subscribe(topicHaStatus_)) {
    Serial.println("AppBridge: no se pudo suscribir a homeassistant/status.");
  }
}

bool AppBridge::promptWifiCredentialsFromSerial(uint32_t timeoutMs) {
  if (timeoutMs == 0) {
    return false;
  }

  Serial.println("AppBridge: ingresa credenciales por Serial en formato WIFI:ssid,password");
  Serial.print("AppBridge: esperando ");
  Serial.print(static_cast<unsigned long>(timeoutMs));
  Serial.println(" ms...");

  char line[192] = {0};
  size_t len = 0;
  const uint32_t startMs = millis();

  while (static_cast<uint32_t>(millis() - startMs) < timeoutMs) {
    while (Serial.available() > 0) {
      const char c = static_cast<char>(Serial.read());
      if (c == '\r' || c == '\n') {
        if (len == 0) {
          continue;
        }
        line[len] = '\0';
        if (strncmp(line, "WIFI:", 5) == 0) {
          char* creds = line + 5;
          char* comma = strchr(creds, ',');
          if (comma != nullptr) {
            *comma = '\0';
            const char* ssid = creds;
            const char* pass = comma + 1;
            if (hasValue(ssid)) {
              snprintf(wifiSsid_, sizeof(wifiSsid_), "%s", ssid);
              snprintf(wifiPassword_, sizeof(wifiPassword_), "%s", pass);
              Serial.print("AppBridge: SSID recibido por Serial: ");
              Serial.println(wifiSsid_);
              return true;
            }
          }
        }
        Serial.println("AppBridge: formato invalido. Usa WIFI:ssid,password");
        len = 0;
        continue;
      }

      if (len < (sizeof(line) - 1)) {
        line[len++] = c;
      }
    }
    delay(10);
  }

  Serial.println("AppBridge: timeout de Serial, no se recibieron credenciales WiFi.");
  return false;
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
