#include <BatteryMonitor.h>

#include <math.h>

namespace {
constexpr uint32_t kBatteryReadIntervalMs = 250;
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
}  // namespace

BatteryMonitor::BatteryMonitor(uint8_t adcPin) : adcPin_(adcPin) {}

void BatteryMonitor::begin() {
  analogReadResolution(12);
  pinMode(adcPin_, INPUT);
#ifdef ADC_11db
  analogSetPinAttenuation(adcPin_, ADC_11db);
#endif
}

void BatteryMonitor::update(uint32_t nowMs) {
  if (nowMs - lastReadMs_ < kBatteryReadIntervalMs) {
    return;
  }
  lastReadMs_ = nowMs;

  const BatteryAdcAverages averages = readAverages();
  data_.raw = averages.raw;
  data_.adcMilliVolts = averages.milliVolts;
  if (data_.adcMilliVolts > 0) {
    data_.adcVoltage = static_cast<float>(data_.adcMilliVolts) / 1000.0f;
  } else {
    data_.adcVoltage = rawToAdcVoltage(data_.raw);
  }

  data_.batteryVoltage = adcToBatteryVoltage(data_.adcVoltage);
  data_.status = evaluateBatteryStatus(data_.batteryVoltage);

  if (!data_.initialized) {
    data_.filteredBatteryVoltage = data_.batteryVoltage;
    data_.initialized = true;
  } else {
    data_.filteredBatteryVoltage =
        (kBatteryEmaAlpha * data_.batteryVoltage) +
        ((1.0f - kBatteryEmaAlpha) * data_.filteredBatteryVoltage);
  }

  if (data_.filteredBatteryVoltage <= kLowBatteryTriggerV) {
    if (data_.lowBatteryCounter < kLowBatteryConfirmReadings) {
      data_.lowBatteryCounter++;
    }
    if (data_.lowBatteryCounter >= kLowBatteryConfirmReadings) {
      data_.lowBatteryAlarm = true;
    }
  } else if (data_.filteredBatteryVoltage >= kLowBatteryClearV) {
    data_.lowBatteryCounter = 0;
    data_.lowBatteryAlarm = false;
  }
}

const BatteryData& BatteryMonitor::data() const {
  return data_;
}

ChargeStage BatteryMonitor::chargeStage() const {
  return evaluateChargeStage(data_.filteredBatteryVoltage, data_.status, data_.lowBatteryAlarm);
}

const char* BatteryMonitor::rangeStatusText(BatteryStatus status) {
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

const char* BatteryMonitor::socStatusText(float batteryVoltage) {
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

const char* BatteryMonitor::chargeStageText(ChargeStage stage) {
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

BatteryAdcAverages BatteryMonitor::readAverages() const {
  uint32_t rawSum = 0;
  uint32_t milliVoltsSum = 0;

  for (uint8_t i = 0; i < kBatterySamplesPerRead; ++i) {
    (void)analogRead(adcPin_);
    delayMicroseconds(kBatterySampleSettleUs);
    rawSum += analogRead(adcPin_);
    milliVoltsSum += analogReadMilliVolts(adcPin_);
  }

  return {static_cast<uint16_t>(rawSum / kBatterySamplesPerRead),
          static_cast<uint16_t>(milliVoltsSum / kBatterySamplesPerRead)};
}

float BatteryMonitor::rawToAdcVoltage(uint16_t raw) {
  return (static_cast<float>(raw) * kAdcReferenceV) / static_cast<float>(kAdcMaxCount);
}

float BatteryMonitor::adcToBatteryVoltage(float adcVoltage) {
  const float dividerGain = (kR1Ohm + kR2Ohm) / kR2Ohm;
  const float batteryVoltage = adcVoltage * dividerGain;
  return (batteryVoltage * kBatteryCalibrationScale) + kBatteryCalibrationOffsetV;
}

BatteryStatus BatteryMonitor::evaluateBatteryStatus(float batteryVoltage) {
  if (batteryVoltage > kBatteryMaxSafeV) {
    return BatteryStatus::OverRange;
  }
  if (batteryVoltage < kBatteryMinReportV) {
    return BatteryStatus::BelowRange;
  }
  return BatteryStatus::InRange;
}

ChargeStage BatteryMonitor::evaluateChargeStage(float batteryVoltage,
                                                BatteryStatus rangeStatus,
                                                bool lowBatteryAlarm) {
  if (rangeStatus != BatteryStatus::InRange) return ChargeStage::Unknown;
  if (lowBatteryAlarm) return ChargeStage::LowBattery;
  if (batteryVoltage >= 15.0f) return ChargeStage::Equalize;
  if (batteryVoltage >= 14.2f) return ChargeStage::BulkOrAbsorption;
  if (batteryVoltage >= 13.2f) return ChargeStage::Float;
  if (batteryVoltage >= 12.7f) return ChargeStage::RestFull;
  return ChargeStage::Discharging;
}
