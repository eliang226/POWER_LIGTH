#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

// =========================
// Parte 1: NeoPixel básico
// =========================
// Hardware objetivo: Seeed XIAO ESP32-C6 + 1 LED NeoPixel (WS2812)
// Ajusta el pin si cambias el cableado.
#define NEO_PIXEL_PIN D3
#define NUM_PIXELS 1

Adafruit_NeoPixel pixels(NUM_PIXELS, NEO_PIXEL_PIN, NEO_GRB + NEO_KHZ800);

void setSolidColor(uint8_t r, uint8_t g, uint8_t b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b));
  pixels.show();
}

void colorTestSequence() {
  setSolidColor(255, 0, 0);   // Rojo
  delay(700);
  setSolidColor(0, 255, 0);   // Verde
  delay(700);
  setSolidColor(0, 0, 255);   // Azul
  delay(700);
  setSolidColor(0, 0, 0);     // Apagado
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

void setup() {
  pixels.begin();
  pixels.setBrightness(40); // Recomendado iniciar con brillo bajo (0..255)
  pixels.show();            // Inicializa en apagado

  // Demostración de arranque (Parte 1)
  colorTestSequence();
  blinkWhite(2, 200, 200);
}

void loop() {
  // Respiración simple en azul (sin librerías extra)
  static int brightness = 0;
  static int delta = 4;

  brightness += delta;
  if (brightness >= 180) {
    brightness = 180;
    delta = -delta;
  } else if (brightness <= 0) {
    brightness = 0;
    delta = -delta;
  }

  setSolidColor(0, 0, static_cast<uint8_t>(brightness));
  delay(25);
}
