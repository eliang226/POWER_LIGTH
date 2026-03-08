#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <PZEM004Tv30.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <liquidcrystal_i2c.h>
#include <AppBridge.h>
#include <BatteryMonitor.h>
#include <CurrentHallMonitor.h>
#include <LedStatus.h>

constexpr uint8_t kNeoPixelPin = D3;
constexpr uint8_t kNumPixels = 1;
constexpr uint8_t kPzemRxPin = D7;
constexpr uint8_t kPzemTxPin = D6;
constexpr uint8_t kBatteryAdcPin = D2;
constexpr uint8_t kHallAdcPin = D1;
constexpr uint8_t kLine1AcPin = A0;  // H11AA: LOW means AC present
constexpr uint8_t kLCD_I2cAddress = 0x27;
constexpr uint32_t kPzemReadIntervalMs = 2500;
constexpr uint32_t kPzemDataTimeoutMs = 10000;
constexpr uint32_t kTelemetryPrintIntervalMs = 20000;
constexpr uint32_t kLcdRotateIntervalMs = 5000;
constexpr uint32_t kLine1AlertHoldMs = 7000;
constexpr uint32_t kLine1AcEvalWindowMs = 250;
constexpr uint16_t kLine1AcMinEdges = 6;
constexpr uint16_t kLine1AcMinLowSamples = 3;
constexpr float kKnownLoadCurrentA = 3.52f;
constexpr float kMinHallCalibrationCurrentA = 3.0f;
constexpr size_t kSerialCmdBufferSize = 96;

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

Adafruit_NeoPixel pixels(kNumPixels, kNeoPixelPin, NEO_GRB + NEO_KHZ800);
HardwareSerial pzemSerial(1);
PZEM004Tv30 pzem(pzemSerial, kPzemRxPin, kPzemTxPin);
BatteryMonitor batteryMonitor(kBatteryAdcPin);
LedStatus ledStatus(pixels);
LiquidCrystal_I2C lcd(kLCD_I2cAddress, 16, 2);
AppBridge appBridge;

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

void IRAM_ATTR isrLine1Ac() {
  if (gLine1EdgeCountIsr < UINT16_MAX) {
    gLine1EdgeCountIsr = static_cast<uint16_t>(gLine1EdgeCountIsr + 1);
  }
}

HallMonitorConfig makeHallConfig() {
  HallMonitorConfig config;
  config.adcPin = kHallAdcPin;
  config.resistorTopOhm = 27000.0f;     // R1
  config.resistorBottomOhm = 4700.0f;   // R2
  config.sensorCenterV = 2.5f;          // 2.5V typical center
  config.sensorSpanV = 2.0f;            // 2.5 +/- 2V from vendor listing
  config.fullScaleCurrentA = 100.0f;    // Confirmed model: 100A
  config.bidirectional = true;          // Use false if you only need one direction
  config.samplesPerRead = 16;
  config.settleUs = 150;
  config.emaAlpha = 0.20f;
  return config;
}

CurrentHallMonitor hallCurrentMonitor(makeHallConfig());

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
  const float vMin = 11.50f;
  const float vMax = 12.73f;
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
  const bool pzemValid = isPzemDataFresh(nowMs);

  telemetry.line1AcPresent = gLine1AcPresent;
  telemetry.pzemValid = pzemValid;
  telemetry.pzemVoltage = pzemValid ? gLastPzemData.voltage : 0.0f;
  telemetry.pzemCurrent = pzemValid ? gLastPzemData.current : 0.0f;
  telemetry.pzemPower = pzemValid ? gLastPzemData.power : 0.0f;
  telemetry.batteryVoltage = battery.filteredBatteryVoltage;
  telemetry.batteryCurrent = current.filteredCurrentA;
  telemetry.batteryCapacityPercent = estimateBatteryCapacityPercent(battery.filteredBatteryVoltage);
  return telemetry;
}

void printTelemetryJson(uint32_t nowMs) {
  if (nowMs - gLastTelemetryPrintMs < kTelemetryPrintIntervalMs) {
    return;
  }
  gLastTelemetryPrintMs = nowMs;

  const BatteryData& battery = batteryMonitor.data();
  const HallCurrentData& current = hallCurrentMonitor.data();
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
  Serial.print(current.filteredCurrentA, 2);
  Serial.print("A | HALL_mV=");
  Serial.print(current.adcMilliVolts);
  Serial.print("mV | GAIN=");
  Serial.print(hallCurrentMonitor.currentGain(), 3);
  Serial.print(" | STATUS=");
  Serial.print(batteryStatus);
  Serial.println();
  Serial.println("====================================");
  Serial.println();
}

void printHallCommandHelp() {
  Serial.println("Comandos Hall:");
  Serial.println("  HALL ZERO");
  Serial.println("  HALL GAIN <amps>   (>=3A, ej: HALL GAIN 4.2)");
  Serial.println("  HALL STATUS");
  Serial.println("  HALL HELP");
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

void processHallCommand(const char* rawCmd) {
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

  if (strcmp(cmd, "HALL HELP") == 0) {
    printHallCommandHelp();
    return;
  }

  if (strcmp(cmd, "HALL ZERO") == 0) {
    Serial.println("Calibrando cero Hall... deja el conductor sin carga.");
    hallCurrentMonitor.calibrateZero(250);
    Serial.println("OK: cero Hall calibrado.");
    return;
  }

  if (strcmp(cmd, "HALL STATUS") == 0) {
    Serial.print("Hall gain actual: ");
    Serial.println(hallCurrentMonitor.currentGain(), 4);
    return;
  }

  if (strncmp(cmd, "HALL GAIN ", 10) == 0) {
    char* endPtr = nullptr;
    const float knownA = strtof(cmd + 10, &endPtr);
    if (endPtr == (cmd + 10)) {
      Serial.println("ERROR: valor invalido. Ejemplo: HALL GAIN 4.2");
      return;
    }
    if (knownA < kMinHallCalibrationCurrentA) {
      Serial.print("ERROR: usa una carga de calibracion >=");
      Serial.print(kMinHallCalibrationCurrentA, 1);
      Serial.println("A.");
      return;
    }

    Serial.print("Calibrando ganancia Hall con carga conocida de ");
    Serial.print(knownA, 3);
    Serial.println("A...");
    if (hallCurrentMonitor.calibrateGainFromKnownCurrent(knownA, 300)) {
      Serial.print("OK: nueva ganancia Hall = ");
      Serial.println(hallCurrentMonitor.currentGain(), 4);
    } else {
      Serial.println("ERROR: no se pudo calibrar (corriente medida muy baja).");
    }
    return;
  }

  Serial.println("Comando no reconocido. Usa: HALL HELP");
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      processHallCommand(gSerialCommand);
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
      snprintf(line1, sizeof(line1), "VBat:%4.1fV A:%3.1f", battery.filteredBatteryVoltage, current.filteredCurrentA);
      snprintf(line2, sizeof(line2), "Capacidad:%3u%%", estimateBatteryCapacityPercent(battery.filteredBatteryVoltage));
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

  const bool lowStateDetected = (gLine1LowSamples >= kLine1AcMinLowSamples);
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
    } else {
      gLine1AlertType = Line1AlertType::PowerRestored;
      gLine1AlertUntilMs = nowMs + kLine1AlertHoldMs;
      Serial.println("INFO: LINEA 1 RESTABLECIDA");
      appBridge.publishAlert("LINE1_RESTORED", nowMs);
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
  appBridge.begin();

  pzemSerial.begin(9600, SERIAL_8N1, kPzemRxPin, kPzemTxPin);
  pinMode(kLine1AcPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kLine1AcPin), isrLine1Ac, CHANGE);
  lcdBoot("Init sensores", "PZEM/Bat/Hall", 700);
  Serial.println("Parte 2: lectura basica PZEM-004T v3.0");
  Serial.println("Parte 3: lectura de bateria por divisor resistivo en D2");
  Serial.println("Parte 4: sensor Hall 100A en D1 + divisor R1=27k R2=4.7k");

  batteryMonitor.begin();
  hallCurrentMonitor.begin();
  lcdBoot("Calibrando Hall", "Cero en curso");
  Serial.println("Calibrando cero de corriente Hall, deja el conductor sin carga...");
  hallCurrentMonitor.calibrateZero(250);
  if (kKnownLoadCurrentA >= kMinHallCalibrationCurrentA) {
    lcdBoot("Calibrando Hall", "Gain en curso");
    Serial.print("Calibrando ganancia Hall para carga conocida de ");
    Serial.print(kKnownLoadCurrentA, 1);
    Serial.println("A...");
    if (hallCurrentMonitor.calibrateGainFromKnownCurrent(kKnownLoadCurrentA, 300)) {
      lcdBoot("Gain Hall OK", "Calibrado", 700);
      Serial.print("Ganancia Hall ajustada a: ");
      Serial.println(hallCurrentMonitor.currentGain(), 4);
    } else {
      lcdBoot("Gain Hall ERROR", "Revise carga", 1200);
      Serial.println("No se pudo ajustar ganancia Hall (corriente medida muy baja).");
    }
  } else {
    lcdBoot("Gain Hall omit.", "Carga < 3.0A", 900);
    Serial.print("Calibracion de ganancia omitida: usar carga >=");
    Serial.print(kMinHallCalibrationCurrentA, 1);
    Serial.println("A.");
  }
  printHallCommandHelp();
  ledStatus.begin(40);
  ledStatus.runStartupSequence();
  lcdBoot("Sistema listo", "Serial 115200", 1000);
  lcd.clear();
  gSerialCommandLen = 0;
  gSerialCommand[0] = '\0';
  gLine1WindowStartMs = millis();
  gLine1EdgeCountIsr = 0;
  gLine1LowSamples = 0;
  gLine1AcPresent = false;
  gLine1AcInitialized = false;
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
  handleSerialCommands();
  batteryMonitor.update(nowMs);
  hallCurrentMonitor.update(nowMs);
  updatePzem(nowMs);
  updateLine1Ac(nowMs);
  appBridge.publishTelemetry(buildAppTelemetry(nowMs), nowMs);
  updateLcdDashboard(nowMs);
  ledStatus.update(nowMs, batteryMonitor.chargeStage(), batteryMonitor.data().status);
  printTelemetryJson(nowMs);
}
