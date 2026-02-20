#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <PZEM004Tv30.h>
#include <math.h>

// =========================
// Parte 1 + Parte 2 (Refactor)
// =========================
// Hardware objetivo: Seeed XIAO ESP32-C6 + NeoPixel + PZEM-004T v3.0
constexpr uint8_t kNeoPixelPin = D3;
constexpr uint8_t kNumPixels = 1;
constexpr uint8_t kPzemRxPin = D7;
constexpr uint8_t kPzemTxPin = D6;
constexpr uint8_t kBatteryAdcPin = D2;

constexpr uint32_t kPzemReadIntervalMs = 2000;
constexpr uint32_t kBatteryReadIntervalMs = 250;
constexpr uint32_t kBatteryPrintIntervalMs = 1000;
constexpr uint8_t kFadeMin = 0;
constexpr int8_t kFadeStep = 4;
constexpr uint32_t kLowBatteryBlinkMs = 150;
constexpr uint32_t kFloatFadeIntervalMs = 45;
constexpr uint8_t kFloatFadeMax = 170;

constexpr float kAdcReferenceV = 3.3f;
constexpr uint16_t kAdcMaxCount = 4095;
constexpr float kR1Ohm = 40200.0f;
constexpr float kR2Ohm = 10000.0f;
constexpr float kBatteryMinReportV = 5.0f;
constexpr float kBatteryMaxSafeV = 16.5f;
constexpr float kBatteryCalibrationScale = 1.05f;
constexpr float kBatteryCalibrationOffsetV = 0.0f;
constexpr float kBatteryEmaAlpha = 0.20f;
constexpr uint8_t kBatterySamplesPerRead = 16;
constexpr uint16_t kBatterySampleSettleUs = 200;
constexpr float kLowBatteryTriggerV = 11.4f;
constexpr float kLowBatteryClearV = 11.7f;
constexpr uint8_t kLowBatteryConfirmReadings = 6;

struct PzemData {
  float voltage = NAN;
  float current = NAN;
  float power = NAN;
  float energy = NAN;
  float frequency = NAN;
  float pf = NAN;
  bool valid = false;
};

enum class BatteryStatus : uint8_t {
  BelowRange = 0,
  InRange = 1,
  OverRange = 2
};

struct BatteryData {
  uint16_t raw = 0;
  uint16_t adcMilliVolts = 0;
  float adcVoltage = 0.0f;
  float batteryVoltage = 0.0f;
  float filteredBatteryVoltage = 0.0f;
  BatteryStatus status = BatteryStatus::BelowRange;
  uint8_t lowBatteryCounter = 0;
  bool lowBatteryAlarm = false;
  bool initialized = false;
};

enum class ChargeStage : uint8_t {
  Equalize = 0,
  BulkOrAbsorption = 1,
  Float = 2,
  RestFull = 3,
  Discharging = 4,
  LowBattery = 5,
  Unknown = 6
};

ChargeStage evaluateChargeStage(float batteryVoltage, BatteryStatus rangeStatus, bool lowBatteryAlarm);
const char* chargeStageText(ChargeStage stage);

Adafruit_NeoPixel pixels(kNumPixels, kNeoPixelPin, NEO_GRB + NEO_KHZ800);
HardwareSerial pzemSerial(1);
PZEM004Tv30 pzem(pzemSerial, kPzemRxPin, kPzemTxPin);

uint8_t gBrightness = kFadeMin;
int8_t gFadeDirection = kFadeStep;
uint32_t gLastFadeMs = 0;
uint32_t gLastPzemReadMs = 0;
uint32_t gLastBatteryReadMs = 0;
uint32_t gLastBatteryPrintMs = 0;
BatteryData gBattery;
bool gLowBatteryBlinkOn = false;

void setSolidColor(uint8_t r, uint8_t g, uint8_t b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

void colorTestSequence() {
  setSolidColor(255, 0, 0);
  delay(700);
  setSolidColor(0, 255, 0);
  delay(700);
  setSolidColor(0, 0, 255);
  delay(700);
  setSolidColor(0, 0, 0);
  delay(400);
}

void blinkWhite(uint8_t times, uint16_t onMs, uint16_t offMs) {
  for (uint8_t i = 0; i < times; i++) {
    setSolidColor(255, 255, 255);
    delay(onMs);
    setSolidColor(0, 0, 0);
    delay(offMs);
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

  data.valid = !isnan(data.voltage) &&
               !isnan(data.current) &&
               !isnan(data.power) &&
               !isnan(data.energy) &&
               !isnan(data.frequency) &&
               !isnan(data.pf);

  return data;
}

void printPzem(const PzemData& data) {
  if (!data.valid) {
    Serial.println("PZEM: lectura invalida (NaN). Revisa RX/TX, GND comun y alimentacion.");
    return;
  }

  Serial.printf("PZEM -> V: %.1fV | I: %.3fA | P: %.1fW | E: %.3fkWh | F: %.1fHz | PF: %.2f\n",
                data.voltage, data.current, data.power, data.energy, data.frequency, data.pf);
}

void updatePzem() {
  const uint32_t now = millis();
  if (now - gLastPzemReadMs < kPzemReadIntervalMs) {
    return;
  }
  gLastPzemReadMs = now;

  const PzemData data = readPzem();
  printPzem(data);
}

uint16_t readBatteryRawAverage() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < kBatterySamplesPerRead; ++i) {
    (void)analogRead(kBatteryAdcPin);
    delayMicroseconds(kBatterySampleSettleUs);
    sum += analogRead(kBatteryAdcPin);
  }
  return static_cast<uint16_t>(sum / kBatterySamplesPerRead);
}

uint16_t readBatteryMilliVoltsAverage() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < kBatterySamplesPerRead; ++i) {
    sum += analogReadMilliVolts(kBatteryAdcPin);
  }
  return static_cast<uint16_t>(sum / kBatterySamplesPerRead);
}

float rawToAdcVoltage(uint16_t raw) {
  return (static_cast<float>(raw) * kAdcReferenceV) / static_cast<float>(kAdcMaxCount);
}

float adcToBatteryVoltage(float adcVoltage) {
  const float dividerGain = (kR1Ohm + kR2Ohm) / kR2Ohm;
  const float batteryVoltage = adcVoltage * dividerGain;
  return (batteryVoltage * kBatteryCalibrationScale) + kBatteryCalibrationOffsetV;
}

BatteryStatus evaluateBatteryStatus(float batteryVoltage) {
  if (batteryVoltage > kBatteryMaxSafeV) {
    return BatteryStatus::OverRange;
  }
  if (batteryVoltage < kBatteryMinReportV) {
    return BatteryStatus::BelowRange;
  }
  return BatteryStatus::InRange;
}

const char* batteryRangeStatusText(BatteryStatus status) {
  switch (status) {
    case BatteryStatus::BelowRange:
      return "BELOW_RANGE";
    case BatteryStatus::InRange:
      return "IN_RANGE";
    case BatteryStatus::OverRange:
      return "OVER_RANGE";
    default:
      return "UNKNOWN";
  }
}

const char* batterySocStatusText(float batteryVoltage) {
  // Tabla aproximada para plomo-acido 12V en reposo (OCV, ~25C):
  // 100%=12.73, 90%=12.62, 80%=12.50, 70%=12.37, 60%=12.27,
  // 50%=12.10, 40%=11.96, 30%=11.81, 20%=11.66, 10%=11.51.
  if (batteryVoltage >= 12.73f) return "SOC_100";
  if (batteryVoltage >= 12.62f) return "SOC_90";
  if (batteryVoltage >= 12.50f) return "SOC_80";
  if (batteryVoltage >= 12.37f) return "SOC_70";
  if (batteryVoltage >= 12.27f) return "SOC_60";
  if (batteryVoltage >= 12.10f) return "SOC_50";
  if (batteryVoltage >= 11.96f) return "SOC_40";
  if (batteryVoltage >= 11.81f) return "SOC_30";
  if (batteryVoltage >= 11.66f) return "SOC_20";
  if (batteryVoltage >= 11.51f) return "SOC_10";
  return "SOC_0";
}

ChargeStage evaluateChargeStage(float batteryVoltage, BatteryStatus rangeStatus, bool lowBatteryAlarm) {
  if (rangeStatus != BatteryStatus::InRange) return ChargeStage::Unknown;
  if (lowBatteryAlarm) return ChargeStage::LowBattery;
  if (batteryVoltage >= 15.0f) return ChargeStage::Equalize;
  if (batteryVoltage >= 14.2f) return ChargeStage::BulkOrAbsorption;
  if (batteryVoltage >= 13.2f) return ChargeStage::Float;
  if (batteryVoltage >= 12.7f) return ChargeStage::RestFull;
  return ChargeStage::Discharging;
}

const char* chargeStageText(ChargeStage stage) {
  switch (stage) {
    case ChargeStage::Equalize:
      return "EQUALIZE";
    case ChargeStage::BulkOrAbsorption:
      return "BULK_OR_ABSORPTION";
    case ChargeStage::Float:
      return "FLOAT";
    case ChargeStage::RestFull:
      return "REST_FULL";
    case ChargeStage::Discharging:
      return "DISCHARGING";
    case ChargeStage::LowBattery:
      return "LOW_BATTERY";
    default:
      return "N/A";
  }
}

void updateLedEffect() {
  const uint32_t now = millis();
  const ChargeStage stage = evaluateChargeStage(gBattery.filteredBatteryVoltage, gBattery.status, gBattery.lowBatteryAlarm);

  // LOW_BATTERY: parpadeo rapido rojo.
  if (stage == ChargeStage::LowBattery) {
    if (now - gLastFadeMs >= kLowBatteryBlinkMs) {
      gLastFadeMs = now;
      gLowBatteryBlinkOn = !gLowBatteryBlinkOn;
    }
    setSolidColor(gLowBatteryBlinkOn ? 255 : 0, 0, 0);
    return;
  }

  // BULK/ABSORPTION: doble pulso naranja.
  if (stage == ChargeStage::BulkOrAbsorption) {
    const uint16_t t = static_cast<uint16_t>(now % 900);
    const bool pulseOn = (t < 100) || (t >= 220 && t < 320);
    setSolidColor(pulseOn ? 255 : 0, pulseOn ? 120 : 0, 0);
    return;
  }

  // FLOAT: respiracion lenta verde.
  if (stage == ChargeStage::Float) {
    if (now - gLastFadeMs >= kFloatFadeIntervalMs) {
      gLastFadeMs = now;
      int16_t nextBrightness = static_cast<int16_t>(gBrightness) + gFadeDirection;
      if (nextBrightness >= kFloatFadeMax) {
        nextBrightness = kFloatFadeMax;
        gFadeDirection = -kFadeStep;
      } else if (nextBrightness <= kFadeMin) {
        nextBrightness = kFadeMin;
        gFadeDirection = kFadeStep;
      }
      gBrightness = static_cast<uint8_t>(nextBrightness);
    }
    setSolidColor(0, gBrightness, 0);
    return;
  }

  // Resto de estados: color solido.
  switch (stage) {
    case ChargeStage::Equalize:
      setSolidColor(255, 0, 255);
      break;
    case ChargeStage::RestFull:
      setSolidColor(0, 180, 255);
      break;
    case ChargeStage::Discharging:
      setSolidColor(0, 0, 255);
      break;
    default:
      if (gBattery.status == BatteryStatus::OverRange) {
        setSolidColor(255, 0, 255);
      } else {
        setSolidColor(0, 0, 80);
      }
      break;
  }
}

void updateBattery() {
  const uint32_t now = millis();
  if (now - gLastBatteryReadMs < kBatteryReadIntervalMs) {
    return;
  }
  gLastBatteryReadMs = now;

  gBattery.raw = readBatteryRawAverage();
  gBattery.adcMilliVolts = readBatteryMilliVoltsAverage();
  if (gBattery.adcMilliVolts > 0) {
    gBattery.adcVoltage = static_cast<float>(gBattery.adcMilliVolts) / 1000.0f;
  } else {
    gBattery.adcVoltage = rawToAdcVoltage(gBattery.raw);
  }
  gBattery.batteryVoltage = adcToBatteryVoltage(gBattery.adcVoltage);
  gBattery.status = evaluateBatteryStatus(gBattery.batteryVoltage);

  if (!gBattery.initialized) {
    gBattery.filteredBatteryVoltage = gBattery.batteryVoltage;
    gBattery.initialized = true;
  } else {
    gBattery.filteredBatteryVoltage =
      (kBatteryEmaAlpha * gBattery.batteryVoltage) +
      ((1.0f - kBatteryEmaAlpha) * gBattery.filteredBatteryVoltage);
  }

  if (gBattery.filteredBatteryVoltage <= kLowBatteryTriggerV) {
    if (gBattery.lowBatteryCounter < kLowBatteryConfirmReadings) {
      gBattery.lowBatteryCounter++;
    }
    if (gBattery.lowBatteryCounter >= kLowBatteryConfirmReadings) {
      gBattery.lowBatteryAlarm = true;
    }
  } else if (gBattery.filteredBatteryVoltage >= kLowBatteryClearV) {
    gBattery.lowBatteryCounter = 0;
    gBattery.lowBatteryAlarm = false;
  }
}

void printBattery() {
  const uint32_t now = millis();
  if (now - gLastBatteryPrintMs < kBatteryPrintIntervalMs) {
    return;
  }
  gLastBatteryPrintMs = now;

  const char* rangeStatus = batteryRangeStatusText(gBattery.status);
  const char* socStatus = "N/A";
  const ChargeStage stage = evaluateChargeStage(gBattery.filteredBatteryVoltage, gBattery.status, gBattery.lowBatteryAlarm);
  const char* chargeStage = chargeStageText(stage);
  const char* lowBatteryStatus = gBattery.lowBatteryAlarm ? "LOW_BATT_ON" : "LOW_BATT_OFF";
  if (gBattery.status == BatteryStatus::InRange) {
    socStatus = batterySocStatusText(gBattery.filteredBatteryVoltage);
  }

  Serial.printf("BAT -> raw:%u | mV:%u | Vadc:%.3fV | Vbat:%.2fV | Vf:%.2fV | STATUS:%s | SOC:%s | CHG_STAGE:%s | ALARM:%s | LBC:%u\n",
                gBattery.raw,
                gBattery.adcMilliVolts,
                gBattery.adcVoltage,
                gBattery.batteryVoltage,
                gBattery.filteredBatteryVoltage,
                rangeStatus,
                socStatus,
                chargeStage,
                lowBatteryStatus,
                gBattery.lowBatteryCounter);
}

void setup() {
  Serial.begin(115200);

  pixels.begin();
  pixels.setBrightness(40);
  pixels.show();

  pzemSerial.begin(9600, SERIAL_8N1, kPzemRxPin, kPzemTxPin);
  Serial.println("Parte 2: lectura basica PZEM-004T v3.0");
  Serial.println("Parte 3: lectura de bateria por divisor resistivo en D2");

  analogReadResolution(12);
  pinMode(kBatteryAdcPin, INPUT);
#ifdef ADC_11db
  analogSetPinAttenuation(kBatteryAdcPin, ADC_11db);
#endif

  colorTestSequence();
  blinkWhite(2, 200, 200);
}

void loop() {
  updateLedEffect();
  updatePzem();
  updateBattery();
  printBattery();
}
