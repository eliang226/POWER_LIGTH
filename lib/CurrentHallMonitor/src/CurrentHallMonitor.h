#pragma once

#include <Arduino.h>

struct HallMonitorConfig {
  uint8_t adcPin = A0;
  float resistorTopOhm = 3900.0f;     // R1: sensor output -> ADC
  float resistorBottomOhm = 10000.0f; // R2: ADC -> GND
  float sensorCenterV = 2.5f;         // Typical Hall center voltage
  float sensorSensitivityVPerA = 0.04f;  // ACS758-50B: 40mV/A
  float sensorSpanV = 0.0f;              // Optional legacy fallback
  float fullScaleCurrentA = 50.0f;       // Optional legacy fallback
  bool bidirectional = true;
  uint8_t samplesPerRead = 16;
  uint16_t settleUs = 150;
  float emaAlpha = 0.20f;
};

struct HallAdcAverages {
  uint16_t raw = 0;
  uint16_t milliVolts = 0;
};

struct HallCurrentData {
  uint16_t adcRaw = 0;
  uint16_t adcMilliVolts = 0;
  float adcVoltage = 0.0f;
  float sensorVoltage = 0.0f;
  float zeroCurrentVoltage = 0.0f;
  float centeredVoltage = 0.0f;
  float instantCurrentA = 0.0f;
  float filteredCurrentA = 0.0f;
  bool initialized = false;
};

struct HallCalibration {
  float zeroCurrentVoltage = 2.5f;
  float currentGain = 1.0f;
};

class CurrentHallMonitor {
 public:
  explicit CurrentHallMonitor(const HallMonitorConfig& config);

  void begin();
  void calibrateZero(uint16_t samples = 200);
  bool calibrateGainFromKnownCurrent(float knownCurrentA, uint16_t samples = 300);
  void setCalibration(const HallCalibration& calibration);
  void update(uint32_t nowMs);

  const HallCurrentData& data() const;
  HallCalibration calibration() const;
  float currentGain() const;
  float zeroCurrentVoltage() const;
  float dividerRatio() const;
  float sensitivityVperA() const;

 private:
  HallAdcAverages readAdcAverages(uint16_t samples) const;

  HallMonitorConfig config_;
  HallCurrentData data_;
  uint32_t lastReadMs_ = 0;
  float centerOffsetV_ = 0.0f;
  float currentGain_ = 1.0f;
};
