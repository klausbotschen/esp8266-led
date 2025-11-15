/*
  ESP8266 sparkles
  https://arduino-esp8266.readthedocs.io/en/latest/installing.html

    Start Arduino and open Preferences window.
    Enter https://arduino.esp8266.com/stable/package_esp8266com_index.json into Additional Board Manager URLs field. You can add multiple URLs, separating them with commas.
    Open Boards Manager from Tools > Board menu and find esp8266 platform.
    Select the version you need from a drop-down box.
    Click install button.
    Don’t forget to select your ESP8266 board from Tools > Board menu after installation.
  
*/

#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include "Adafruit_NeoPixel.h"
#include "entropy.h"

// max value, used only for storage allocation
#define LED_CNT 800
// WS2812B consume 24 bit per LED = 3 bytes
#define LED_BYTES 3
// 10 LED/m farytail string: RGB
// 60 LED/m strip: GRB
#define LED_PIXT NEO_RGB
#define LED_UDP (LED_CNT*LED_BYTES/4)

#define SPARKS (LED_CNT*64/100)
#define LAVASPARKS (LED_CNT / 4)
#define CANDLES (LED_CNT/25)
#define MAX_COL 4
#define SPR_CNT (LED_CNT/3)
// time in ms for each iteration => ca 30 fps
#define STEP 33

//const char* ssid = "A1-7DB236D1";
//const char* password = "ZrinqYrPZ19CR8";
//const char* ssid = "katzenwildbahn";
const char* ssid = "klausb";
const char* password = "buskatze";
//const char* ssid = "kanaan";
//const char* password = "welcome2home";


// D8 = GPIO15
Adafruit_NeoPixel state = Adafruit_NeoPixel (1, D8, NEO_GRBW);
// NEO_NOSPLIT: the full length is written to all 4 pins, which means the strips have identical data.
// NEO_SPLIT2: the first half of the array is written to pins 1 and 3, the seconed half is reversed in order
//      and written to pins 2 and 4. This results in having the full array on the strings with the controller in the middle.
// NEO_SPLIT4: the array is cut into 4 segments, and each segment is written to one pin.
// D3 => 16, D2 => 4, D1 => 5, D7 => 13
Adafruit_NeoPixel strip = Adafruit_NeoPixel (LED_CNT, D3, D2, D1, D7, LED_PIXT+NEO_NOSPLIT);
// note, _D3_ = GPIO0 oszillates after boot for ~50ms and has a similar effect after receiving a UDP packet. Whatever.

void Stars_Init(void);
void Stars_Load(void);
void Stars_DispCol(void);
uint8_t Stars_Param(void);
void Stars(void);

void Candle_Init(void);
uint8_t Candle_Param(void);
void Candles(void);

void Duco_Init(void);
void Duco_Load(void);
uint8_t Duco_Param(void);
void Duco(void);

void Rainbow_Init(void);
void Rainbow_Load(void);
uint8_t Rainbow_Param(void);
void Rainbow(void);

void Sprites_Init(void);
void Sprites_Load(void);
uint8_t Sprites_Param(void);
void Sprites(void);

void Fire_Init(void);
void Fire_Load(void);
uint8_t Fire_Param(void);
void Fire(void);

void Lava_Init(void);
void Lava_Load(void);
uint8_t Lava_Param(void);
void Lava(void);

void loadParam(void);

// ######################################################################

// number of effects
#define NPROG 7

// this datastructure is stored in the EEPROM
// and holds all necessary config entries for all effects.
struct
{
  uint32_t magic, ctrid; // controller ID
  uint16_t len; // size of this struct
  uint8_t slen, srgb, split; // configuration of the LED strip/string
  // effect configurations
  uint8_t type, scol, sacc, ccs, cdel, cdens, speed, stype, density;
  uint8_t sparks, cooling, flicker;
  uint8_t lavacool, heat, lavaspeed;
  uint8_t rbspeed, rbspread;
  uint8_t bri[NPROG];
} conf;

#define EE_MAGIC 0x1ecdaffe

// --------------------------------------------------------
// every effect needs to store some data from cycle to cycle.
typedef struct
{
  uint16_t hue;         // 0 = full spectrum, 1 = auto color wheel, 2..16 manual color wheel
  uint8_t slowing;      // 0 = full speed
  uint16_t col[SPARKS]; // color
  uint16_t pos[SPARKS]; // position
  uint16_t t0[SPARKS];  // start time
  uint8_t acc[SPARKS];  // accelerated aging
} sparks_t;

typedef struct
{
  uint16_t ts;              // timestamp
  uint16_t candles;         // derived from led_cnt
  uint8_t heat[LED_CNT];    // current candle heat
  uint16_t sparks[CANDLES]; // index of flickering candles
  uint16_t timer[CANDLES];  // start time
  uint16_t dur[CANDLES];    // duration
} candle_t;

typedef struct
{
  uint16_t ts, gts, del; // timestamp, gradient, and calculated delay
  uint16_t off;     // starting offset
  uint8_t walk, twin;
  uint16_t val[MAX_COL];
  float grad[MAX_COL];
} colors_t;

typedef struct
{
  uint16_t off, stsz;         // color offset, timestamp and step size
} rainbow_t;

typedef struct
{
  uint16_t s1, s2, iter;
  uint16_t col[SPR_CNT];
  uint16_t pos[SPR_CNT];  // 0..65535 => 0..LED_CNT
  uint16_t v[SPR_CNT];
  uint16_t dv[SPR_CNT];
} sprites_t;

typedef struct
{
  uint16_t ts, del;         // timestamp and calculated delay
  uint8_t heat[LED_CNT];    // current heat
} fire_t;

typedef struct
{
  uint16_t ts, del;         // timestamp and calculated delay
  uint8_t heat[LED_CNT];    // current heat
  uint16_t sparks[LAVASPARKS];
  uint8_t spix;
} lava_t;

// as only one effect is active at a time, we use a memory union
static union
{
  sparks_t sparks;
  candle_t candle;
  colors_t colors;
  sprites_t sprites;
  fire_t fire;
  lava_t lava;
  rainbow_t rainbow;
};

// ######################################################################

uint16_t base, now, conf_tim, alive_tim, d, led_cnt;
uint8_t conf_dirty, inacnt;
uint32_t stateCol, altstCol=0;

void paramsel(uint8_t);
void proginit(uint8_t load);
void rdclock(void);

// variables that are set in interrupt handling (rotary encoder)
volatile uint8_t type=0, col=0, bri=3, del=0, dens=0, slen=0, srgb=0, split=0;
volatile uint8_t re_max, *re_val_ptr, re_flag=0; // re_flag: flag for change
uint8_t wifi_param, re_param, re_debounce=0, re_selector=0;  // re_selector: parameter, re_param set to re_selector when re_flag=1
uint8_t re_click=0; // switch request, 1..click, 2..long click
uint8_t re_level = 1; // UI parameter level, 1..effects, 2..settings
uint16_t reqts; // switch pressed
void re_read(void);

// ######################################################################

ESP8266WebServer server(80);
WiFiUDP Udp;
IPAddress peer; // send alive packet only to peer!
#define localUdpPort 5700
uint8_t recPkt[1472], alivePkt[128];

// HTML Page
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP8266 Control</title>
  <script>
    function sendData() {
      var textInput = document.getElementById("textInput").value;
      var xhr = new XMLHttpRequest();
      xhr.open("POST", "/set", true);
      xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
      xhr.send("text=" + encodeURIComponent(textInput));
    }
  </script>
</head>
<body>
    <h2>bussicat-led</h2>
    <input type="text" id="textInput" placeholder="p0c0s5dFb4">
    <button onclick="sendData()">Send</button>
    <br/><p>Programs and parameters color, speed, density, brightness:
    <br>p0: Stars c [0..H: 0=full,1=wheel,2..H:red-green-blue] s [0..8] d [0..F] b [0..F]
    <br>p1: Candles b [0..F]
    <br>p2: Duco c [0..3, 2=morphing 0~1] s [0..G, 0=stop] b [0..F]
    <br>p3: Sprits s [0..F] d [0..8] b [0..F]
    <br>i<abcd1234>: 8 digit hex id
    </p>
</body>
</html>
)rawliteral";

// callback for the webserver getting a request for the root page
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

// process a HTTP request or UDP packet:
// input is a string with letters followed by a single HEX digit,
// e.g. p0c0s5dFb4
void handleCommand(uint8_t *rt, uint16_t len) {
  uint32_t ctrid=0;
  uint16_t i=0, cmd=0, cval=0;
  uint8_t idc=0;
  for (i=0; i < len; i++) {
    if (!cmd) cmd = *rt++;
    else {
      cval = *rt++;
      // convert ASCII to HEX digit
      if (cval >= 'a') cval -= 'a'-10;
      else if (cval >= 'A') cval -= 'A'-10;
      else cval -= '0';
      switch (cmd) {
        case 'p': wifi_param|=0x01; cmd = 0; type = cval; loadParam(); break;
        case 'b': wifi_param|=0x02; cmd = 0; bri = cval; break;
        case 'c': wifi_param|=0x04; cmd = 0; col = cval; break;
        case 's': wifi_param|=0x08; cmd = 0; del = cval; break;
        case 'd': wifi_param|=0x10; cmd = 0; dens = cval; break;
        // receive a new 8 HEX digit board ID
        case 'i': wifi_param|=0x11;
        ctrid <<= 4; ctrid += cval; idc++;
        if (idc >= 8) { conf.ctrid=ctrid; idc=0; cmd=0; *alivePkt=0; }
        break;
      }
    }
  }
}

// web browser sending back an AJAX post message with commands
void handlePost() {
  if (server.hasArg("text")) {
    String rt = server.arg("text");
    handleCommand ((uint8_t*)rt.c_str(), rt.length());
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request: Missing 'text' parameter");
  }
}

// got a special UDP packet with a sync command
// => write LED data
void handleSync(uint8_t *rt, uint16_t len) {
  if (type != 255) return;
  alive_tim = now;  // alive! data has been received
  strip.show();
}

// got a UDP packet with LED string data:
// first byte defines show flag (s) and strip bit field: 000s4321
// 0x1F = write pattern to all strips and show it
// default map 0x8421 
void handleBinary(uint8_t *rt, uint16_t len) {
  uint16_t map = conf.ctrid & 0x0000ffff;
  uint8_t cmd = *rt++;
  uint8_t frame = *rt++;
  uint8_t *pix = strip.getPixels();
  len--;
  if (type != 255) {
    type = 255;
    strip.clear();
    strip.setBrightness(255); // server does all brightness scaling
    split = 2; // set strip config to NEO_SPLIT4
    strip_config();
  }
  if (len > LED_UDP) len = LED_UDP;
  while (map) {
    if (map & cmd & 0x0f) memcpy (pix, rt, len);
    pix += LED_UDP;
    map >>= 4;
  }
  if (cmd & 0x10) {
    alive_tim = now;  // alive! data has been received
    strip.show();
  }
}

// called at every loop cycle when WiFi is connected:
// process received UDP packets and HTTP server requests
// when UDP LED packets have been received,
// we check if the data stream is still alive, if not, switch back
// to the last program, and send out broadcast packets with our ID.
void handleIP() {
  int packetSize = Udp.parsePacket();
  if (packetSize) {
    packetSize = Udp.read(recPkt, sizeof(recPkt));
    if (packetSize) switch (recPkt[0]) {
        case 'c': handleCommand(recPkt+1, packetSize-1); break;
        case 's': handleSync(recPkt+1, packetSize-1); break;
        case 'a': break;
        default: handleBinary(recPkt, packetSize);
    }
    peer = Udp.remoteIP();
  }
  server.handleClient();
  d = now - alive_tim;
  if (d > 2000) {
    if (!*alivePkt) {
      snprintf((char*)alivePkt, sizeof(alivePkt), "a%08x", conf.ctrid);
    }
    alive_tim = now;
    Udp.beginPacket("255.255.255.255", localUdpPort+1);
    Udp.write((char*)alivePkt);
    Udp.endPacket();
    if (type == 255) {
      inacnt++;
      // switch back to last program after timeout
      if (inacnt >= 3) {
        split = conf.split;
        strip_config();
        base = now;
        type = conf.type;
        proginit(1);
        inacnt = 0;
      }
    }
  }
}

// ######################################################################

// strip/string configuration was updated, re-configure neopixel library
void strip_config(void) {
  uint16_t led_type = 0;

  switch (slen) {
    case 0: led_cnt = 100; break;
    case 1: led_cnt = 120; break;
    case 2: led_cnt = 181; break;
    case 3: led_cnt = 200; break;
  }
  switch (srgb) {
    case 0: led_type = NEO_RGB; break;
    case 1: led_type = NEO_GRB; break;
  }
  switch (split) {
    case 0: led_type |= NEO_NOSPLIT; break;
    case 1: led_type |= NEO_SPLIT2; led_cnt *= 2; break;
    case 2: led_type |= NEO_SPLIT4; led_cnt *= 4; break;
  }
  strip.updateType(led_type);
  strip.updateLength(led_cnt);
}


// called once at startup

void setup() {
  // WiFi.begin(ssid, password);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/set", HTTP_POST, handlePost);
  server.begin();
  Udp.begin(localUdpPort);
  alive_tim = 0;
  *alivePkt = 0;
  *recPkt = 0;
  inacnt = 0;

  pinMode(D8, OUTPUT);
  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);
  pinMode(D3, OUTPUT);
  pinMode(D7, OUTPUT);
  digitalWrite(D8, LOW);
  digitalWrite(D1, LOW);
  digitalWrite(D2, LOW);
  digitalWrite(D3, LOW);
  digitalWrite(D7, LOW);
  pinMode(D4, INPUT);
  pinMode(D5, INPUT);
  pinMode(D6, INPUT);
  state.begin();
  state.setBrightness(8);

  conf_dirty = 0;
  // read configuration from EEPROM
  EEPROM.begin (sizeof(conf));
  EEPROM.get (0, conf);
  // when integrity fails, initialize with reasonable defaults
  if (conf.magic != EE_MAGIC || conf.len != sizeof(conf))
  {
    conf.magic = EE_MAGIC;
    conf.len = sizeof(conf);
    conf.slen = 0; // 0=100, 1=120, 2=181, 3=200
    conf.srgb = 0; // 0=RGB for string, 1=GRB for strip
    conf.split = 0; // 0=NOSPLIT, 1=SPLIT2, 2=SPLIT4
    conf.scol = 0;
    conf.sacc = 4;
    conf.ccs = 0; // duco color subtype
    conf.cdel = 4; // duco delay
    conf.cdens = 0; // duco delay spread
    for (d=0; d<NPROG; d++) conf.bri[d] = 3;
    conf.type = 3;
    conf.speed = 4; // sprite speed
    conf.stype = 3; // sprite density
    conf.density = 15;
    conf.cooling = 5;
    conf.sparks = 3;
    conf.flicker = 6;
    conf.lavacool = 3;
    conf.heat = 6;
    conf.lavaspeed = 7;
    conf.rbspeed = 3;
    conf.rbspread = 0;
    conf.ctrid = 0x00008421;
  }
  type = conf.type;
  slen = conf.slen;
  srgb = conf.srgb;
  split = conf.split;
  strip_config();
  paramsel(0);
  proginit(1);
  // rotary encoder uses interrupts
  attachInterrupt (D5, re_read, CHANGE);
  attachInterrupt (D6, re_read, CHANGE);
  base = millis();
}

void paramcol() {
  // color wheel for parameter values
  altstCol = state.ColorHSV (65536/(re_max+1) * (*re_val_ptr));
}

// select a new parameter
void paramsel(uint8_t p) {
  re_selector = p;
  altstCol = 0;
// we set encoder handling: (re_val_ptr and re_max), therefore disable interrupts
  noInterrupts();
  switch (re_level) {
    case 1: // depending on prog type get the parameters
    switch (type) {
      case 0: re_selector = Stars_Param(); break;
      case 1: re_selector = Candle_Param(); break;
      case 2: re_selector = Duco_Param(); break;
      case 3: re_selector = Sprites_Param(); break;
      case 4: re_selector = Fire_Param(); break;
      case 5: re_selector = Lava_Param(); break;
      case 6: re_selector = Rainbow_Param(); break;
    }
    if (re_selector == 0) { // no parameter => select prog type
      stateCol = 0x003f3f3f; // dim wite
      re_val_ptr = &type; re_max = NPROG-1; // select the effect
    }
    break;
    case 2: // set string/strip config
    if (re_selector > 2) re_selector = 0;
    switch (re_selector) {
      case 0: re_val_ptr = &slen; re_max = 3; stateCol = 0x000000ff; break;
      case 1: re_val_ptr = &srgb; re_max = 1; stateCol = 0x0000ff00; break;
      case 2: re_val_ptr = &split; re_max = 2; stateCol = 0x00ff00ff; break;
    }
    paramcol();
    break;
  }
  interrupts();
}

// convert range 0 .. 15 => 4 ... 255
uint8_t convertBrightness(void) {
  float td = 4 * exp (0.277 * bri);
  return td;
}

// load values from the EEPROM config structure
void loadParam()
{
  bri = conf.bri[type];
  strip.setBrightness(convertBrightness());
  switch (type) {
    case 0: Stars_Load(); break;
    case 1: break;
    case 2: Duco_Load(); break;
    case 3: Sprites_Load(); break;
    case 4: Fire_Load(); break;
    case 5: Lava_Load(); break;
    case 6: Rainbow_Load(); break;
  }
}

void proginit(uint8_t load)
{
  if (load) loadParam();
  switch (type) {
    case 0: Stars_Init(); break;
    case 1: Candle_Init(); break;
    case 2: Duco_Init(); break;
    case 3: Sprites_Init(); break;
    case 4: Fire_Init(); break;
    case 5: Lava_Init(); break;
    case 6: Rainbow_Init(); break;
  }
}

// main loop
// - check timer
// - updated values from the rotary encoder,
// - build user interface menu
// - call effects program
// - handle UI and time steps

void loop() {
  now = millis();
  // rotary encoder button: switch between parameters type specific
  // bit shift and add over some time to debounce
  re_debounce = (re_debounce << 1) + !digitalRead(D4);
  // when pressed over 4 cycles, recognize a press
  if (re_debounce & 0x0f) {
    if (!re_click) reqts = now, re_click = 1;  // current event has been noticed
    d = now - reqts;
    // long press over 1 second => switch to other level
    if (d > 1000) {
      switch (re_level) {
        case 1: stateCol = 0x007f7f00; break;
        case 2: stateCol = 0x007f007f; break;
      }
      altstCol = 0;
      re_selector = 0;
      re_click = 2;
    }
  }
  else if (re_click == 1) { // release click
    paramsel(++re_selector);
    re_click = 0;
  }
  else if (re_click == 2) { // release long click, switch level=1..2
    re_level++;
    if (re_level > 2) re_level=1, re_selector=0;
    paramsel(re_selector);
    re_click = 0;
  }
  // check wifi
  wifi_param = 0; // wifi_param holds which parameters have been set via WiFi
  if (WiFi.status() == WL_CONNECTED) handleIP();
  re_param = 0; // assume nothing changed, disable interrupts to sync flag handling
  // disable interrupt while we check rotary encoder values
  noInterrupts();
  if (re_flag || wifi_param) {
    // when rotary encoder is changed, store which parameter changed
    if (re_flag) re_param = re_selector + (re_level << 4);
    conf_dirty = 1;
    conf_tim = now;
  }
  re_flag = 0; // clear flag
  interrupts();
  // #### re_param = which parameter was changed ####
  if (re_param == 0x10 || wifi_param & 0x01) {   // new program type
    if (type != 255) conf.type = type;
    proginit(!wifi_param); // don't load parameters if from wifi
  }
  else if ((re_param & 0xf0) == 0x20) { // anything in level 2: update strip
    conf.slen = slen;
    conf.srgb = srgb;
    conf.split = split;
    paramcol();
    strip_config();
    proginit(1);
  }
  else if (re_param || wifi_param & 0x02) { // any other parameter
    // just in case update brightness, all other parameter are handled in realtime
    conf.bri[type] = bri;
    strip.setBrightness(convertBrightness());
  }
  // #### LED strip calculations except when type = 255, udp to pixel ####
  if (type != 255) {
    // finally we actually run our effect programs:
    switch (type) {
      case 0: Stars(); break;
      case 1: Candles(); break;
      case 2: Duco(); break;
      case 3: Sprites(); break;
      case 4: Fire(); break;
      case 5: Lava(); break;
      case 6: Rainbow(); break;
    }
    strip.show();
  }
  // user interface: blink status LED when has been changed and is written to EEPROM
  uint32_t sc = stateCol;
  // alt color is shown blinking, alternatively to the parameter color
  if (altstCol && ((now & 0x03ff) < 850)) sc = altstCol;
  if (conf_dirty) {
    d = now - conf_tim;
    // dirty config - blink from sec 10 to 19, then write.
    if (((d & 0x03ff) > 960 && d > 10000) || d > 19000) sc |= 0x7f000000;
    else sc &= 0x00ffffff;
    if (d > 20000) {
      sc &= 0x00ffffff;
      conf_dirty = 0;
      EEPROM.put (0, conf);
      EEPROM.commit();
    }
  }
  state.setPixelColor(0, sc);
  state.show();
  // no delay when udp, also fire and lava have their own delay
  if (type != 255 && type != 4 && type != 5) {
    base += STEP;
    d = base - now;
    if (d <= STEP) delay (d);
  }
  // keep base current
  else base = now;
}

// ######################################################################
// Box-Muller-transformation for gaussian distributed random numbers
// timing wise: add, mul 0.06µs, div 1.2µs, sqrt 8.2µs, pow 56µs, sin 16µs
// used by Stars effect to align the random colors around a (moving) center color.

#define MAX_VAR 0x7fffff80

uint16_t gauss_rand_16 (uint16_t mu, uint16_t var) {
  float r1 = (float) random(MAX_VAR) / MAX_VAR;
  float r2 = (float) random(1, MAX_VAR) / MAX_VAR; // avoid 0
  return mu + (float) var * cos (2*PI*r1) * sqrt(-log(r2));
}

// ######################################################################
// rotary encoder interrupt driven state machine
// input: re_max = max value
// output: *re_val_ptr = int value in the interval [0..re_max], and re_flag = 1 when new value

volatile uint8_t rep=0x0f, red=0;

IRAM_ATTR void re_read () {
  rep = (rep << 1) + digitalRead(D6);
  rep = (rep << 1) + digitalRead(D5);
  rep &= 0x0f;
  switch (rep) {
    case 0b1110: red = 'p'; break;
    case 0b1011: if (red == 'n') {
      if (*re_val_ptr) (*re_val_ptr)--;
      else *re_val_ptr = re_max; // turn around
      red = 0;
      re_flag = 1;  // raise flag
    }
    break;
    case 0b1101: red = 'n'; break;
    case 0b0111: if (red == 'p') {
      if (*re_val_ptr == re_max) *re_val_ptr = 0; // turn aroud
      else (*re_val_ptr)++;
      red = 0;
      re_flag = 1;  // raise flag
    }
    break;
  }
}

// ######################################################################
// sparkling stars all along the LED strip
// col 0: full spectrum random color
// col 1: slowly walk the color wheel and align the random colors around
// col 2..17: manually select a color wheel

#define SINULL 192

uint8_t col_dis = 0;  // flag to display color
uint16_t dcnt = 0;    // number of stars

uint8_t getNewAcc(void)
{
  switch (del) {
    case 0: return random ( 12,  18);
    case 1: return random ( 21,  28);
    case 2: return random ( 32,  44);
    case 3: return random ( 48,  64);
    case 4: return random ( 64,  96);
    case 5: return random (100, 134);
    case 6: return random (160, 190);
    case 7: return random (220, 245);
    case 8: return random (240, 255);
  }
  return 0;
}

uint16_t getNewPos(void);

void Star_Set(uint16_t i)
{
  if (col == 0) sparks.col[i] = random(65535);
  else if (col == 1) sparks.col[i] = gauss_rand_16(sparks.hue, 8196);
  else sparks.col[i] = gauss_rand_16(((uint16_t)(col-2)) << 12, 8196);
  sparks.pos[i] = getNewPos();
  sparks.acc[i] = getNewAcc();
}

void Stars_DispCol(uint8_t col)
{
  if (col == 0) altstCol = 0xffffff; // white: random all colors
  else if (col == 1) altstCol =  0x0f0f0f; // dark grey: color wheel walk
  else altstCol = state.ColorHSV (((uint16_t)(col-2)) << 12);
}

void Stars_Load(void)
{
  col = conf.scol;
  del = conf.sacc;
  dens = conf.density;
}

void Stars_Init(void)
{
  uint16_t i, cnt = led_cnt*64/100;

  sparks.hue = 0; // walking the circle
  dcnt = led_cnt * ((dens+1) << 2) / 100;
  // cnt is the maximal density, dcnt can be lower
  for (i=0; i<cnt; i++)
  {
    if (i < dcnt) {
      sparks.t0[i] = now - random(0x07ff);
      Star_Set(i);
    } else {
      sparks.t0[i] = 0;
      sparks.col[i] = 0;
      sparks.pos[i] = 0;
      sparks.acc[i] = 0;
    }
  }
  strip.clear();
}

void Stars(void)
{
  uint16_t i;

  for (i=0; i<dcnt; i++)
  {
    uint16_t p = sparks.pos[i];
    uint16_t d = now - sparks.t0[i];
    uint32_t age = (uint32_t)d * sparks.acc[i];
    age >>= 9;
    if (age > 0xff) // retired; prepare a new spark
    {
      sparks.t0[i] = now;
      Star_Set(i);
      age = 0; // clear the old pixel
    }
    uint8_t c = SINULL + age;
    uint32_t hsv = strip.ColorHSV (sparks.col[i], 255, strip.sine8(c));
    //hsv = strip.gamma32(hsv);
    strip.setPixelColor(p, hsv);
  }
  sparks.hue += 16; // slowly walk the color wheel, 136 seconds the round
  // parameter update
  if (re_param == 0x11 || wifi_param & 0x04) { // color wheel
    if (col > 17) col = 17;
    Stars_DispCol(col);
    conf.scol = col;
  }
  if (re_param == 0x12 || wifi_param & 0x08) { // acceleration
    if (del > 8) del = 8;
    conf.sacc = del;
  }
  if (re_param == 0x13 || wifi_param & 0x10) { // density
    if (dens > 15) dens = 15;
    conf.density = dens;
    Stars_Init();
  }
}

uint16_t getNewPos()
{
  uint16_t i = 0;
  uint16_t a, b = random(led_cnt);

  while (i<dcnt) // check if the selected LED is already taken
  {
    a = sparks.pos[i];
    if (a == b) // if taken, try a new one
    {
      b = random(led_cnt);
      i = 0;
    }
    else i++;
  }
  return b;
}

uint8_t Stars_Param() {
  switch (re_selector) {
      case 1: re_val_ptr = &col; re_max = 17; stateCol = 0x00ff00ff; Stars_DispCol(col); break;
      case 2: re_val_ptr = &del; re_max = 8; stateCol = 0x0000ff00; break;
      case 3: re_val_ptr = &dens; re_max = 15; stateCol = 0x000000ff; break;
      case 4: re_val_ptr = &bri; re_max = 15; stateCol = 0x00ff0000; break;
      default: return 0;
  }
  return re_selector;
}

// ######################################################################
// flickering candles

#define SPXDELAY 92
#define BASE 112
#define CA_DENS 25

uint32_t getPixelHeatColor (uint16_t temperature);

void Candle_Init(void)
{
  uint16_t i, cnt = led_cnt / CA_DENS;
  for (i=0; i<cnt; i++) {
    candle.sparks[i] = random(led_cnt);
    candle.timer[i] = now;
    candle.dur[i] = random(2000, 5000);
  }
  candle.ts = now - SPXDELAY;
}

void Candles(void)
{
  uint32_t temp;
  uint16_t i, cnt = led_cnt / CA_DENS;

  uint16_t d = now - candle.ts;
  if (d < SPXDELAY) return;
  candle.ts = now;
  // all candles flickering
  for (i=0; i<led_cnt; i++)
  {
    temp = BASE+getRandRange(4);
    candle.heat[i] = temp;
  }
  for (i=0; i<cnt; i++)
  {
    temp = BASE-8+getRandRange(48);
    candle.heat[candle.sparks[i]] = temp;
    d = now - candle.timer[i];
    if (d > candle.dur[i]) {
      candle.sparks[i] = random(led_cnt);
      candle.timer[i] = now;
      candle.dur[i] = random(2000, 5000);
    }
  }
  // Convert heat to LED colors
  for (i=0; i<led_cnt; i++)
    strip.setPixelColor(i, getPixelHeatColor (candle.heat[i]));
}

uint32_t getPixelHeatColor (uint16_t temperature)
{
  // Scale 'heat' down from 0-255 to 0-191
  uint8_t t192 = temperature*191/255;
  // calculate ramp up from
  uint16_t heatramp = t192 & 0x3F; // 0..63
  heatramp = heatramp*4;
  // figure out which third of the spectrum we're in:
  if( t192 >= 0x80) {                     // hottest
    return strip.Color(255, 255, heatramp);
  } else if( t192 >= 0x40 ) {             // middle
    return strip.Color(255, heatramp, 0);
  } else {                               // coolest
    return strip.Color(heatramp, 0, 0);
  }
}

uint8_t Candle_Param() {
  switch (re_selector) {
      case 1: re_val_ptr = &bri; re_max = 15; stateCol = 0x00ff0000; break;
      default: return 0;
  }
  return re_selector;
}

// ######################################################################
// toggle between colors, in 3 variations - 2 different color sets, and
// a mode that gradually changes colors between the color sets.

// effect speed with time delay:
// curve fitting 1 ... 16 => 10 ... 1500 with y = a e^bx
// log y = log a + bx log e
// Y = A + Bx
// sum(Y) = n A + B sum(x)
// sum(xY) = A sum(x) + B sum(x^2)
// => 4.174 = 2 A + 17 B
// => 51.82 = 17 A + 257 B
// A = 0.8546 => a = 7.155
// B = 0.1451 => b = 0.334

// del: 0 = stop, 1..16 => 16..1
// 1 ... 16 => 10 ... 1500
uint16_t Duco_Delay(void) {
  conf.cdel = del;
  float td = 7.155 * exp (0.334 * (17-del));
  return td;
}

// the gradient is used to walk through the color wheel,
// but always the long way, never through 0.
// would be a future improvement to select the path.
float getGradient(int32_t a, int32_t b) {
  return (float)(b-a)/20000;
}

// set new colors and calculate a gradient with the previous color
// for each of the MAX_COL colors.
// the current implementation uses a fixed number of parameters
// whereas it should be MAX_COL... future improvement.
void Duco_Set(uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
  uint8_t i = 0;
  colors.grad[i] = getGradient(a, colors.val[i]);
  colors.val[i++] = a;
  colors.grad[i] = getGradient(b, colors.val[i]);
  colors.val[i++] = b;
  colors.grad[i] = getGradient(c, colors.val[i]);
  colors.val[i++] = c;
  colors.grad[i] = getGradient(d, colors.val[i]);
  colors.val[i++] = d;
  colors.walk = 0;
  colors.twin = 0;
}

#define DUCO_MAX 8
void Duco_Select(uint8_t i) {
  switch (i) {
    case 0: Duco_Set (0, 10923, 41000, 21845 ); break; // red, yellow, light blue, green (shift)
    case 1: Duco_Set (45000, 56000, 62000, 50000); break; // magenta/violett (shift)
    case 2: Duco_Set (6000, 10923, 14000, 20000); break; // orange-green (shift)
    case 3: Duco_Set (23000, 29000, 34000, 40000); break; // green-turquiose (shift)
    case 4: Duco_Set (65500, 58000, 53000, 48000); break; // red-violet-blue (shift)
    case 5: Duco_Set (0, 43690, 10923, 21845 ); colors.twin = 1; break; // toggle red, blue <=> yellow, green
    case 6: Duco_Select(0); Duco_Select(1); colors.gts = now; colors.walk = 1; break; // shift and color morphing between 0/1
    case 7: Duco_Select(2); Duco_Select(3); colors.gts = now; colors.walk = 1; break; // shift and color morphing between 0/1
    case 8: Duco_Select(3); Duco_Select(4); colors.gts = now; colors.walk = 1; break; // shift and color morphing between 0/1
  }
}

void Duco_Load()
{
  col = conf.ccs;
  del = conf.cdel;
  dens = conf.cdens;
}

void Duco_Init()
{
  Duco_Load();
  colors.del = Duco_Delay();
  colors.ts = now - colors.del;
  colors.off = 0;
  Duco_Select(col);
}

void Duco()
{
  uint16_t v, i, c = colors.off;
  uint16_t d = now - colors.ts;
  uint16_t gd = now - colors.gts;
  if (gd >= 40000) colors.gts += 40000, gd -= 40000;
  if (gd > 20000) gd = 40000 - gd;
  // update parameters
  if (re_param == 0x11 || wifi_param & 0x04) { // color selection
    if (col > DUCO_MAX) col = DUCO_MAX;
    conf.ccs = col; Duco_Select(col);
  }
  if (re_param == 0x12 || wifi_param & 0x08) { // speed, 0=stop, 1..16
    if (del > 16) del = 16;
    colors.del = Duco_Delay();
  }
  if (re_param == 0x13 || wifi_param & 0x10) { // speed variation
    if (dens > 7) dens = 7;
    conf.cdens = dens;
  }
  if (del && d >= colors.del) {   // update was triggered timeout
    colors.ts = now;
    colors.off++; // advance offset
    if (colors.off == MAX_COL) colors.off = 0;
  }
  for (i=0; i<led_cnt; i++) {
    v = colors.val[c];
    if (colors.walk) v += colors.grad[c] * gd; // gradient handling
    uint32_t hsv = strip.ColorHSV (v);
    strip.setPixelColor(i, hsv);
    c++;
    if (c == MAX_COL) c = 0;
    if (colors.twin) {         // toggle between two sets
      c++;
      if (c == MAX_COL) c = 0;
    }
  }
}

uint8_t Duco_Param() {
  switch (re_selector) {
      case 1: re_val_ptr = &col; re_max = DUCO_MAX; stateCol = 0x00ff00ff; break;
      case 2: re_val_ptr = &del; re_max = 16; stateCol = 0x0000ff00; break;
      case 3: re_val_ptr = &dens; re_max = 7; stateCol = 0x000000ff; break;
      case 4: re_val_ptr = &bri; re_max = 15; stateCol = 0x00ff0000; break;
      default: return 0;
  }
  return re_selector;
}

// ######################################################################

// 1 ... 16 => 10 ... 1500, 0=stop
uint16_t Rainbow_Step(void) {
  if (del == 0) return 0;
  float td = 7.155 * exp (0.334 * del);
  return td;
}

// 0 ... 41 => 3 ... 240
float Rainbow_Spread(void) {
  float td = 3.0 * exp (0.128 * dens);
  return td;
}

void Rainbow_Load()
{
  del = conf.rbspeed;
  dens = conf.rbspread;
}

void Rainbow_Init()
{
  Rainbow_Load();
  rainbow.stsz = Rainbow_Step();
  rainbow.off = 0;
}

void Rainbow()
{
  uint16_t i, v, c = rainbow.off;

  // update parameters
  if (re_param == 0x11 || wifi_param & 0x08) { // speed, 0=stop, 1..16
    if (del > 16) del = 16;
    rainbow.stsz = Rainbow_Step();
    conf.rbspeed = del;
  }
  if (re_param == 0x12 || wifi_param & 0x10) { // color spread factor
    if (dens > 31) dens = 31;
    conf.rbspread = dens;
  }
  // divide color wheel by led count and density
  v = 655360.0 / led_cnt / Rainbow_Spread();
  for (i=0; i<led_cnt; i++) {
    uint32_t hsv = strip.ColorHSV (c);
    strip.setPixelColor(i, hsv);
    c += v; // walk along the color wheel
  }
  rainbow.off += rainbow.stsz; // advance offset
}

uint8_t Rainbow_Param() {
  switch (re_selector) {
      case 1: re_val_ptr = &del; re_max = 16; stateCol = 0x0000ff00; break;
      case 2: re_val_ptr = &dens; re_max = 31; stateCol = 0x000000ff; break;
      case 3: re_val_ptr = &bri; re_max = 15; stateCol = 0x00ff0000; break;
      default: return 0;
  }
  return re_selector;
}

// ######################################################################
// sprites running along the LED strip with different speed, overtaking each other

// sprites have color, position and speed (col, pos 0..65535, v)
// yet unused: delta v (dv)
// configuration: density, speed, brightness
// density defines the size of start ramp - a new sprite is born
// when the startup ramp has been left be the previous sprite.

uint16_t actdens;


// 1..16 => 4 .. 254
uint16_t sprites_speed(uint8_t s) {
  float td = 9.094 * exp (0.277 * (s+1));
  return td;
}

// 0..15 => 2 .. 32
uint16_t sprites_density(uint8_t s) {
  float td = 2.0 * exp (0.185 * s);
  return td;
}

void Sprites_Load(void)
{
  del = conf.speed;
  dens = conf.stype;
}

// initially just set some random sprites
void Sprites_Init(void)
{
  actdens = sprites_density(dens);
  uint16_t i, h, n = led_cnt/(actdens+3);
  sprites.iter = 0;
  memset (&sprites, 0, sizeof (sprites_t));
  // speed spread 
  sprites.s1 = sprites_speed(del);
  h = sprites.s1 >> 2;
  sprites.s2 = sprites.s1 + h;
  sprites.s1 -= h;
  for (i=0; i<n; i++) {
    sprites.col[i] = random(65535);
    sprites.pos[i] = (i << 16) / n;
    sprites.v[i] = random(sprites.s1, sprites.s2);
  }
  sprites.iter = i; // next free sprite
}

void Sprites(void)
{
  uint16_t i, cnt = led_cnt / 3, h, ramp;
  uint8_t coll=0; // 1 when a sprite is in the start ramp

  // update parameters
  if (re_param == 0x11 || wifi_param & 0x10) { // density
    if (dens > 15) dens = 15;
    conf.stype = dens;
    actdens = sprites_density(dens);
  }
  ramp=500*actdens;
  if (re_param == 0x12 || wifi_param & 0x08) { // acceleration
    if (del > 15) del = 15;
    conf.speed = del;
    sprites.s1 = sprites_speed(del);
    h = sprites.s1 >> 2;
    sprites.s2 = sprites.s1 + h;
    sprites.s1 -= h;
    for (i=0; i<cnt; i++) {
      if (sprites.v[i]) sprites.v[i] = random(sprites.s1, sprites.s2);
    }
  }

  strip.clear();
  for (i=0; i<cnt; i++)
  {
    if (!sprites.v[i]) continue;
    uint16_t pp = sprites.pos[i];
    uint16_t pn = pp + sprites.v[i]; // next position
    if (pn < pp) {  // overflow => end point reached
      sprites.v[i] = 0;
      continue;
    }
    if (pn < ramp) coll = 1; // check if start ramp is free
    sprites.pos[i] = pn; // move to next position
    uint32_t t = pn * led_cnt;
    uint8_t ci = (t >> 9) & 0x7f; // half wave
    uint8_t c1 = 64 + ci; // starting at peak
    uint8_t c2 = 192 + ci; // starting at valley
    t >>= 16;
    strip.addPixelColor(t, strip.ColorHSV (sprites.col[i], 255, strip.sine8(c1)));
    strip.addPixelColor(t+1, strip.ColorHSV (sprites.col[i], 255, strip.sine8(c2)));
  }
  if (!coll) { // start ramp free, start next one
    i = sprites.iter;
    sprites.col[i] = random(65535);
    sprites.v[i] = random(sprites.s1, sprites.s2);
    sprites.pos[i] = 0;
    while (sprites.v[i]) { // find next free sprite
      i++;
      if (i >= cnt) i = 0;
      if (i == sprites.iter) break;
    }
    sprites.iter = i;
  }
}

uint8_t Sprites_Param() {
  switch (re_selector) {
      case 1: re_val_ptr = &dens; re_max = 15; stateCol = 0x000000ff; break;
      case 2: re_val_ptr = &del; re_max = 15; stateCol = 0x0000ff00; break;
      case 3: re_val_ptr = &bri; re_max = 15; stateCol = 0x00ff0000; break;
      default: return 0;
  }
  return re_selector;
}

// ######################################################################

// effect speed (flickering)
// curve fitting 0 ... 15 => 5 ... 96 with y = a e^bx
uint16_t Fire_Delay(void) {
  float td = 5.0 * exp (0.197 * del);
  return td;
}

void Fire_Load(void)
{
  col = conf.cooling;
  dens = conf.sparks;
  del = conf.flicker;
  fire.del = Fire_Delay();
}

void Fire_Init(void)
{
  Fire_Load();
  fire.ts = now - fire.del;
}


void Fire(void)
{
  uint16_t i;

  // update parameters
  if (re_param == 0x11 || wifi_param & 0x04) { // cooling
    if (col > 15) col = 15;
    conf.cooling = col;
  }
  if (re_param == 0x12 || wifi_param & 0x08) { // flicker speed
    if (del > 15) del = 15;
    conf.flicker = del;
    fire.del = Fire_Delay();
  }
  if (re_param == 0x13 || wifi_param & 0x10) { // density
    if (dens > 15) dens = 15;
    conf.sparks = dens;
  }

  // just return when cycle time is not yet reached
  uint16_t d = now - fire.ts;
  if (d < fire.del) return;
  fire.ts = now;

  // cool down every cell a little
  for (i=0; i<led_cnt; i++) {
    uint8_t cooldown = getRandRange(col);
    if (cooldown > fire.heat[i]) fire.heat[i]=0;
    else fire.heat[i] = fire.heat[i]-cooldown;
  }
  // in each cycle, heat from each cell drifts 'up' and diffuses a little
  for (i=led_cnt-1; i>=2; i--) {
    fire.heat[i] = (fire.heat[i-1] + 2*fire.heat[i-2]) / 3;
  }
  // randomly ignite new 'sparks' near the bottom area, which is 2x dens
  for (i=0; i<dens; i++) {
    uint16_t y = getRandRange(dens * 2);
    fire.heat[y] += getRand() & 0x7f; // intentional overflow
  }
  // Convert heat to LED colors
  for (i=0; i<led_cnt; i++)
    strip.setPixelColor(i, getPixelHeatColor (fire.heat[i]));
}

uint8_t Fire_Param() {
  switch (re_selector) {
      case 1: re_val_ptr = &col; re_max = 15; stateCol = 0x000000ff; break;
      case 2: re_val_ptr = &del; re_max = 15; stateCol = 0x0000ff00; break;
      case 3: re_val_ptr = &dens; re_max = 15; stateCol = 0x00ff00ff; break;
      case 4: re_val_ptr = &bri; re_max = 15; stateCol = 0x00ff0000; break;
      default: return 0;
  }
  return re_selector;
}

// ######################################################################
// The floor is lava

void Lava_Load(void)
{
  col = conf.lavacool;
  dens = conf.heat;
  del = conf.lavaspeed;
  lava.del = Fire_Delay();
}

void Lava_Init(void)
{
  uint16_t i, cnt = led_cnt / 4;

  Lava_Load();
  lava.ts = now - lava.del;

  for (i=0; i<cnt; i++)
    lava.sparks[i] = random(led_cnt);
  for (i=0; i<led_cnt; i++)
    lava.heat[i] = getRand() >> 8;
  lava.spix = 0;
}

void Lava(void)
{
  uint32_t temp = 0;
  uint16_t i, cnt = led_cnt / 4;

  // update parameters
  if (re_param == 0x11 || wifi_param & 0x04) { // cooling
    if (col > 15) col = 15;
    conf.lavacool = col;
  }
  if (re_param == 0x12 || wifi_param & 0x08) { // lavaspeed
    if (del > 15) del = 15;
    conf.lavaspeed = del;
    lava.del = Fire_Delay();
  }
  if (re_param == 0x13 || wifi_param & 0x10) { // heat
    if (dens > 15) dens = 15;
    conf.heat = dens;
  }

  // just return when cycle time is not yet reached
  uint16_t d = now - lava.ts;
  if (d < lava.del) return;
  lava.ts = now;

  // Cool down every cell a little
  for (i=0; i<led_cnt; i++) {
    uint8_t cooldown = getRandRange(col);
    if (cooldown > lava.heat[i]) lava.heat[i]=0;
    else lava.heat[i] = lava.heat[i]-cooldown;
    temp += lava.heat[i];
  }

  // check temperature and sparks, when too low, add sparks
  temp = temp / led_cnt;
  if (temp < 100) for (i=0; i<cnt; i++) {
    uint16_t t = lava.heat[lava.sparks[i]] + dens;
    lava.heat[lava.sparks[i]] = t>255?255:t;
  }
  lava.sparks[lava.spix] = random(led_cnt);
  lava.spix++; if (lava.spix > cnt) lava.spix = 0;

  // heat from each cell diffuses a little
  for (i=1; i<led_cnt-1; i++) {
    lava.heat[i] = (lava.heat[i+1] + (lava.heat[i]<<1) + lava.heat[i-1]) >> 2;
  }
  // first and last LED get special treatment, include the first LED from the other side
  lava.heat[0] = (lava.heat[1] + (lava.heat[0]<<1) + lava.heat[led_cnt-1]) >> 2;
  lava.heat[led_cnt-1] = (lava.heat[0] + (lava.heat[led_cnt-1]<<1) + lava.heat[led_cnt-2]) >> 2;

  // Convert heat to LED colors
  for (i=0; i<led_cnt; i++)
    strip.setPixelColor(i, getPixelHeatColor (lava.heat[i]));
}

uint8_t Lava_Param() {
  switch (re_selector) {
      case 1: re_val_ptr = &col; re_max = 15; stateCol = 0x000000ff; break;
      case 2: re_val_ptr = &del; re_max = 15; stateCol = 0x0000ff00; break;
      case 3: re_val_ptr = &dens; re_max = 15; stateCol = 0x00ff00ff; break;
      case 4: re_val_ptr = &bri; re_max = 15; stateCol = 0x00ff0000; break;
      default: return 0;
  }
  return re_selector;
}



// ######################################################################
// was only written to distinguish between RGB and RBG chips

// void Test_Init(void)
// {
// }

// void Test(void)
// {
//   uint16_t i;
//   uint32_t hsv;
//   for (i=0; i<led_cnt; i++) {
//     switch (col) {
//       case 0: hsv = 0x000000ff; break;
//       case 1: hsv = 0x0000ff00; break;
//       case 2: hsv = 0x00ff0000; break;
//       case 3: hsv = 0x00ffffff; break;
//     }
//     strip.setPixelColor(i, hsv);
//   }
// }

// uint8_t Test_Param() {
//   switch (re_selector) {
//       case 1: re_val_ptr = &col; re_max = 3; stateCol = 0x000000ff; break;
//       case 2: re_val_ptr = &bri; re_max = 15; stateCol = 0x00ff0000; break;
//       default: return 0;
//   }
//   return re_selector;
// }

// ######################################################################
