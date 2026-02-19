#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <PZEM004Tv30.h>

#define NEO_PIXEL_PIN D3

Adafruit_NeoPixel pixels(1, NEO_PIXEL_PIN, NEO_GRB + NEO_KHZ800);


void setup() {
 
 
  pixels.begin();
  pixels.setBrightness(50);
  pixels.show();

  adc_attenuation_t

}

void loop() {

}

