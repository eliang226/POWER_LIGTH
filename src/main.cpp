#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <PZEM004Tv30.h>
#include <ctype.h>
#include <HTTPClient.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <Preferences.h>
#include <RTClib.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <liquidcrystal_i2c.h>
#include <AppBridge.h>
#include <AppBridgeConfig.h>
#include <BatteryMonitor.h>
#include <CurrentHallMonitor.h>
#include <LedStatus.h>

constexpr uint8_t kNeoPixelPin = D3;
constexpr uint8_t kNumPixels = 1;
constexpr uint8_t kPzemRxPin = D7;
constexpr uint8_t kPzemTxPin = D6;
constexpr uint8_t kBatteryAdcPin = D2;
constexpr uint8_t kHallAdcPin = D1;
constexpr uint8_t kBuzzerPin = D10;
constexpr uint8_t kLine1AcPin = A0;  // H11AA: LOW means AC present
constexpr uint8_t kLCD_I2cAddress = 0x27;
constexpr uint32_t kPzemReadIntervalMs = 2500;
constexpr uint32_t kPzemDataTimeoutMs = 10000;
constexpr uint32_t kTelemetryPrintIntervalMs = 5000;
constexpr uint32_t kLcdRotateIntervalMs = 5000;
constexpr uint32_t kLine1AlertHoldMs = 7000;
constexpr uint32_t kLine1AcEvalWindowMs = 250;
constexpr uint16_t kLine1AcMinEdges = 6;
constexpr uint16_t kLine1AcMinLowSamples = 3;
constexpr float kMinHallCalibrationCurrentA = 1.0f;
constexpr float kAcs758ZeroCurrentV = 2.5f;
constexpr float kAcs758SensitivityVPerA = 0.040f;
constexpr float kAcs758FullScaleCurrentA = 50.0f;
constexpr float kDefaultHallCurrentDirection = 1.0f;
constexpr float kDefaultHallDeadbandA = 0.10f;
constexpr float kMaxHallDeadbandA = 5.0f;
constexpr float kCriticalLowBatteryV = 10.5f;
constexpr uint16_t kLowBatteryBeepOnMs = 80;
constexpr uint16_t kLowBatteryBeepOffMs = 80;
constexpr uint32_t kTelegramPollIntervalMs = 5000;
constexpr uint32_t kTelegramLowBatteryRepeatMs = 900000;
constexpr uint32_t kTelegramHttpTimeoutMs = 7000;
constexpr uint32_t kPzemFaultStartupGraceMs = 15000;
constexpr size_t kSerialCmdBufferSize = 96;
constexpr char kConfigNamespace[] = "pl_config";
constexpr char kKeyRestoreHour[] = "rest_hour";
constexpr char kKeyBatteryScale[] = "bat_scale";
constexpr char kKeyBatteryOffset[] = "bat_offs";
constexpr char kKeyHallDirection[] = "hall_dir";
constexpr char kKeyHallDeadband[] = "hall_dead";
constexpr char kKeyHallZeroVoltage[] = "hall_zero";
constexpr char kKeyHallGain[] = "hall_gain";
constexpr uint8_t kRestoreHourAny = 255;

struct PzemData {
  float voltage = NAN;
  float current = NAN;
  float power = NAN;
  float energy = NAN;
  float frequency = NAN;
  float pf = NAN;
  bool valid = false;
};

enum class Line1AlertType : uint8_t {
  None = 0,
  PowerLost = 1,
  PowerRestored = 2
};

enum class BatteryFlowState : uint8_t {
  Idle = 0,
  Charging = 1,
  Discharging = 2
};

Adafruit_NeoPixel pixels(kNumPixels, kNeoPixelPin, NEO_GRB + NEO_KHZ800);
HardwareSerial pzemSerial(1);
PZEM004Tv30 pzem(pzemSerial, kPzemRxPin, kPzemTxPin);
BatteryMonitor batteryMonitor(kBatteryAdcPin);
HallMonitorConfig makeHallConfig();
CurrentHallMonitor hallCurrentMonitor(makeHallConfig());
LedStatus ledStatus(pixels);
LiquidCrystal_I2C lcd(kLCD_I2cAddress, 16, 2);
AppBridge appBridge;
RTC_DS1307 rtc;
Preferences preferences;
WiFiClientSecure telegramClient;

uint32_t gLastPzemReadMs = 0;
uint32_t gLastPzemValidMs = 0;
uint32_t gLastTelemetryPrintMs = 0;
uint32_t gLastLcdRotateMs = 0;
uint32_t gLine1WindowStartMs = 0;
PzemData gLastPzemData;
bool gHasPzemSample = false;
bool gLcdRefreshRequested = true;
bool gLine1AcPresent = false;
bool gLine1AcInitialized = false;
bool gAlertScreenVisible = false;
uint8_t gLcdScreenIndex = 0;
char gSerialCommand[kSerialCmdBufferSize] = {0};
size_t gSerialCommandLen = 0;
volatile uint16_t gLine1EdgeCountIsr = 0;
uint16_t gLine1LowSamples = 0;
uint32_t gLine1AlertUntilMs = 0;
Line1AlertType gLine1AlertType = Line1AlertType::None;
bool gRtcReady = false;
uint8_t gRestoreAlertHour = kRestoreHourAny;
float gHallCurrentDirection = kDefaultHallCurrentDirection;
float gHallDeadbandA = kDefaultHallDeadbandA;
bool gLowBatteryCriticalActive = false;
bool gLowBatteryBeepState = false;
uint32_t gLowBatteryBeepLastToggleMs = 0;
uint32_t gLastTelegramPollMs = 0;
uint32_t gLastLowBatteryTelegramMs = 0;
int32_t gTelegramLastUpdateId = 0;
bool gPzemFaultActive = false;
bool gTelegramStartupSent = false;
bool gTelegramConfigWarned = false;

void processConsoleCommand(const char* rawCmd);
void onMqttConsoleCommand(const char* command);
bool isPzemDataFresh(uint32_t nowMs);
uint8_t estimateBatteryCapacityPercent(float batteryVoltage);
bool buildStatusMessage(char* buffer, size_t bufferSize, uint32_t nowMs);
bool telegramEnabled();
bool telegramSendMessage(const char* text);
void pollTelegramCommands(uint32_t nowMs);
void updateLowBatteryAlarm(uint32_t nowMs);
void updatePzemHealthAlert(uint32_t nowMs);
void maybeSendStartupTelegram(uint32_t nowMs);
void printBatterySnapshot();
void printHallSnapshot();

void IRAM_ATTR isrLine1Ac() {
  if (gLine1EdgeCountIsr < UINT16_MAX) {
    gLine1EdgeCountIsr = static_cast<uint16_t>(gLine1EdgeCountIsr + 1);
  }
}

bool isValidRestoreHour(uint8_t hour) {
  return hour <= 23 || hour == kRestoreHourAny;
}

bool isValidHallDirection(float direction) {
  return direction == 1.0f || direction == -1.0f;
}

bool isValidBatteryCalibrationScale(float scale) {
  return isfinite(scale) && scale >= 0.5f && scale <= 1.5f;
}

bool isValidBatteryCalibrationOffset(float offsetV) {
  return isfinite(offsetV) && offsetV >= -2.0f && offsetV <= 2.0f;
}

bool isValidHallDeadband(float deadbandA) {
  return isfinite(deadbandA) && deadbandA >= 0.0f && deadbandA <= kMaxHallDeadbandA;
}

bool isValidHallZeroVoltage(float zeroVoltage) {
  return isfinite(zeroVoltage) && zeroVoltage >= 0.0f && zeroVoltage <= 5.0f;
}

bool isValidHallGain(float gain) {
  return isfinite(gain) && gain >= 0.05f && gain <= 10.0f;
}

void loadRuntimeConfig() {
  if (!preferences.begin(kConfigNamespace, false)) {
    Serial.println("Config: no se pudo abrir NVS.");
    gRestoreAlertHour = kRestoreHourAny;
    gHallCurrentDirection = kDefaultHallCurrentDirection;
    gHallDeadbandA = kDefaultHallDeadbandA;
    return;
  }

  BatteryCalibration batteryCalibration = batteryMonitor.calibration();
  const float storedBatteryScale =
      preferences.getFloat(kKeyBatteryScale, batteryCalibration.scale);
  if (isValidBatteryCalibrationScale(storedBatteryScale)) {
    batteryCalibration.scale = storedBatteryScale;
  }
  const float storedBatteryOffset =
      preferences.getFloat(kKeyBatteryOffset, batteryCalibration.offsetV);
  if (isValidBatteryCalibrationOffset(storedBatteryOffset)) {
    batteryCalibration.offsetV = storedBatteryOffset;
  }
  batteryMonitor.setCalibration(batteryCalibration);

  const uint8_t storedHour = preferences.getUChar(kKeyRestoreHour, kRestoreHourAny);
  gRestoreAlertHour = isValidRestoreHour(storedHour) ? storedHour : kRestoreHourAny;
  const float storedDirection = preferences.getFloat(kKeyHallDirection, kDefaultHallCurrentDirection);
  gHallCurrentDirection = isValidHallDirection(storedDirection) ? storedDirection : kDefaultHallCurrentDirection;
  const float storedDeadband = preferences.getFloat(kKeyHallDeadband, kDefaultHallDeadbandA);
  gHallDeadbandA = isValidHallDeadband(storedDeadband) ? storedDeadband : kDefaultHallDeadbandA;
  HallCalibration hallCalibration = hallCurrentMonitor.calibration();
  const float storedHallZero =
      preferences.getFloat(kKeyHallZeroVoltage, hallCalibration.zeroCurrentVoltage);
  if (isValidHallZeroVoltage(storedHallZero)) {
    hallCalibration.zeroCurrentVoltage = storedHallZero;
  }
  const float storedHallGain =
      preferences.getFloat(kKeyHallGain, hallCalibration.currentGain);
  if (isValidHallGain(storedHallGain)) {
    hallCalibration.currentGain = storedHallGain;
  }
  hallCurrentMonitor.setCalibration(hallCalibration);

  Serial.print("Config: BAT scale=");
  Serial.print(batteryCalibration.scale, 4);
  Serial.print(" offset=");
  Serial.print(batteryCalibration.offsetV, 3);
  Serial.println("V");

  Serial.print("Config: alerta retorno energia = ");
  if (gRestoreAlertHour == kRestoreHourAny) {
    Serial.println("SIEMPRE");
  } else {
    Serial.print("HORA ");
    Serial.println(gRestoreAlertHour);
  }

  Serial.print("Config: HALL dir=");
  Serial.print(gHallCurrentDirection > 0.0f ? "+1" : "-1");
  Serial.print(" deadband=");
  Serial.print(gHallDeadbandA, 2);
  Serial.print("A zero=");
  Serial.print(hallCalibration.zeroCurrentVoltage, 3);
  Serial.print("V gain=");
  Serial.println(hallCalibration.currentGain, 4);
}

void saveBatteryCalibrationConfig() {
  const BatteryCalibration calibration = batteryMonitor.calibration();
  preferences.putFloat(kKeyBatteryScale, calibration.scale);
  preferences.putFloat(kKeyBatteryOffset, calibration.offsetV);
  Serial.println("Config: BAT guardado en NVS.");
}

void saveRestoreAlertHour() {
  preferences.putUChar(kKeyRestoreHour, gRestoreAlertHour);
  Serial.println("Config: guardado en NVS.");
}

void saveHallCurrentConfig() {
  preferences.putFloat(kKeyHallDirection, gHallCurrentDirection);
  preferences.putFloat(kKeyHallDeadband, gHallDeadbandA);
  Serial.println("Config: HALL guardado en NVS.");
}

void saveHallSensorCalibration() {
  const HallCalibration calibration = hallCurrentMonitor.calibration();
  preferences.putFloat(kKeyHallZeroVoltage, calibration.zeroCurrentVoltage);
  preferences.putFloat(kKeyHallGain, calibration.currentGain);
  Serial.println("Config: calibracion HALL guardada en NVS.");
}

void initRtc() {
  Wire.begin();
  if (!rtc.begin()) {
    gRtcReady = false;
    Serial.println("RTC: DS1307 no detectado.");
    return;
  }

  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("RTC: ajustado con fecha/hora de compilacion.");
  }

  gRtcReady = true;
  const DateTime now = rtc.now();
  Serial.printf("RTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
}

void playBuzzerPattern(uint8_t pulses, uint16_t onMs, uint16_t offMs) {
  if (pulses == 0) {
    return;
  }

  for (uint8_t i = 0; i < pulses; ++i) {
    digitalWrite(kBuzzerPin, HIGH);
    delay(onMs);
    digitalWrite(kBuzzerPin, LOW);
    if ((i + 1) < pulses) {
      delay(offMs);
    }
  }
}

bool shouldPlayRestoreAlertNow() {
  if (gRestoreAlertHour == kRestoreHourAny) {
    return true;
  }

  if (!gRtcReady) {
    Serial.println("RTC: no disponible, alerta retorno sonara igualmente.");
    return true;
  }

  const DateTime now = rtc.now();
  const bool shouldPlay = (now.hour() == gRestoreAlertHour);
  if (!shouldPlay) {
    Serial.printf("RTC: retorno fuera de hora configurada (%02u != %02u), sin buzzer.\n",
                  now.hour(), gRestoreAlertHour);
  }
  return shouldPlay;
}

void playPowerLostAlert() {
  playBuzzerPattern(4, 1000, 500);
}

void playPowerRestoredAlert() {
  if (shouldPlayRestoreAlertNow()) {
    playBuzzerPattern(2, 1000, 500);
  }
}

HallMonitorConfig makeHallConfig() {
  HallMonitorConfig config;
  config.adcPin = kHallAdcPin;
  config.resistorTopOhm = 3900.0f;              // R1 ajustado para limitar a ~3.24V max
  config.resistorBottomOhm = 10000.0f;          // R2 a GND
  config.sensorCenterV = kAcs758ZeroCurrentV;   // ACS758 zero-current output
  config.sensorSensitivityVPerA = kAcs758SensitivityVPerA;
  config.sensorSpanV = 0.0f;                    // No usar calculo generico
  config.fullScaleCurrentA = kAcs758FullScaleCurrentA;
  config.bidirectional = true;
  config.samplesPerRead = 30;                   // Similar al smoothing del ejemplo DFRobot
  config.settleUs = 150;
  config.emaAlpha = 0.18f;
  return config;
}

float correctedHallCurrentA(const HallCurrentData& current) {
  return current.filteredCurrentA * gHallCurrentDirection;
}

float normalizedBatteryCurrentA(const HallCurrentData& current) {
  const float correctedA = correctedHallCurrentA(current);
  if (fabsf(correctedA) < gHallDeadbandA) {
    return 0.0f;
  }
  return correctedA;
}

BatteryFlowState batteryFlowState(float currentA) {
  if (currentA > 0.0f) {
    return BatteryFlowState::Charging;
  }
  if (currentA < 0.0f) {
    return BatteryFlowState::Discharging;
  }
  return BatteryFlowState::Idle;
}

const char* batteryFlowText(BatteryFlowState state) {
  switch (state) {
    case BatteryFlowState::Charging:
      return "CHG";
    case BatteryFlowState::Discharging:
      return "DIS";
    default:
      return "IDLE";
  }
}

bool appendToBuffer(char* buffer, size_t bufferSize, size_t* used, const char* format, ...) {
  if (!buffer || !used || *used >= bufferSize) {
    return false;
  }

  va_list args;
  va_start(args, format);
  const int written = vsnprintf(buffer + *used, bufferSize - *used, format, args);
  va_end(args);
  if (written < 0) {
    return false;
  }

  const size_t writtenSize = static_cast<size_t>(written);
  if (writtenSize >= (bufferSize - *used)) {
    *used = bufferSize - 1;
    buffer[*used] = '\0';
    return false;
  }

  *used += writtenSize;
  return true;
}

bool urlEncode(const char* value, char* encoded, size_t encodedSize) {
  if (!value || !encoded || encodedSize == 0) {
    return false;
  }

  static const char kHex[] = "0123456789ABCDEF";
  size_t out = 0;

  for (size_t i = 0; value[i] != '\0'; ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      if ((out + 1) >= encodedSize) {
        encoded[0] = '\0';
        return false;
      }
      encoded[out++] = static_cast<char>(c);
      continue;
    }

    if ((out + 3) >= encodedSize) {
      encoded[0] = '\0';
      return false;
    }
    encoded[out++] = '%';
    encoded[out++] = kHex[(c >> 4) & 0x0F];
    encoded[out++] = kHex[c & 0x0F];
  }

  encoded[out] = '\0';
  return true;
}

bool telegramEnabled() {
#if APP_TELEGRAM_ENABLE
  const bool configured = (APP_TELEGRAM_BOT_TOKEN[0] != '\0') && (APP_TELEGRAM_CHAT_ID[0] != '\0');
  if (!configured && !gTelegramConfigWarned) {
    Serial.println("Telegram: deshabilitado (faltan APP_TELEGRAM_BOT_TOKEN/APP_TELEGRAM_CHAT_ID).");
    gTelegramConfigWarned = true;
  }
  return configured;
#else
  return false;
#endif
}

bool telegramSendMessage(const char* text) {
  if (!telegramEnabled() || !text || text[0] == '\0') {
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  char url[192] = {0};
  snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/sendMessage", APP_TELEGRAM_BOT_TOKEN);
  if (!http.begin(telegramClient, url)) {
    Serial.println("Telegram: no se pudo abrir HTTPS para sendMessage.");
    return false;
  }

  http.setTimeout(kTelegramHttpTimeoutMs);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  char encodedText[768] = {0};
  char payload[960] = {0};
  if (!urlEncode(text, encodedText, sizeof(encodedText))) {
    Serial.println("Telegram: mensaje demasiado largo para urlencode.");
    http.end();
    return false;
  }

  const int payloadLen = snprintf(
      payload,
      sizeof(payload),
      "chat_id=%s&text=%s",
      APP_TELEGRAM_CHAT_ID,
      encodedText);
  if (payloadLen <= 0 || static_cast<size_t>(payloadLen) >= sizeof(payload)) {
    Serial.println("Telegram: payload demasiado largo.");
    http.end();
    return false;
  }

  const int httpCode = http.POST(reinterpret_cast<uint8_t*>(payload), static_cast<size_t>(payloadLen));
  const bool ok = (httpCode == HTTP_CODE_OK);
  if (!ok) {
    Serial.print("Telegram: sendMessage fallo HTTP=");
    Serial.println(httpCode);
  }
  http.end();
  return ok;
}

bool buildStatusMessage(char* buffer, size_t bufferSize, uint32_t nowMs) {
  if (!buffer || bufferSize == 0) {
    return false;
  }

  const BatteryData& battery = batteryMonitor.data();
  const HallCurrentData& hallCurrent = hallCurrentMonitor.data();
  const float batteryCurrentA = normalizedBatteryCurrentA(hallCurrent);
  uint8_t capPercent = estimateBatteryCapacityPercent(battery.filteredBatteryVoltage);
  if (capPercent > 99) {
    capPercent = 99;
  }

  size_t used = 0;
  bool ok = appendToBuffer(
      buffer,
      bufferSize,
      &used,
      "POWER LIGHT V1\nAC: %s | MQTT: %s\nVBat: %.2fV  IBat: %.2fA\nCapacidad: %u%%",
      gLine1AcPresent ? "LINE" : "OUT",
      appBridge.mqttConnected() ? "OK" : "OFF",
      battery.filteredBatteryVoltage,
      batteryCurrentA,
      capPercent);

  if (isPzemDataFresh(nowMs)) {
    ok = appendToBuffer(
             buffer,
             bufferSize,
             &used,
             "\nAC V:%.1f A:%.2f W:%.0f",
             gLastPzemData.voltage,
             gLastPzemData.current,
             gLastPzemData.power) && ok;
  } else {
    ok = appendToBuffer(buffer, bufferSize, &used, "\nPZEM: sin datos validos") && ok;
  }

  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    ok = appendToBuffer(
             buffer,
             bufferSize,
             &used,
             "\nWiFi IP: %u.%u.%u.%u",
             ip[0],
             ip[1],
             ip[2],
             ip[3]) && ok;
  } else {
    ok = appendToBuffer(buffer, bufferSize, &used, "\nWiFi: desconectado") && ok;
  }

  return ok;
}

void handleTelegramCommand(const char* text, uint32_t nowMs) {
  if (!text || text[0] == '\0') {
    return;
  }

  const char* cursor = text;
  while (*cursor != '\0' && isspace(static_cast<unsigned char>(*cursor))) {
    ++cursor;
  }

  char token[40] = {0};
  size_t idx = 0;
  while (*cursor != '\0' && !isspace(static_cast<unsigned char>(*cursor)) && idx < (sizeof(token) - 1)) {
    token[idx++] = *cursor++;
  }
  token[idx] = '\0';

  if (token[0] == '\0') {
    return;
  }
  if (token[0] == '/') {
    memmove(token, token + 1, strlen(token));
  }
  char* atPos = strchr(token, '@');
  if (atPos != nullptr) {
    *atPos = '\0';
  }
  for (size_t i = 0; token[i] != '\0'; ++i) {
    token[i] = static_cast<char>(toupper(static_cast<unsigned char>(token[i])));
  }

  if (strcmp(token, "ESTADO") == 0 || strcmp(token, "STATUS") == 0) {
    char statusMessage[320] = {0};
    buildStatusMessage(statusMessage, sizeof(statusMessage), nowMs);
    telegramSendMessage(statusMessage);
    return;
  }

  if (strcmp(token, "HELP") == 0 || strcmp(token, "AYUDA") == 0 || strcmp(token, "START") == 0) {
    telegramSendMessage("Comandos disponibles:\n- estado\n- help");
    return;
  }

  telegramSendMessage("Comando no reconocido. Usa: estado");
}

void pollTelegramCommands(uint32_t nowMs) {
  if (!telegramEnabled()) {
    return;
  }
  if (nowMs - gLastTelegramPollMs < kTelegramPollIntervalMs) {
    return;
  }
  gLastTelegramPollMs = nowMs;

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  char url[256] = {0};
  if (gTelegramLastUpdateId > 0) {
    snprintf(
        url,
        sizeof(url),
        "https://api.telegram.org/bot%s/getUpdates?timeout=0&limit=5&allowed_updates=%%5B%%22message%%22%%5D&offset=%ld",
        APP_TELEGRAM_BOT_TOKEN,
        static_cast<long>(gTelegramLastUpdateId + 1));
  } else {
    snprintf(
        url,
        sizeof(url),
        "https://api.telegram.org/bot%s/getUpdates?timeout=0&limit=5&allowed_updates=%%5B%%22message%%22%%5D",
        APP_TELEGRAM_BOT_TOKEN);
  }

  HTTPClient http;
  if (!http.begin(telegramClient, url)) {
    Serial.println("Telegram: no se pudo abrir HTTPS para getUpdates.");
    return;
  }
  http.setTimeout(kTelegramHttpTimeoutMs);
  const int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    if (httpCode > 0) {
      Serial.print("Telegram: getUpdates HTTP=");
      Serial.println(httpCode);
    }
    http.end();
    return;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.print("Telegram: JSON invalido en getUpdates: ");
    Serial.println(err.c_str());
    return;
  }

  JsonArray updates = doc["result"].as<JsonArray>();
  if (updates.isNull()) {
    return;
  }

  const int64_t expectedChatId = strtoll(APP_TELEGRAM_CHAT_ID, nullptr, 10);
  for (JsonObject update : updates) {
    const int32_t updateId = update["update_id"] | 0;
    if (updateId > gTelegramLastUpdateId) {
      gTelegramLastUpdateId = updateId;
    }

    JsonObject message = update["message"].as<JsonObject>();
    if (message.isNull()) {
      continue;
    }
    const int64_t chatId = message["chat"]["id"] | 0LL;
    if (chatId != expectedChatId) {
      continue;
    }

    const char* text = message["text"] | "";
    if (text[0] == '\0') {
      continue;
    }
    handleTelegramCommand(text, nowMs);
  }
}

void maybeSendStartupTelegram(uint32_t nowMs) {
  if (gTelegramStartupSent || !telegramEnabled()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED || !appBridge.mqttConnected()) {
    return;
  }

  char statusMessage[320] = {0};
  char startupMessage[352] = {0};
  buildStatusMessage(statusMessage, sizeof(statusMessage), nowMs);
  snprintf(
      startupMessage,
      sizeof(startupMessage),
      "POWER LIGHT V1 conectado.\n%s",
      statusMessage);

  if (!telegramSendMessage(startupMessage)) {
    return;
  }

  gTelegramStartupSent = true;
}

void updateLowBatteryAlarm(uint32_t nowMs) {
  const float batteryV = batteryMonitor.data().filteredBatteryVoltage;
  if (!isfinite(batteryV) || batteryV <= 0.0f) {
    return;
  }

  const bool criticalNow = batteryV <= kCriticalLowBatteryV;

  if (criticalNow && !gLowBatteryCriticalActive) {
    gLowBatteryCriticalActive = true;
    gLowBatteryBeepState = false;
    gLowBatteryBeepLastToggleMs = nowMs;
    gLastLowBatteryTelegramMs = nowMs;
    Serial.print("ALERTA: bateria critica ");
    Serial.print(batteryV, 2);
    Serial.println("V");
    appBridge.publishAlert("BAT_LOW_CRITICAL", nowMs);
    char criticalMessage[96] = {0};
    snprintf(
        criticalMessage,
        sizeof(criticalMessage),
        "ALERTA CRITICA: bateria baja (%.2fV <= %.1fV)",
        batteryV,
        kCriticalLowBatteryV);
    telegramSendMessage(criticalMessage);
  } else if (!criticalNow && gLowBatteryCriticalActive) {
    gLowBatteryCriticalActive = false;
    gLowBatteryBeepState = false;
    digitalWrite(kBuzzerPin, LOW);
    Serial.print("INFO: bateria recuperada ");
    Serial.print(batteryV, 2);
    Serial.println("V");
    appBridge.publishAlert("BAT_LOW_RECOVERED", nowMs);
    char recoveredMessage[64] = {0};
    snprintf(
        recoveredMessage,
        sizeof(recoveredMessage),
        "INFO: bateria recuperada (%.2fV)",
        batteryV);
    telegramSendMessage(recoveredMessage);
    return;
  }

  if (!gLowBatteryCriticalActive) {
    return;
  }

  const uint16_t waitMs = gLowBatteryBeepState ? kLowBatteryBeepOnMs : kLowBatteryBeepOffMs;
  if (nowMs - gLowBatteryBeepLastToggleMs >= waitMs) {
    gLowBatteryBeepLastToggleMs = nowMs;
    gLowBatteryBeepState = !gLowBatteryBeepState;
    digitalWrite(kBuzzerPin, gLowBatteryBeepState ? HIGH : LOW);
  }

  if (nowMs - gLastLowBatteryTelegramMs >= kTelegramLowBatteryRepeatMs) {
    gLastLowBatteryTelegramMs = nowMs;
    char reminderMessage[96] = {0};
    snprintf(
        reminderMessage,
        sizeof(reminderMessage),
        "RECORDATORIO: bateria critica (%.2fV), revisar carga/consumo.",
        batteryV);
    telegramSendMessage(reminderMessage);
  }
}

void updatePzemHealthAlert(uint32_t nowMs) {
  const bool pzemFresh = isPzemDataFresh(nowMs);
  const bool startupGrace =
      (!gHasPzemSample) && (nowMs < kPzemFaultStartupGraceMs);
  if (!pzemFresh && !gPzemFaultActive && !startupGrace) {
    gPzemFaultActive = true;
    Serial.println("ALERTA: sin datos validos del PZEM.");
    appBridge.publishAlert("PZEM_DATA_LOST", nowMs);
    telegramSendMessage("ALERTA: PZEM sin datos validos.");
    return;
  }

  if (pzemFresh && gPzemFaultActive) {
    gPzemFaultActive = false;
    Serial.println("INFO: datos de PZEM restablecidos.");
    appBridge.publishAlert("PZEM_DATA_RESTORED", nowMs);
    telegramSendMessage("INFO: datos de PZEM restablecidos.");
  }
}

PzemData readPzem() {
  PzemData data;

  data.voltage = pzem.voltage();
  data.current = pzem.current();
  data.power = pzem.power();
  data.energy = pzem.energy();
  data.frequency = pzem.frequency();
  data.pf = pzem.pf();

  data.valid = !isnan(data.voltage) && !isnan(data.current) && !isnan(data.power) &&
               !isnan(data.energy) && !isnan(data.frequency) && !isnan(data.pf);

  return data;
}

void updatePzem(uint32_t nowMs) {
  if (nowMs - gLastPzemReadMs < kPzemReadIntervalMs) {
    return;
  }
  gLastPzemReadMs = nowMs;

  gLastPzemData = readPzem();
  gHasPzemSample = true;
  if (gLastPzemData.valid) {
    gLastPzemValidMs = nowMs;
  }
}

bool isPzemDataFresh(uint32_t nowMs) {
  return gHasPzemSample &&
         gLastPzemData.valid &&
         (nowMs - gLastPzemValidMs <= kPzemDataTimeoutMs);
}

uint8_t estimateBatteryCapacityPercent(float batteryVoltage) {
  // Temporary approximation for 12V lead-acid profile; later this will come from app config.
  const float vMin = 10.50f;
  const float vMax = 13.50f;
  if (!isfinite(batteryVoltage) || batteryVoltage <= vMin) {
    return 0;
  }
  if (batteryVoltage >= vMax) {
    return 100;
  }
  const float ratio = (batteryVoltage - vMin) / (vMax - vMin);
  return static_cast<uint8_t>(ratio * 100.0f + 0.5f);
}

AppTelemetry buildAppTelemetry(uint32_t nowMs) {
  AppTelemetry telemetry;
  const BatteryData& battery = batteryMonitor.data();
  const HallCurrentData& current = hallCurrentMonitor.data();
  const float batteryCurrentA = normalizedBatteryCurrentA(current);
  const bool pzemValid = isPzemDataFresh(nowMs);

  telemetry.line1AcPresent = gLine1AcPresent;
  telemetry.pzemValid = pzemValid;
  telemetry.pzemVoltage = pzemValid ? gLastPzemData.voltage : 0.0f;
  telemetry.pzemCurrent = pzemValid ? gLastPzemData.current : 0.0f;
  telemetry.pzemPower = pzemValid ? gLastPzemData.power : 0.0f;
  telemetry.batteryVoltage = battery.filteredBatteryVoltage;
  telemetry.batteryCurrent = batteryCurrentA;
  telemetry.batteryCapacityPercent = estimateBatteryCapacityPercent(battery.filteredBatteryVoltage);
  return telemetry;
}

void printBatterySnapshot() {
  const BatteryData& battery = batteryMonitor.data();
  const BatteryCalibration calibration = batteryMonitor.calibration();
  const uint8_t capacityPercent =
      estimateBatteryCapacityPercent(battery.filteredBatteryVoltage);

  Serial.print("BAT ADC: RAW=");
  Serial.print(battery.raw);
  Serial.print(" | ADC=");
  Serial.print(battery.adcMilliVolts);
  Serial.print("mV | Vadc=");
  Serial.print(battery.adcVoltage, 3);
  Serial.print("V | Vsense=");
  Serial.print(battery.sensedBatteryVoltage, 3);
  Serial.print("V | Vbat=");
  Serial.print(battery.batteryVoltage, 3);
  Serial.print("V | Vflt=");
  Serial.print(battery.filteredBatteryVoltage, 3);
  Serial.print("V | Scale=");
  Serial.print(calibration.scale, 4);
  Serial.print(" | Offset=");
  Serial.print(calibration.offsetV, 3);
  Serial.print("V | SOC=");
  Serial.print(capacityPercent);
  Serial.print("% | STATUS=");
  Serial.println(BatteryMonitor::rangeStatusText(battery.status));
}

void printHallSnapshot() {
  const HallCurrentData& current = hallCurrentMonitor.data();
  const float correctedCurrentA = correctedHallCurrentA(current);
  const float batteryCurrentA = normalizedBatteryCurrentA(current);

  Serial.print("ACS758: RAW=");
  Serial.print(current.adcRaw);
  Serial.print(" | ADC=");
  Serial.print(current.adcMilliVolts);
  Serial.print("mV");
  Serial.print(" | Vadc=");
  Serial.print(current.adcVoltage, 3);
  Serial.print("V | Vsens=");
  Serial.print(current.sensorVoltage, 3);
  Serial.print("V | Vzero=");
  Serial.print(current.zeroCurrentVoltage, 3);
  Serial.print("V | dV=");
  Serial.print(current.centeredVoltage, 3);
  Serial.print("V | Iinst=");
  Serial.print(current.instantCurrentA, 2);
  Serial.print("A | Ifilt=");
  Serial.print(current.filteredCurrentA, 2);
  Serial.print("A | Icorr=");
  Serial.print(correctedCurrentA, 2);
  Serial.print("A | Idead=");
  Serial.print(batteryCurrentA, 2);
  Serial.print("A | Sens=");
  Serial.print(hallCurrentMonitor.sensitivityVperA() * 1000.0f, 1);
  Serial.print("mV/A | Gain=");
  Serial.print(hallCurrentMonitor.currentGain(), 3);
  Serial.println();
}

void printTelemetryJson(uint32_t nowMs) {
  if (nowMs - gLastTelemetryPrintMs < kTelemetryPrintIntervalMs) {
    return;
  }
  gLastTelemetryPrintMs = nowMs;

  const BatteryData& battery = batteryMonitor.data();
  const HallCurrentData& current = hallCurrentMonitor.data();
  const float batteryCurrentA = normalizedBatteryCurrentA(current);
  const BatteryFlowState flowState = batteryFlowState(batteryCurrentA);
  const char* batteryStatus = BatteryMonitor::rangeStatusText(battery.status);
  const bool pzemValid = isPzemDataFresh(nowMs);

  Serial.println();
  Serial.println("============ TELEMETRIA ============");
  Serial.print("t(ms): ");
  Serial.print(nowMs);
  Serial.println();

  if (pzemValid) {
    Serial.printf("PZEM: V=%.2fV | I=%.3fA | P=%.1fW | E=%.3fkWh | F=%.1fHz | PF=%.2f\n",
                  gLastPzemData.voltage,
                  gLastPzemData.current,
                  gLastPzemData.power,
                  gLastPzemData.energy,
                  gLastPzemData.frequency,
                  gLastPzemData.pf);
  } else {
    Serial.println("PZEM: lectura invalida");
  }

  Serial.print("BAT : V=");
  Serial.print(battery.filteredBatteryVoltage, 2);
  Serial.print("V | I=");
  Serial.print(batteryCurrentA, 2);
  Serial.print("A | FLOW=");
  Serial.print(batteryFlowText(flowState));
  Serial.print(" | STATUS=");
  Serial.print(batteryStatus);
  Serial.println();
  printHallSnapshot();
  Serial.println("====================================");
  Serial.println();
}

void printCommandHelp() {
  Serial.println("Comandos disponibles:");
  Serial.println("  HELP");
  Serial.println("  (MQTT: publica el comando en <topic_base>/cmd)");
  Serial.println("Comandos Bateria:");
  Serial.println("  BAT STATUS");
  Serial.println("  BAT CAL <voltios_reales>");
  Serial.println("  BAT SCALE <factor>");
  Serial.println("  BAT OFFSET <voltios>");
  Serial.println("Comandos ACS758:");
  Serial.println("  HALL ZERO");
  Serial.println("  HALL RAW");
  Serial.println("  HALL GAIN <amps>   (opcional, despues de ajustar divisor)");
  Serial.println("  HALL STATUS");
  Serial.println("Comandos Corriente:");
  Serial.println("  CURR STATUS");
  Serial.println("  CURR ZERO");
  Serial.println("  CURR CAL <amps_reales>");
  Serial.println("  CURR DIR <1|-1>");
  Serial.println("  CURR DEAD <amps>   (ej: CURR DEAD 0.15)");
  Serial.println("Comandos RTC:");
  Serial.println("  RTC STATUS");
  Serial.println("  RTC SET YYYY-MM-DD HH:MM:SS");
  Serial.println("Comandos Alerta sonora:");
  Serial.println("  ALERT STATUS");
  Serial.println("  ALERT HOUR <0-23>");
  Serial.println("  ALERT HOUR ANY");
  Serial.println("  ALERT TEST LOST");
  Serial.println("  ALERT TEST RESTORE");
  Serial.println("Telegram bot:");
  Serial.println("  estado / status");
  Serial.println("  help");
}

void normalizeCommand(char* text) {
  if (!text) {
    return;
  }

  size_t len = strlen(text);
  size_t start = 0;
  while (start < len && isspace(static_cast<unsigned char>(text[start]))) {
    start++;
  }
  if (start > 0) {
    memmove(text, text + start, len - start + 1);
    len -= start;
  }

  while (len > 0 && isspace(static_cast<unsigned char>(text[len - 1]))) {
    text[--len] = '\0';
  }

  for (size_t i = 0; i < len; ++i) {
    text[i] = static_cast<char>(toupper(static_cast<unsigned char>(text[i])));
  }
}

void processConsoleCommand(const char* rawCmd) {
  if (!rawCmd) {
    return;
  }

  char cmd[kSerialCmdBufferSize];
  strncpy(cmd, rawCmd, sizeof(cmd) - 1);
  cmd[sizeof(cmd) - 1] = '\0';
  normalizeCommand(cmd);

  const size_t cmdLen = strlen(cmd);

  if (cmdLen == 0) {
    return;
  }

  if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "HALL HELP") == 0) {
    printCommandHelp();
    return;
  }

  if (strcmp(cmd, "RTC STATUS") == 0) {
    if (!gRtcReady) {
      Serial.println("RTC: no disponible.");
      return;
    }
    const DateTime now = rtc.now();
    Serial.printf("RTC: %04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
    return;
  }

  if (strncmp(cmd, "RTC SET ", 8) == 0) {
    int y = 0;
    int mo = 0;
    int d = 0;
    int h = 0;
    int mi = 0;
    int s = 0;
    const int parsed = sscanf(cmd + 8, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s);
    if (parsed != 6) {
      Serial.println("ERROR: formato RTC SET invalido. Usa RTC SET YYYY-MM-DD HH:MM:SS");
      return;
    }
    if (!gRtcReady) {
      Serial.println("ERROR: RTC no disponible.");
      return;
    }
    if (y < 2000 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) {
      Serial.println("ERROR: fecha/hora fuera de rango.");
      return;
    }
    rtc.adjust(DateTime(y, mo, d, h, mi, s));
    Serial.println("RTC: hora ajustada.");
    return;
  }

  if (strcmp(cmd, "ALERT STATUS") == 0) {
    Serial.print("Alerta retorno energia: ");
    if (gRestoreAlertHour == kRestoreHourAny) {
      Serial.println("SIEMPRE");
    } else {
      Serial.print("HORA ");
      Serial.println(gRestoreAlertHour);
    }
    return;
  }

  if (strcmp(cmd, "ALERT HOUR ANY") == 0) {
    gRestoreAlertHour = kRestoreHourAny;
    saveRestoreAlertHour();
    Serial.println("Alerta retorno energia: SIEMPRE.");
    return;
  }

  if (strncmp(cmd, "ALERT HOUR ", 11) == 0) {
    char* endPtr = nullptr;
    const long hour = strtol(cmd + 11, &endPtr, 10);
    if (endPtr == (cmd + 11) || hour < 0 || hour > 23) {
      Serial.println("ERROR: hora invalida. Usa ALERT HOUR <0-23> o ALERT HOUR ANY");
      return;
    }
    gRestoreAlertHour = static_cast<uint8_t>(hour);
    saveRestoreAlertHour();
    Serial.print("Alerta retorno energia configurada a hora ");
    Serial.println(gRestoreAlertHour);
    return;
  }

  if (strcmp(cmd, "ALERT TEST LOST") == 0) {
    Serial.println("Test buzzer: perdida de energia.");
    playPowerLostAlert();
    return;
  }

  if (strcmp(cmd, "ALERT TEST RESTORE") == 0) {
    Serial.println("Test buzzer: retorno de energia.");
    playPowerRestoredAlert();
    return;
  }

  if (strcmp(cmd, "BAT STATUS") == 0) {
    printBatterySnapshot();
    return;
  }

  if (strncmp(cmd, "BAT CAL ", 8) == 0) {
    char* endPtr = nullptr;
    const float measuredVoltage = strtof(cmd + 8, &endPtr);
    if (endPtr == (cmd + 8) || !isfinite(measuredVoltage) || measuredVoltage <= 0.0f) {
      Serial.println("ERROR: valor invalido. Usa BAT CAL <voltios_reales>");
      return;
    }
    if (batteryMonitor.calibrateFromMeasuredVoltage(measuredVoltage)) {
      saveBatteryCalibrationConfig();
      Serial.print("Bateria calibrada a ");
      Serial.print(measuredVoltage, 3);
      Serial.println("V");
      printBatterySnapshot();
    } else {
      Serial.println("ERROR: no se pudo calibrar bateria (verifica lectura actual).");
    }
    return;
  }

  if (strncmp(cmd, "BAT SCALE ", 10) == 0) {
    char* endPtr = nullptr;
    const float scale = strtof(cmd + 10, &endPtr);
    if (endPtr == (cmd + 10) || !isValidBatteryCalibrationScale(scale)) {
      Serial.println("ERROR: scale invalido. Rango 0.5000 a 1.5000");
      return;
    }
    BatteryCalibration calibration = batteryMonitor.calibration();
    calibration.scale = scale;
    batteryMonitor.setCalibration(calibration);
    saveBatteryCalibrationConfig();
    printBatterySnapshot();
    return;
  }

  if (strncmp(cmd, "BAT OFFSET ", 11) == 0) {
    char* endPtr = nullptr;
    const float offsetV = strtof(cmd + 11, &endPtr);
    if (endPtr == (cmd + 11) || !isValidBatteryCalibrationOffset(offsetV)) {
      Serial.println("ERROR: offset invalido. Rango -2.000 a 2.000V");
      return;
    }
    BatteryCalibration calibration = batteryMonitor.calibration();
    calibration.offsetV = offsetV;
    batteryMonitor.setCalibration(calibration);
    saveBatteryCalibrationConfig();
    printBatterySnapshot();
    return;
  }

  if (strcmp(cmd, "CURR STATUS") == 0) {
    const float batteryCurrentA = normalizedBatteryCurrentA(hallCurrentMonitor.data());
    Serial.print("Corriente dir=");
    Serial.print(gHallCurrentDirection > 0.0f ? "+1" : "-1");
    Serial.print(" deadband=");
    Serial.print(gHallDeadbandA, 2);
    Serial.print("A I=");
    Serial.print(batteryCurrentA, 2);
    Serial.print("A FLOW=");
    Serial.println(batteryFlowText(batteryFlowState(batteryCurrentA)));
    printHallSnapshot();
    return;
  }

  if (strcmp(cmd, "CURR ZERO") == 0 || strcmp(cmd, "HALL ZERO") == 0) {
    Serial.println("Calibrando cero ACS758... deja el conductor sin carga.");
    hallCurrentMonitor.calibrateZero(300);
    saveHallSensorCalibration();
    Serial.print("OK: Vzero=");
    Serial.print(hallCurrentMonitor.zeroCurrentVoltage(), 3);
    Serial.println("V");
    printHallSnapshot();
    return;
  }

  if (strncmp(cmd, "CURR CAL ", 9) == 0 || strncmp(cmd, "HALL GAIN ", 10) == 0) {
    const char* valueText = (strncmp(cmd, "CURR CAL ", 9) == 0) ? (cmd + 9) : (cmd + 10);
    char* endPtr = nullptr;
    const float knownA = strtof(valueText, &endPtr);
    if (endPtr == valueText) {
      Serial.println("ERROR: valor invalido. Ejemplo: CURR CAL 4.2");
      return;
    }
    if (knownA < kMinHallCalibrationCurrentA) {
      Serial.print("ERROR: usa una carga de calibracion >=");
      Serial.print(kMinHallCalibrationCurrentA, 1);
      Serial.println("A.");
      return;
    }

    Serial.print("Calibrando ganancia ACS758 con carga conocida de ");
    Serial.print(knownA, 3);
    Serial.println("A...");
    if (hallCurrentMonitor.calibrateGainFromKnownCurrent(knownA, 300)) {
      saveHallSensorCalibration();
      Serial.print("OK: nueva ganancia ACS758 = ");
      Serial.println(hallCurrentMonitor.currentGain(), 4);
      printHallSnapshot();
    } else {
      Serial.println("ERROR: no se pudo calibrar (corriente medida muy baja).");
    }
    return;
  }

  if (strncmp(cmd, "CURR DIR ", 9) == 0) {
    char* endPtr = nullptr;
    const long direction = strtol(cmd + 9, &endPtr, 10);
    if (endPtr == (cmd + 9) || (direction != 1 && direction != -1)) {
      Serial.println("ERROR: direccion invalida. Usa CURR DIR 1 o CURR DIR -1");
      return;
    }
    gHallCurrentDirection = static_cast<float>(direction);
    saveHallCurrentConfig();
    Serial.print("Corriente direccion ajustada a ");
    Serial.println(direction);
    return;
  }

  if (strncmp(cmd, "CURR DEAD ", 10) == 0) {
    char* endPtr = nullptr;
    const float deadbandA = strtof(cmd + 10, &endPtr);
    if (endPtr == (cmd + 10) || !isValidHallDeadband(deadbandA)) {
      Serial.print("ERROR: deadband invalido. Rango 0.00 a ");
      Serial.print(kMaxHallDeadbandA, 2);
      Serial.println("A");
      return;
    }
    gHallDeadbandA = deadbandA;
    saveHallCurrentConfig();
    Serial.print("Deadband corriente ajustado a ");
    Serial.print(gHallDeadbandA, 2);
    Serial.println("A");
    return;
  }

  if (strcmp(cmd, "HALL RAW") == 0) {
    printHallSnapshot();
    return;
  }

  if (strcmp(cmd, "HALL STATUS") == 0) {
    Serial.println("ACS758 DFRobot 50A:");
    Serial.print("  Centro nominal: ");
    Serial.print(kAcs758ZeroCurrentV, 3);
    Serial.println("V");
    Serial.print("  Sensibilidad: ");
    Serial.print(hallCurrentMonitor.sensitivityVperA() * 1000.0f, 1);
    Serial.println("mV/A");
    Serial.print("  Vzero actual: ");
    Serial.print(hallCurrentMonitor.zeroCurrentVoltage(), 3);
    Serial.println("V");
    Serial.print("  Divider ratio: ");
    Serial.println(hallCurrentMonitor.dividerRatio(), 4);
    Serial.println("  ADC max esperado ~= 3.24V con sensor a 4.5V");
    Serial.print("  Hall gain actual: ");
    Serial.println(hallCurrentMonitor.currentGain(), 4);
    Serial.print("  Hall dir actual: ");
    Serial.println(gHallCurrentDirection > 0.0f ? "+1" : "-1");
    Serial.print("  Hall deadband: ");
    Serial.print(gHallDeadbandA, 2);
    Serial.println("A");
    printHallSnapshot();
    return;
  }

  Serial.println("Comando no reconocido. Usa: HELP");
}

void onMqttConsoleCommand(const char* command) {
  if (!command || command[0] == '\0') {
    return;
  }
  Serial.print("MQTT CMD: ");
  Serial.println(command);
  processConsoleCommand(command);
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      processConsoleCommand(gSerialCommand);
      gSerialCommandLen = 0;
      gSerialCommand[0] = '\0';
      continue;
    }

    if (gSerialCommandLen < (kSerialCmdBufferSize - 1)) {
      gSerialCommand[gSerialCommandLen++] = c;
      gSerialCommand[gSerialCommandLen] = '\0';
    }
  }
}

void lcdBoot(const char* line1, const char* line2 = "", uint16_t holdMs = 0) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  if (holdMs > 0) {
    delay(holdMs);
  }
}

void lcdShowScreen(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void renderLine1AlertScreen() {
  if (gLine1AlertType == Line1AlertType::PowerLost) {
    lcdShowScreen("ALERTA LINEA 1", "SIN ELECTRICIDAD");
  } else if (gLine1AlertType == Line1AlertType::PowerRestored) {
    lcdShowScreen("LINEA 1 ACTIVA", "ENERGIA VOLVIO");
  }
}

void renderLcdScreen(uint8_t screenIndex, uint32_t nowMs) {
  char line1[17] = {0};
  char line2[17] = {0};
  const BatteryData& battery = batteryMonitor.data();
  const HallCurrentData& current = hallCurrentMonitor.data();
  const float batteryCurrentA = normalizedBatteryCurrentA(current);
  const BatteryFlowState flowState = batteryFlowState(batteryCurrentA);
  const bool pzemValid = isPzemDataFresh(nowMs);

  switch (screenIndex) {
    case 0:  // Pantalla 1: Estado AC + datos PZEM
      if (pzemValid) {
        snprintf(line1, sizeof(line1), "AC:%s V:%3.0fV", gLine1AcPresent ? "LINE" : "OUT", gLastPzemData.voltage);
        snprintf(line2, sizeof(line2), "A:%4.2f W:%4.0f", gLastPzemData.current, gLastPzemData.power);
      } else {
        snprintf(line1, sizeof(line1), "AC:%s V:---", gLine1AcPresent ? "LINE" : "OUT");
        snprintf(line2, sizeof(line2), "A:--.-- W:----");
      }
      lcdShowScreen(line1, line2);
      return;

    case 1:  // Pantalla 2: Bateria
      {
        uint8_t capPercent = estimateBatteryCapacityPercent(battery.filteredBatteryVoltage);
        if (capPercent > 99) {
          capPercent = 99;
        }
        snprintf(line1, sizeof(line1), "Vbat:%4.1fV C:%02u%%", battery.filteredBatteryVoltage, capPercent);
        snprintf(line2, sizeof(line2), "IBAT:%+4.1fA %s", batteryCurrentA, batteryFlowText(flowState));
      }
      lcdShowScreen(line1, line2);
      return;

    default:
      lcdShowScreen("Sistema", "Pantalla N/A");
      return;
  }
}

void updateLine1Ac(uint32_t nowMs) {
  if (digitalRead(kLine1AcPin) == LOW && gLine1LowSamples < UINT16_MAX) {
    gLine1LowSamples = static_cast<uint16_t>(gLine1LowSamples + 1);
  }

  if (nowMs - gLine1WindowStartMs < kLine1AcEvalWindowMs) {
    return;
  }

  noInterrupts();
  const uint16_t edgeCount = gLine1EdgeCountIsr;
  gLine1EdgeCountIsr = 0;
  interrupts();

  const bool pinLowNow = (digitalRead(kLine1AcPin) == LOW);
  const bool lowStateDetected = pinLowNow || (gLine1LowSamples >= kLine1AcMinLowSamples);
  const bool newLine1AcPresent = (edgeCount >= kLine1AcMinEdges) || lowStateDetected;
  gLine1LowSamples = 0;

  if (!gLine1AcInitialized) {
    gLine1AcPresent = newLine1AcPresent;
    gLine1AcInitialized = true;
  } else if (newLine1AcPresent != gLine1AcPresent) {
    gLine1AcPresent = newLine1AcPresent;
    gLcdRefreshRequested = true;
    if (!gLine1AcPresent) {
      gLine1AlertType = Line1AlertType::PowerLost;
      gLine1AlertUntilMs = nowMs + kLine1AlertHoldMs;
      Serial.println("ALERTA: LINEA 1 SIN ELECTRICIDAD");
      appBridge.publishAlert("LINE1_LOST", nowMs);
      telegramSendMessage("ALERTA: sin energia AC en LINEA 1.");
      playPowerLostAlert();
    } else {
      gLine1AlertType = Line1AlertType::PowerRestored;
      gLine1AlertUntilMs = nowMs + kLine1AlertHoldMs;
      Serial.println("INFO: LINEA 1 RESTABLECIDA");
      appBridge.publishAlert("LINE1_RESTORED", nowMs);
      telegramSendMessage("INFO: energia AC restablecida en LINEA 1.");
      playPowerRestoredAlert();
    }
  }

  gLine1WindowStartMs = nowMs;
}

void updateLcdDashboard(uint32_t nowMs) {
  const bool alertActive =
      (gLine1AlertType != Line1AlertType::None) &&
      (static_cast<int32_t>(gLine1AlertUntilMs - nowMs) > 0);

  if (alertActive) {
    if (!gAlertScreenVisible || gLcdRefreshRequested) {
      gLcdRefreshRequested = false;
      renderLine1AlertScreen();
    }
    gAlertScreenVisible = true;
    return;
  }

  if (gLine1AlertType != Line1AlertType::None) {
    gLine1AlertType = Line1AlertType::None;
  }
  if (gAlertScreenVisible) {
    gAlertScreenVisible = false;
    gLcdRefreshRequested = true;
    gLastLcdRotateMs = nowMs;
  }

  if (gLcdRefreshRequested) {
    gLcdRefreshRequested = false;
    gLastLcdRotateMs = nowMs;
    renderLcdScreen(gLcdScreenIndex, nowMs);
    return;
  }

  if (nowMs - gLastLcdRotateMs < kLcdRotateIntervalMs) {
    return;
  }

  gLastLcdRotateMs = nowMs;
  gLcdScreenIndex = (gLcdScreenIndex + 1) % 2;
  renderLcdScreen(gLcdScreenIndex, nowMs);
}

void setup() {
  lcd.init();
  lcd.backlight();
  lcdBoot("POWER LIGHT V1", "Iniciando...", 1000);
  Serial.begin(115200);
  telegramClient.setInsecure();
  pinMode(kBuzzerPin, OUTPUT);
  digitalWrite(kBuzzerPin, LOW);
  initRtc();
  loadRuntimeConfig();
  appBridge.setCommandCallback(onMqttConsoleCommand);
  appBridge.begin();

  pzemSerial.begin(9600, SERIAL_8N1, kPzemRxPin, kPzemTxPin);
  pinMode(kLine1AcPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kLine1AcPin), isrLine1Ac, CHANGE);
  lcdBoot("Init sensores", "PZEM/Bat/Hall", 700);
  Serial.println("Parte 2: lectura basica PZEM-004T v3.0");
  Serial.println("Parte 3: lectura de bateria por divisor resistivo en D2");
  Serial.println("Parte 4: ACS758 DFRobot 50A en D1 + divisor R1=3.9k R2=10k");
  Serial.println("Modo diagnostico ACS758: RAW ADC, mV ADC y V sensor por Serial.");
  Serial.println("No se aplica ganancia fija al arranque.");
  Serial.println("Usa HALL ZERO sin carga para fijar el cero real.");

  batteryMonitor.begin();
  hallCurrentMonitor.begin();
  lcdBoot("ACS758 activo", "Diag serial", 700);
  printHallSnapshot();
  printCommandHelp();
  ledStatus.begin(40);
  ledStatus.runStartupSequence();
  lcdBoot("Sistema listo", "Serial 115200", 1000);
  lcd.clear();
  gSerialCommandLen = 0;
  gSerialCommand[0] = '\0';
  gLine1WindowStartMs = millis();
  gLine1EdgeCountIsr = 0;
  gLine1LowSamples = 0;
  gLine1AcPresent = (digitalRead(kLine1AcPin) == LOW);
  gLine1AcInitialized = true;
  Serial.print("LINEA 1 inicial: ");
  Serial.println(gLine1AcPresent ? "ACTIVA" : "SIN ENERGIA");
  gLine1AlertUntilMs = 0;
  gLine1AlertType = Line1AlertType::None;
  gAlertScreenVisible = false;
  gLcdScreenIndex = 0;
  gLcdRefreshRequested = true;
  gLastLcdRotateMs = millis();
}

void loop() {
  const uint32_t nowMs = millis();

  appBridge.update(nowMs);
  maybeSendStartupTelegram(nowMs);
  pollTelegramCommands(nowMs);
  handleSerialCommands();
  batteryMonitor.update(nowMs);
  hallCurrentMonitor.update(nowMs);
  updatePzem(nowMs);
  updatePzemHealthAlert(nowMs);
  updateLine1Ac(nowMs);
  updateLowBatteryAlarm(nowMs);
  appBridge.publishTelemetry(buildAppTelemetry(nowMs), nowMs);
  updateLcdDashboard(nowMs);
  ledStatus.update(nowMs, batteryMonitor.chargeStage(), batteryMonitor.data().status);
  printTelemetryJson(nowMs);
}
