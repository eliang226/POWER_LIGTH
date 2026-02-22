#include <LedStatus.h>

namespace {
constexpr uint8_t kFadeMin = 0;
constexpr int8_t kFadeStep = 4;
constexpr uint32_t kLowBatteryBlinkMs = 150;
constexpr uint32_t kFloatFadeIntervalMs = 45;
constexpr uint8_t kFloatFadeMax = 170;
}

LedStatus::LedStatus(Adafruit_NeoPixel& pixels, uint8_t pixelIndex)
    : pixels_(pixels), pixelIndex_(pixelIndex), fadeDirection_(kFadeStep) {}

void LedStatus::begin(uint8_t defaultBrightness) {
  pixels_.begin();
  pixels_.setBrightness(defaultBrightness);
  pixels_.show();
}

void LedStatus::runStartupSequence() {
  setSolidColor(255, 0, 0);
  delay(700);
  setSolidColor(0, 255, 0);
  delay(700);
  setSolidColor(0, 0, 255);
  delay(700);
  setSolidColor(0, 0, 0);
  delay(400);

  for (uint8_t i = 0; i < 2; i++) {
    setSolidColor(255, 255, 255);
    delay(200);
    setSolidColor(0, 0, 0);
    delay(200);
  }
}

void LedStatus::update(uint32_t nowMs, ChargeStage stage, BatteryStatus batteryStatus) {
  if (stage == ChargeStage::LowBattery) {
    if (nowMs - lastFadeMs_ >= kLowBatteryBlinkMs) {
      lastFadeMs_ = nowMs;
      lowBatteryBlinkOn_ = !lowBatteryBlinkOn_;
    }
    setSolidColor(lowBatteryBlinkOn_ ? 255 : 0, 0, 0);
    return;
  }

  if (stage == ChargeStage::BulkOrAbsorption) {
    const uint16_t t = static_cast<uint16_t>(nowMs % 900);
    const bool pulseOn = (t < 100) || (t >= 220 && t < 320);
    setSolidColor(pulseOn ? 255 : 0, pulseOn ? 120 : 0, 0);
    return;
  }

  if (stage == ChargeStage::Float) {
    if (nowMs - lastFadeMs_ >= kFloatFadeIntervalMs) {
      lastFadeMs_ = nowMs;
      int16_t nextBrightness = static_cast<int16_t>(brightness_) + fadeDirection_;
      if (nextBrightness >= kFloatFadeMax) {
        nextBrightness = kFloatFadeMax;
        fadeDirection_ = -kFadeStep;
      } else if (nextBrightness <= kFadeMin) {
        nextBrightness = kFadeMin;
        fadeDirection_ = kFadeStep;
      }
      brightness_ = static_cast<uint8_t>(nextBrightness);
    }
    setSolidColor(0, brightness_, 0);
    return;
  }

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
      if (batteryStatus == BatteryStatus::OverRange) {
        setSolidColor(255, 0, 255);
      } else {
        setSolidColor(0, 0, 80);
      }
      break;
  }
}

void LedStatus::setSolidColor(uint8_t r, uint8_t g, uint8_t b) {
  if (ledInitialized_ && r == lastLedR_ && g == lastLedG_ && b == lastLedB_) {
    return;
  }

  lastLedR_ = r;
  lastLedG_ = g;
  lastLedB_ = b;
  ledInitialized_ = true;

  pixels_.setPixelColor(pixelIndex_, pixels_.Color(r, g, b));
  pixels_.show();
}
