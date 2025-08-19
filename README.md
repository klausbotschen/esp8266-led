# WS2812B LED controller with ESP8266

This project requires the Arduino development framework and libraries for ESP8266, low level functions derived from the Adafriut NeoPixel library.

## Useful links

https://arduino-esp8266.readthedocs.io/
https://docs.arduino.cc/language-reference/

## Hardware

* AZDelivery ESP8266-12F D1-mini
* Rotary encoder AZDelivery KY-040
* Status LED with a single SK6812 (RGBW)
* Level converter boards for output lines

![Pinout of ESP8266 for this project](https://github.com/klausbotschen/esp8266-led/blob/main/doc/pin-useage-small.png)

## The sketch

Arduino programs are called a "sketch" which consists (at the bare minimum) of two functions, __setup()__ which initializes the program, and __loop()__ which is repeatedly invoked and performs the actual work. There is no fixed loop time; when a repetitive timing is desired, a __sleep()__ needs to be invoked, and the time can be calculated from the millisecond timer.

The sketch provides a set of LED effects with their parameters, it handles the rotary controller input to change parameters, maintains a clock with 1 minute granularity (one tick every 60 seconds), and processes data packets that have been received via WiFI.

### Functional Overview

All LED data is stored in a single array with __LED_CNT__ lenght, and is written to 4 pins a the same time. This array can be split up (with a parameter at creation of the strip):

* _NOSPLIT_: the full length is written to all 4 pins, which means the strips have identical data.
* _SPLIT2_: the first half of the array is written to pins 1 and 3, the seconed half is reversed in order and written to pins 2 and 4. This results in having the full array on the strings with the controller in the middle.
* __split4_: the array is cut in 4, and each segment is written to one pin.

### Effects

Following effects can be selected using the rotary wheel, where the color shown

* Sparkling stars with following parameters:
  * Color wheel center: select a color, or "black" which moves through the wheel in 80 seconds.
  * Speed
  * Density
  * Brightness
* Candle light chain
* Dual Color Flip
* Sprites: Moving dots, instantiate in one end, and vanish at the other

## Details

In __setup__, we configure the WiFi part, set the I/O mode of all pins, check the EEPROM for valid data, and initialize the selected effects.