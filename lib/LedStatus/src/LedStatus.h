#pragma once

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include <BatteryMonitor.h>

class LedStatus {
 public:
  LedStatus(Adafruit_NeoPixel& pixels, uint8_t pixelIndex = 0);

  void begin(uint8_t defaultBrightness = 40);
  void runStartupSequence();
  void update(uint32_t nowMs, ChargeStage stage, BatteryStatus batteryStatus);

 private:
  void setSolidColor(uint8_t r, uint8_t g, uint8_t b);

  Adafruit_NeoPixel& pixels_;
  uint8_t pixelIndex_;

  uint8_t brightness_ = 0;
  int8_t fadeDirection_ = 4;
  uint32_t lastFadeMs_ = 0;
  bool lowBatteryBlinkOn_ = false;

  uint8_t lastLedR_ = 0;
  uint8_t lastLedG_ = 0;
  uint8_t lastLedB_ = 0;
  bool ledInitialized_ = false;
};
