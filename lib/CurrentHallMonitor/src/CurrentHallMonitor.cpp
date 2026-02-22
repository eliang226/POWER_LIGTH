#include <CurrentHallMonitor.h>

namespace {
constexpr uint32_t kReadIntervalMs = 100;
}

CurrentHallMonitor::CurrentHallMonitor(const HallMonitorConfig& config) : config_(config) {}

void CurrentHallMonitor::begin() {
  analogReadResolution(12);
  pinMode(config_.adcPin, INPUT);
#ifdef ADC_11db
  analogSetPinAttenuation(config_.adcPin, ADC_11db);
#endif
}

void CurrentHallMonitor::calibrateZero(uint16_t samples) {
  const uint16_t avgMv = readAdcMilliVoltsAvg(samples);
  const float adcV = static_cast<float>(avgMv) / 1000.0f;
  const float sensedV = adcV / dividerRatio();
  centerOffsetV_ = sensedV - config_.sensorCenterV;
}

bool CurrentHallMonitor::calibrateGainFromKnownCurrent(float knownCurrentA, uint16_t samples) {
  if (knownCurrentA <= 0.0f) {
    return false;
  }

  const uint16_t avgMv = readAdcMilliVoltsAvg(samples);
  const float adcV = static_cast<float>(avgMv) / 1000.0f;
  const float sensedV = adcV / dividerRatio();
  const float centeredV = sensedV - (config_.sensorCenterV + centerOffsetV_);

  const float measuredAbsA = fabsf(centeredV / sensitivityVperA());
  if (measuredAbsA < 0.05f) {
    return false;
  }

  currentGain_ = knownCurrentA / measuredAbsA;
  return true;
}

void CurrentHallMonitor::update(uint32_t nowMs) {
  if (nowMs - lastReadMs_ < kReadIntervalMs) {
    return;
  }
  lastReadMs_ = nowMs;

  const uint16_t avgMv = readAdcMilliVoltsAvg(config_.samplesPerRead);
  data_.adcMilliVolts = avgMv;
  data_.adcVoltage = static_cast<float>(avgMv) / 1000.0f;
  data_.sensorVoltage = data_.adcVoltage / dividerRatio();

  const float centeredV = data_.sensorVoltage - (config_.sensorCenterV + centerOffsetV_);
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

float CurrentHallMonitor::currentGain() const {
  return currentGain_;
}

uint16_t CurrentHallMonitor::readAdcMilliVoltsAvg(uint16_t samples) const {
  if (samples == 0) {
    samples = 1;
  }

  uint32_t sumMv = 0;
  for (uint16_t i = 0; i < samples; ++i) {
    (void)analogRead(config_.adcPin);
    delayMicroseconds(config_.settleUs);
    sumMv += analogReadMilliVolts(config_.adcPin);
  }

  return static_cast<uint16_t>(sumMv / samples);
}

float CurrentHallMonitor::dividerRatio() const {
  const float denom = config_.resistorTopOhm + config_.resistorBottomOhm;
  if (denom <= 0.0f) {
    return 1.0f;
  }
  return config_.resistorBottomOhm / denom;
}

float CurrentHallMonitor::sensitivityVperA() const {
  if (config_.fullScaleCurrentA <= 0.0f) {
    return 1.0f;
  }
  return config_.sensorSpanV / config_.fullScaleCurrentA;
}
