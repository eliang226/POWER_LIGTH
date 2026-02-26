#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <PZEM004Tv30.h>
#include <math.h>

#include <BatteryMonitor.h>
#include <CurrentHallMonitor.h>
#include <LedStatus.h>

constexpr uint8_t kNeoPixelPin = D3;
constexpr uint8_t kNumPixels = 1;
constexpr uint8_t kPzemRxPin = D7;
constexpr uint8_t kPzemTxPin = D6;
constexpr uint8_t kBatteryAdcPin = D2;
constexpr uint8_t kHallAdcPin = D1;

constexpr uint32_t kPzemReadIntervalMs = 2500;
constexpr uint32_t kTelemetryPrintIntervalMs = 2500;
constexpr float kKnownLoadCurrentA = 3.52f;
constexpr float kMinHallCalibrationCurrentA = 3.0f;

struct PzemData {
  float voltage = NAN;
  float current = NAN;
  float power = NAN;
  float energy = NAN;
  float frequency = NAN;
  float pf = NAN;
  bool valid = false;
};

Adafruit_NeoPixel pixels(kNumPixels, kNeoPixelPin, NEO_GRB + NEO_KHZ800);
HardwareSerial pzemSerial(1);
PZEM004Tv30 pzem(pzemSerial, kPzemRxPin, kPzemTxPin);
BatteryMonitor batteryMonitor(kBatteryAdcPin);
LedStatus ledStatus(pixels);

uint32_t gLastPzemReadMs = 0;
uint32_t gLastTelemetryPrintMs = 0;
PzemData gLastPzemData;
bool gHasPzemSample = false;
String gSerialCommand;

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
}

void printTelemetryJson(uint32_t nowMs) {
  if (nowMs - gLastTelemetryPrintMs < kTelemetryPrintIntervalMs) {
    return;
  }
  gLastTelemetryPrintMs = nowMs;

  const BatteryData& battery = batteryMonitor.data();
  const HallCurrentData& current = hallCurrentMonitor.data();
  const char* batteryStatus = BatteryMonitor::rangeStatusText(battery.status);
  const bool pzemValid = gHasPzemSample && gLastPzemData.valid;

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

void processHallCommand(const String& rawCmd) {
  String cmd = rawCmd;
  cmd.trim();
  cmd.toUpperCase();

  if (cmd.length() == 0) {
    return;
  }

  if (cmd == "HALL HELP") {
    printHallCommandHelp();
    return;
  }

  if (cmd == "HALL ZERO") {
    Serial.println("Calibrando cero Hall... deja el conductor sin carga.");
    hallCurrentMonitor.calibrateZero(250);
    Serial.println("OK: cero Hall calibrado.");
    return;
  }

  if (cmd == "HALL STATUS") {
    Serial.print("Hall gain actual: ");
    Serial.println(hallCurrentMonitor.currentGain(), 4);
    return;
  }

  if (cmd.startsWith("HALL GAIN ")) {
    const String valueText = cmd.substring(10);
    const float knownA = valueText.toFloat();
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
      gSerialCommand = "";
      continue;
    }

    if (gSerialCommand.length() < 80) {
      gSerialCommand += c;
    }
  }
}

void setup() {
  Serial.begin(115200);

  pzemSerial.begin(9600, SERIAL_8N1, kPzemRxPin, kPzemTxPin);
  Serial.println("Parte 2: lectura basica PZEM-004T v3.0");
  Serial.println("Parte 3: lectura de bateria por divisor resistivo en D2");
  Serial.println("Parte 4: sensor Hall 100A en D1 + divisor R1=27k R2=4.7k");

  batteryMonitor.begin();
  hallCurrentMonitor.begin();
  Serial.println("Calibrando cero de corriente Hall, deja el conductor sin carga...");
  hallCurrentMonitor.calibrateZero(250);
  if (kKnownLoadCurrentA >= kMinHallCalibrationCurrentA) {
    Serial.print("Calibrando ganancia Hall para carga conocida de ");
    Serial.print(kKnownLoadCurrentA, 1);
    Serial.println("A...");
    if (hallCurrentMonitor.calibrateGainFromKnownCurrent(kKnownLoadCurrentA, 300)) {
      Serial.print("Ganancia Hall ajustada a: ");
      Serial.println(hallCurrentMonitor.currentGain(), 4);
    } else {
      Serial.println("No se pudo ajustar ganancia Hall (corriente medida muy baja).");
    }
  } else {
    Serial.print("Calibracion de ganancia omitida: usar carga >=");
    Serial.print(kMinHallCalibrationCurrentA, 1);
    Serial.println("A.");
  }
  printHallCommandHelp();
  ledStatus.begin(40);
  ledStatus.runStartupSequence();
}

void loop() {
  const uint32_t nowMs = millis();

  handleSerialCommands();
  batteryMonitor.update(nowMs);
  hallCurrentMonitor.update(nowMs);
  updatePzem(nowMs);
  ledStatus.update(nowMs, batteryMonitor.chargeStage(), batteryMonitor.data().status);
  printTelemetryJson(nowMs);
}
