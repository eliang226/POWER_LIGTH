#pragma once

#include <Arduino.h>

struct BatteryAdcAverages {
  uint16_t raw = 0;
  uint16_t milliVolts = 0;
};

enum class BatteryStatus : uint8_t {
  BelowRange = 0,
  InRange = 1,
  OverRange = 2
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

class BatteryMonitor {
 public:
  explicit BatteryMonitor(uint8_t adcPin);

  void begin();
  void update(uint32_t nowMs);

  const BatteryData& data() const;
  ChargeStage chargeStage() const;

  static const char* rangeStatusText(BatteryStatus status);
  static const char* socStatusText(float batteryVoltage);
  static const char* chargeStageText(ChargeStage stage);

 private:
  BatteryAdcAverages readAverages() const;
  static float rawToAdcVoltage(uint16_t raw);
  static float adcToBatteryVoltage(float adcVoltage);
  static BatteryStatus evaluateBatteryStatus(float batteryVoltage);
  static ChargeStage evaluateChargeStage(float batteryVoltage, BatteryStatus rangeStatus, bool lowBatteryAlarm);

  uint8_t adcPin_;
  uint32_t lastReadMs_ = 0;
  BatteryData data_;
};
