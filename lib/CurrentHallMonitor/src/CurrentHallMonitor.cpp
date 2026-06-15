#include <CurrentHallMonitor.h>

namespace {
constexpr uint32_t kReadIntervalMs = 100;
constexpr float kMinZeroCurrentVoltage = 0.0f;
constexpr float kMaxZeroCurrentVoltage = 5.0f;
constexpr float kMinCurrentGain = 0.05f;
constexpr float kMaxCurrentGain = 10.0f;
}

CurrentHallMonitor::CurrentHallMonitor(const HallMonitorConfig& config) : config_(config) {}

void CurrentHallMonitor::begin() {
  analogReadResolution(12);
  pinMode(config_.adcPin, INPUT);
#ifdef ADC_11db
  analogSetPinAttenuation(config_.adcPin, ADC_11db);
#endif
  data_.zeroCurrentVoltage = zeroCurrentVoltage();
}

void CurrentHallMonitor::calibrateZero(uint16_t samples) {
  const HallAdcAverages averages = readAdcAverages(samples);
  const float adcV = static_cast<float>(averages.milliVolts) / 1000.0f;
  const float sensedV = adcV / dividerRatio();
  centerOffsetV_ = sensedV - config_.sensorCenterV;
  data_.zeroCurrentVoltage = config_.sensorCenterV + centerOffsetV_;
}

bool CurrentHallMonitor::calibrateGainFromKnownCurrent(float knownCurrentA, uint16_t samples) {
  if (knownCurrentA <= 0.0f) {
    return false;
  }

  const HallAdcAverages averages = readAdcAverages(samples);
  const float adcV = static_cast<float>(averages.milliVolts) / 1000.0f;
  const float sensedV = adcV / dividerRatio();
  const float centeredV = sensedV - (config_.sensorCenterV + centerOffsetV_);

  const float measuredAbsA = fabsf(centeredV / sensitivityVperA());
  if (measuredAbsA < 0.05f) {
    return false;
  }

  currentGain_ = knownCurrentA / measuredAbsA;
  return true;
}

void CurrentHallMonitor::setCalibration(const HallCalibration& calibration) {
  if (!isfinite(calibration.zeroCurrentVoltage) ||
      !isfinite(calibration.currentGain) ||
      calibration.zeroCurrentVoltage < kMinZeroCurrentVoltage ||
      calibration.zeroCurrentVoltage > kMaxZeroCurrentVoltage ||
      calibration.currentGain < kMinCurrentGain ||
      calibration.currentGain > kMaxCurrentGain) {
    return;
  }

  centerOffsetV_ = calibration.zeroCurrentVoltage - config_.sensorCenterV;
  currentGain_ = calibration.currentGain;
  data_.zeroCurrentVoltage = calibration.zeroCurrentVoltage;
}

void CurrentHallMonitor::update(uint32_t nowMs) {
  if (nowMs - lastReadMs_ < kReadIntervalMs) {
    return;
  }
  lastReadMs_ = nowMs;

  const HallAdcAverages averages = readAdcAverages(config_.samplesPerRead);
  data_.adcRaw = averages.raw;
  data_.adcMilliVolts = averages.milliVolts;
  data_.adcVoltage = static_cast<float>(averages.milliVolts) / 1000.0f;
  data_.sensorVoltage = data_.adcVoltage / dividerRatio();
  data_.zeroCurrentVoltage = config_.sensorCenterV + centerOffsetV_;

  const float centeredV = data_.sensorVoltage - data_.zeroCurrentVoltage;
  data_.centeredVoltage = centeredV;
  const float currentA = (centeredV / sensitivityVperA()) * currentGain_;

  if (config_.bidirectional) {
    data_.instantCurrentA = currentA;
  } else {
    data_.instantCurrentA = (currentA < 0.0f) ? 0.0f : currentA;
  }

  if (!data_.initialized) {
    data_.filteredCurrentA = data_.instantCurrentA;
    data_.initialized = true;
  } else {
    data_.filteredCurrentA =
        (config_.emaAlpha * data_.instantCurrentA) +
        ((1.0f - config_.emaAlpha) * data_.filteredCurrentA);
  }
}

const HallCurrentData& CurrentHallMonitor::data() const {
  return data_;
}

HallCalibration CurrentHallMonitor::calibration() const {
  HallCalibration calibration;
  calibration.zeroCurrentVoltage = zeroCurrentVoltage();
  calibration.currentGain = currentGain_;
  return calibration;
}

float CurrentHallMonitor::currentGain() const {
  return currentGain_;
}

float CurrentHallMonitor::zeroCurrentVoltage() const {
  return config_.sensorCenterV + centerOffsetV_;
}

HallAdcAverages CurrentHallMonitor::readAdcAverages(uint16_t samples) const {
  if (samples == 0) {
    samples = 1;
  }

  uint32_t sumRaw = 0;
  uint32_t sumMv = 0;
  for (uint16_t i = 0; i < samples; ++i) {
    (void)analogRead(config_.adcPin);
    delayMicroseconds(config_.settleUs);
    sumRaw += analogRead(config_.adcPin);
    sumMv += analogReadMilliVolts(config_.adcPin);
  }

  HallAdcAverages averages;
  averages.raw = static_cast<uint16_t>(sumRaw / samples);
  averages.milliVolts = static_cast<uint16_t>(sumMv / samples);
  return averages;
}

float CurrentHallMonitor::dividerRatio() const {
  const float denom = config_.resistorTopOhm + config_.resistorBottomOhm;
  if (denom <= 0.0f) {
    return 1.0f;
  }
  return config_.resistorBottomOhm / denom;
}

float CurrentHallMonitor::sensitivityVperA() const {
  if (config_.sensorSensitivityVPerA > 0.0f) {
    return config_.sensorSensitivityVPerA;
  }
  if (config_.fullScaleCurrentA > 0.0f && config_.sensorSpanV > 0.0f) {
    return config_.sensorSpanV / config_.fullScaleCurrentA;
  }
  return 1.0f;
}
