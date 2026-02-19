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

constexpr uint32_t kPzemReadIntervalMs = 2000;
constexpr uint8_t kFadeMin = 0;
constexpr uint8_t kFadeMax = 180;
constexpr int8_t kFadeStep = 4;
constexpr uint32_t kFadeIntervalMs = 25;

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

uint8_t gBrightness = kFadeMin;
int8_t gFadeDirection = kFadeStep;
uint32_t gLastFadeMs = 0;
uint32_t gLastPzemReadMs = 0;

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

void updateFade() {
  const uint32_t now = millis();
  if (now - gLastFadeMs < kFadeIntervalMs) {
    return;
  }
  gLastFadeMs = now;

  int16_t nextBrightness = static_cast<int16_t>(gBrightness) + gFadeDirection;
  if (nextBrightness >= kFadeMax) {
    nextBrightness = kFadeMax;
    gFadeDirection = -kFadeStep;
  } else if (nextBrightness <= kFadeMin) {
    nextBrightness = kFadeMin;
    gFadeDirection = kFadeStep;
  }

  gBrightness = static_cast<uint8_t>(nextBrightness);
  setSolidColor(gBrightness, 0, 0);
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

void setup() {
  Serial.begin(115200);

  pixels.begin();
  pixels.setBrightness(40);
  pixels.show();

  pzemSerial.begin(9600, SERIAL_8N1, kPzemRxPin, kPzemTxPin);
  Serial.println("Parte 2: lectura basica PZEM-004T v3.0");

  colorTestSequence();
  blinkWhite(2, 200, 200);
}

void loop() {
  updateFade();
  updatePzem();
}
