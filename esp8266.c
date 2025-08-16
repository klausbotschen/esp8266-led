// This is a mash-up of the Due show() code + insights from Michael Miller's
// ESP8266 work for the NeoPixelBus library: github.com/Makuna/NeoPixelBus
// Needs to be a separate .c file to enforce ICACHE_RAM_ATTR execution.

#if defined(ESP8266)

#include <Arduino.h>
#ifdef ESP8266
#include <eagle_soc.h>
#endif

#define CYCLES_800_T0H  (F_CPU / 2500001) // 0.4us = 32 cycles
#define CYCLES_800_T1H  (F_CPU / 1250001) // 0.8us
#define CYCLES_800      (F_CPU /  800001) // 1.25us per bit

// The CCOUNT register increments on every processor-clock cycle.
// RSR = read special register
static uint32_t _getCycleCount(void) __attribute__((always_inline));
static inline uint32_t _getCycleCount(void) {
  uint32_t ccount;
  __asm__ __volatile__("rsr %0,ccount":"=a" (ccount));
  return ccount;
}


#ifdef ESP8266
IRAM_ATTR void espShow(uint8_t pinA, uint8_t *pixels, uint32_t numBytes) {
#else
void espShow(uint8_t pinA, uint8_t *pixels, uint32_t numBytes) {
#endif

    uint8_t *pA, *end, pixA, mask;
  uint32_t time0, time1, timec, c, startTime, offset = 0;

#ifdef ESP8266
  uint32_t pinMaskA;
  pinMaskA   = _BV(pinA);
#endif

  pA        =  pixels;
  end       =  pixels + numBytes;
  pixA      = *pA++;
  mask      = 0x80;

  time0  = CYCLES_800_T0H;
  time1  = CYCLES_800_T1H;
  timec  = CYCLES_800;

  noInterrupts();
  startTime = _getCycleCount() - timec;
  for(;;) {
    while(((c = _getCycleCount()) - startTime) < timec); // Wait for bit start
    startTime = c;
    // now set all bits to HIGH
#ifdef ESP8266
    GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, pinMaskA);
#else
    gpio_set_level(pinA, HIGH);
#endif
    while(((_getCycleCount()) - startTime) < time0);      // Wait low duration
#ifdef ESP8266
    if (!(pixA & mask)) GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, pinMaskA);
#else
    if (!(pixA & mask)) gpio_set_level(pinA, LOW);
#endif
    while(((_getCycleCount()) - startTime) < time1);      // Wait high duration
#ifdef ESP8266
    if (pixA & mask) GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, pinMaskA);
#else
    if (pixA & mask) gpio_set_level(pinA, LOW);
#endif
    if(!(mask >>= 1)) {                                   // Next bit/byte
      interrupts();
      if(pA >= end) break;
      pixA  = *pA++;
      mask = 0x80;
      noInterrupts();
    }
  }
  while((_getCycleCount() - startTime) < timec); // Wait for last bit
  interrupts();
}

  
#ifdef ESP8266
IRAM_ATTR void espShow4(uint8_t pinA, uint8_t pinB, uint8_t pinC, uint8_t pinD,
    uint8_t split, uint8_t *pixels, uint32_t numBytes) {
#else
void espShow4(uint8_t pinA, uint8_t pinB, uint8_t pinC, uint8_t pinD,
    uint8_t split, uint8_t *pixels, uint32_t numBytes) {
#endif

  uint8_t *pA, *pB, *pC, *pD, pixA, pixB, pixC, pixD, mask, bcnt = 0;
  uint32_t i = 0, time0, time1, timec, c, startTime;

#ifdef ESP8266
  uint32_t pinMaskA, pinMaskB, pinMaskC, pinMaskD, pma, pmn;
  pinMaskA   = _BV(pinA);
  pinMaskB   = _BV(pinB);
  pinMaskC   = _BV(pinC);
  pinMaskD   = _BV(pinD);
  pma = pinMaskA+pinMaskB+pinMaskC+pinMaskD;
#endif

  if (split == 0x02) { // split in 2, invert 2nd part, and copy *2
    numBytes = numBytes >> 1;
    pA        =  pixels + numBytes - 3;
    pB        =  pixels + numBytes;
    pC        =  pA;
    pD        =  pB;
  }
  else if (split == 0x03) { // split in 4, no copy
    numBytes = numBytes >> 2;
    pA        =  pixels;
    pB        =  pA + numBytes;
    pC        =  pB + numBytes;
    pD        =  pC + numBytes;
  }
  else { // no split and copy *4
    pA        =  pixels;
    pB        =  pA;
    pC        =  pA;
    pD        =  pA;
  }
  pixA      = *pA++;
  pixC      = *pC++;
  pixB      = *pB++;
  pixD      = *pD++;
  mask      = 0x80;

  time0  = CYCLES_800_T0H;
  time1  = CYCLES_800_T1H;
  timec  = CYCLES_800;

  noInterrupts();
  startTime = _getCycleCount() - timec;
  for(;;) {
#ifdef ESP8266
    pmn = 0;
    if (!(pixA & mask)) pmn += pinMaskA;
    if (!(pixB & mask)) pmn += pinMaskB;
    if (!(pixC & mask)) pmn += pinMaskC;
    if (!(pixD & mask)) pmn += pinMaskD;
#endif
    while(((c = _getCycleCount()) - startTime) < timec);
    startTime = c;
#ifdef ESP8266
    GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, pma);    // now set all bits to HIGH
#else
    gpio_set_level(pinA, HIGH);
    gpio_set_level(pinB, HIGH);
    gpio_set_level(pinC, HIGH);
    gpio_set_level(pinD, HIGH);
#endif
    while(((_getCycleCount()) - startTime) < time0);      // Wait low duration
#ifdef ESP8266
    GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, pmn);       // set 0 bits to LOW
#else
    if (!(pixA & mask)) gpio_set_level(pinA, LOW);
    if (!(pixB & mask)) gpio_set_level(pinB, LOW);
    if (!(pixC & mask)) gpio_set_level(pinC, LOW);
    if (!(pixD & mask)) gpio_set_level(pinD, LOW);
#endif
    while(((_getCycleCount()) - startTime) < time1);      // Wait high duration
#ifdef ESP8266
    GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, pma); // now set all bits to LOW
#else
    gpio_set_level(pinA, LOW);
    gpio_set_level(pinB, LOW);
    gpio_set_level(pinC, LOW);
    gpio_set_level(pinD, LOW);
#endif
    if(!(mask >>= 1)) {                                   // Next bit/byte
      if(i++ >= numBytes) break;
      bcnt++;
      if (bcnt == 3) {
        interrupts();
        if (split == 0x02) {
          pA = pA - 6;
          pC = pC - 6;
        }
      }
      pixA  = *pA++;
      pixB  = *pB++;
      pixC  = *pC++;
      pixD  = *pD++;
      mask = 0x80;
      if (bcnt == 3) {
        bcnt = 0;
        noInterrupts();
      }
    }
  }
  while((_getCycleCount() - startTime) < timec); // Wait for last bit
  interrupts();
}

#endif // ESP8266
