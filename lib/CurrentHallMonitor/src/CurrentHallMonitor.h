#pragma once

#include <Arduino.h>

struct HallMonitorConfig {
  uint8_t adcPin = A0;
  float resistorTopOhm = 27000.0f;    // R1: sensor output -> ADC
  float resistorBottomOhm = 4700.0f;  // R2: ADC -> GND
  float sensorCenterV = 2.5f;         // Typical Hall center voltage
  float sensorSpanV = 2.0f;           // Typical swing from center to full-scale
  float fullScaleCurrentA = 50.0f;    // Depends on selected model (30/50/100/200A)
  bool bidirectional = true;
  uint8_t samplesPerRead = 16;
  uint16_t settleUs = 150;
  float emaAlpha = 0.20f;
};

struct HallCurrentData {
  uint16_t adcMilliVolts = 0;
  float adcVoltage = 0.0f;
  float sensorVoltage = 0.0f;
  float instantCurrentA = 0.0f;
  float filteredCurrentA = 0.0f;
  bool initialized = false;
};

class CurrentHallMonitor {
 public:
  explicit CurrentHallMonitor(const HallMonitorConfig& config);

  void begin();
  void calibrateZero(uint16_t samples = 200);
  bool calibrateGainFromKnownCurrent(float knownCurrentA, uint16_t samples = 300);
  void update(uint32_t nowMs);

  const HallCurrentData& data() const;
  float currentGain() const;

 private:
  uint16_t readAdcMilliVoltsAvg(uint16_t samples) const;
  float dividerRatio() const;
  float sensitivityVperA() const;

  HallMonitorConfig config_;
  HallCurrentData data_;
  uint32_t lastReadMs_ = 0;
  float centerOffsetV_ = 0.0f;
  float currentGain_ = 1.0f;
};
