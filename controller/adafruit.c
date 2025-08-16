// code from Adafruit Neopixel library
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

uint8_t brightness=8, rOffset=0, gOffset=1, bOffset=2;
uint8_t *pixels;

uint32_t ColorHSV(uint16_t hue, uint8_t sat, uint8_t val) {
    uint8_t r, g, b;
    hue = (hue * 1530L + 32768) / 65536;
    // Convert hue to R,G,B (nested ifs faster than divide+mod+switch):
    if (hue < 510) { // Red to Green-1
    b = 0;
    if (hue < 255) { //   Red to Yellow-1
        r = 255;
        g = hue;       //     g = 0 to 254
    } else {         //   Yellow to Green-1
        r = 510 - hue; //     r = 255 to 1
        g = 255;
    }
    } else if (hue < 1020) { // Green to Blue-1
    r = 0;
    if (hue < 765) { //   Green to Cyan-1
        g = 255;
        b = hue - 510;  //     b = 0 to 254
    } else {          //   Cyan to Blue-1
        g = 1020 - hue; //     g = 255 to 1
        b = 255;
    }
    } else if (hue < 1530) { // Blue to Red-1
    g = 0;
    if (hue < 1275) { //   Blue to Magenta-1
        r = hue - 1020; //     r = 0 to 254
        b = 255;
    } else { //   Magenta to Red-1
        r = 255;
        b = 1530 - hue; //     b = 255 to 1
    }
    } else { // Last 0.5 Red (quicker than % operator)
    r = 255;
    g = b = 0;
    }

    // Apply saturation and value to R,G,B, pack into 32-bit result:
    uint32_t v1 = 1 + val;  // 1 to 256; allows >>8 instead of /255
    uint16_t s1 = 1 + sat;  // 1 to 256; same reason
    uint8_t s2 = 255 - sat; // 255 to 0
    return ((((((r * s1) >> 8) + s2) * v1) & 0xff00) << 8) |
         (((((g * s1) >> 8) + s2) * v1) & 0xff00) |
         (((((b * s1) >> 8) + s2) * v1) >> 8);
}

void setPixelColor(uint16_t n, uint32_t c) {
    uint8_t *p, r = (uint8_t)(c >> 16), g = (uint8_t)(c >> 8), b = (uint8_t)c;
    if (brightness) { // See notes in setBrightness()
        r = (r * brightness) >> 8;
        g = (g * brightness) >> 8;
        b = (b * brightness) >> 8;
    }
    p = &pixels[n * 3];
    p[rOffset] = r;
    p[gOffset] = g;
    p[bOffset] = b;
}

void addPixelColor(uint16_t n, uint32_t c) {
    uint8_t *p, r = (uint8_t)(c >> 16), g = (uint8_t)(c >> 8), b = (uint8_t)c;
    if (brightness) { // See notes in setBrightness()
        r = (r * brightness) >> 8;
        g = (g * brightness) >> 8;
        b = (b * brightness) >> 8;
    }
    p = &pixels[n * 3];
    uint16_t sr = p[rOffset] + r, sg = p[gOffset] + g, sb = p[bOffset] + b;
    p[rOffset] = sr > 255 ? 255 : sr;
    p[gOffset] = sg > 255 ? 255 : sg;
    p[bOffset] = sb > 255 ? 255 : sb;

}


// b 0..15 => 0..255
// stored as +1 to avoid multiplication when set to max
void setBrightness(uint8_t b) {
    float td = 4 * exp (0.277 * b);
    brightness = (uint8_t)round(td+1);
}

uint8_t *getPixels(void) { return pixels; }
void setPixels(uint8_t* p) { pixels = p; }

// eof
