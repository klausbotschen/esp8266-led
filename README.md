# WS2812B LED controller with ESP8266

This project requires the Arduino development framework and libraries for ESP8266, low level functions derived from the Adafriut NeoPixel library.

## Useful links

The C language understood by the Arduino compiler is limited and described here:

https://docs.arduino.cc/language-reference/

Introduction to the ESP8266 specific library functionalities:

https://arduino-esp8266.readthedocs.io/

Neopixel Uberguide - everything about LED programming

https://cdn-learn.adafruit.com/downloads/pdf/adafruit-neopixel-uberguide.pdf

One issue that arises is to map from linear input values to exponential output values, like the brightness in values from 0..15 to 4..254. The calculation can be performed with the following spreadsheet:

https://github.com/klausbotschen/esp8266-led/blob/main/doc/log-curve-fitting.ods

To update the firmware: esptool --port /dev/ttyUSB0 --baud 921600 write_flash --flash_mode dio --flash_size detect 0x00000 boot_v1.7.bin 0x01000 user1.1024.new.2-2022.bin 0x7c000 ../fw/esp_init_data_default_v08-good.bin 0xfC000 esp_init_data_default_v08.bin 0x7E000 blank.bin 0xfE000 blank.bin

https://github.com/espressif/ESP8266_NONOS_SDK/tree/master?tab=readme-ov-file

## Hardware

* AZDelivery ESP8266-12F D1-mini
* Rotary encoder AZDelivery KY-040
* Status LED with a single SK6812 (RGBW)
* Level converter boards for output lines

![Pinout of ESP8266 for this project](https://github.com/klausbotschen/esp8266-led/blob/main/doc/pin-useage-small.png)

## The sketch

Arduino programs are called a "sketch" which consists (at the bare minimum) of two functions, __setup()__ which initializes the program, and __loop()__ which is repeatedly invoked and performs the actual work. There is no fixed loop time; when a repetitive timing is desired, a __sleep()__ needs to be invoked, and the time can be calculated from the millisecond timer.

The sketch provides a set of LED effects with their parameters, it handles the rotary controller input to change parameters, maintains a clock with 1 minute granularity (one tick every 60 seconds), and processes data packets that have been received via WiFI.

### Effects

Following effects can be selected using the rotary wheel:

* Sparkling stars
* Candle light chain
* Color Flip
* Sprites: Moving dots, instantiate in one end, and vanish at the other
* Fire
* Lava
* Rainbow

## User Interface

The user interface consists of the rotary encoder and a status LED.

Rotary Encoder:

* rotate changes the value of the selected parameter,
* single short push: switch to next parameter,
* long push (> 1 sec): switch to next level; yellow for level 2, violet for level 1.

Changed values are made persistent after 20 seconds. To rise awareness, after 10 seconds the status LED will start flashing white, and after another 10 seconds, a one second white flash signals that the values are now persistent. Changing parameter values will reset the 20 seconds timer. This is done because the EEPROM has a limited number of cycles (usually around 10.000).

Status LED color depicts which parameter is currently affected by the rotary encoder. For level 1, these are:

* white: effect
* red: brightness
* green: speed
* blue: density, dampening (for fire/lava), morphing speed (color flip)
* violet: heat (lava), spark area size (fire), color (stars, color flip)

For "sparkling stars", violet allows to select the color range. To visualize the current selection, the current color is shown for 2/3 seconds, and violet is shown for 1/3 second. For the color range, following choices are available:

* white: new sparkling stars are selected from the full spectrum.
* 15 colors out of the color wheel: The selected color is used as center, and colors are selected from a narrow range around this color.
* dark grey: the center color is gradually walking through the color wheel in 136 seconds.

For "Color Flip", the violet parameter provides currently 10 color selections, where 4 of them perform an additional color morphing between two end states. The morphing is performed even when the speed parameter is set to zero and no actual color flip is done.

In level 2, the parameters concern the strip/string configuration, here, the parameter value is shown in discrete colors (1/3 parameter, 2/3 value):

* blue: LED strip channel length. red: 100, green: 120, turqouise: 181 (3m LED strip), violet: 200.
* green: LED type, red: RGB (strings), turquoise: GRB (strip).
* violet: channel splitting,
  * red: no split, the full length of the array is written to all 4 channels in parallel.
  * green: 2-split, the first half of the array is written to pins A and C, the seconed half is reversed in order and written to pins B and D, in effect A+B are one long strip and identical to C+D. The effect is streched out to two segments.
  * blue: 4-split, the array is cut in 4, and each segment is written to one pin, therefore each channel is fully separated.
* red: WiFi mode,
  * red: off
  * yellow: klaus iphone
  * turquise: leo
  * blue: follow (connect to hotspot)
  * violet: lead (start hotspot)

### LED output control

Color is either caclulated with RGB encoding, or HSV encoding (which is the color wheel):

![HSV Color Wheel](https://github.com/klausbotschen/esp8266-led/blob/main/doc/leds_hsv-diagram-phillip_burges.png)


## Details

In __setup__, we configure the WiFi part, set the I/O mode of all pins, check the EEPROM for valid data, and initialize the selected effects.