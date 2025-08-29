/* https://www.tweaking4all.com/hardware/arduino/adruino-led-strip-effects/#LEDStripEffectFire
 * 
 * 4.5m: 270/160, cooldown 0x03, base 0x1f, sparks 7, delay 0
 * 1.5m:  88/ 88, cooldown 0x07, base 0x07, sparks 2, delay 10
 */

#include <Adafruit_NeoPixel.h>
#include "entropy.h"

#define LED_PIN 5
#define LED_CNT 270
#define FIRE_CNT 135
#define BRIGHT 255
#define SPARKS 7
#define FIRE_BASE 0x07
#define COOLING 5

Adafruit_NeoPixel strip = Adafruit_NeoPixel (LED_CNT, LED_PIN, NEO_GRBW + NEO_KHZ800);
uint8_t heat[FIRE_CNT];
uint16_t i;

void Fire(void);
void setPixelHeatColor (uint16_t Pixel, uint16_t temperature);

void setup()
{
  setSeed();
  strip.begin();
}

void loop()
{
  Fire();
  //delay(5);
  strip.show();
}

void Fire(void)
{
  // Cool down every cell a little
  for (i=0; i<FIRE_CNT; i++) {
    uint8_t cooldown = (getRand() * COOLING) >> 8;
    if (cooldown > heat[i]) heat[i]=0;
    else heat[i] = heat[i]-cooldown;
  }
  // Heat from each cell drifts 'up' and diffuses a little
  for (i=FIRE_CNT-1; i>=2; i--) {
    heat[i] = (heat[i-1] + heat[i-2] + heat[i-2]) / 3;
  }
  // Randomly ignite new 'sparks' near the bottom
  for (i=0; i<SPARKS; i++) {
    uint16_t y = getRand() & FIRE_BASE;
    heat[y] += getRand() & 0x7f; // intentional overflow
  }
  // Convert heat to LED colors
  for (i=0; i<FIRE_CNT; i++)
    setPixelHeatColor (i<<1, heat[i]);
}

void setPixelHeatColor (uint16_t pixel, uint16_t temperature)
{
  // Scale 'heat' down from 0-255 to 0-191
  uint8_t t192 = temperature*191/255;
  // calculate ramp up from
  uint16_t heatramp = t192 & 0x3F; // 0..63
  heatramp = heatramp*4*BRIGHT/256;
  // figure out which third of the spectrum we're in:
  if( t192 >= 0x80) {                     // hottest
    strip.setPixelColor(pixel+1, BRIGHT, BRIGHT, heatramp);
    strip.setPixelColor(pixel, BRIGHT, BRIGHT, heatramp);
  } else if( t192 >= 0x40 ) {             // middle
    strip.setPixelColor(pixel+1, BRIGHT, heatramp, 0);
    strip.setPixelColor(pixel, BRIGHT, heatramp, 0);
  } else {                               // coolest
    strip.setPixelColor(pixel+1, heatramp, 0, 0);
    strip.setPixelColor(pixel, heatramp, 0, 0);
  }
}
