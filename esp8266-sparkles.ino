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

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

#include "WrapUDP.h"
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

// D8 = GPIO15
Adafruit_NeoPixel state = Adafruit_NeoPixel (1, D8, NEO_GRBW);
// color values are in RGB
#define STAT_RED 0x00ff0000
#define STAT_VIOLET 0x00ff00ff
#define STAT_DIM_VIOLET 0x007f007f
#define STAT_DIM_YELLOW 0x007f7f00
#define STAT_GREEN 0x0000ff00
#define STAT_BLUE 0x000000ff
#define STAT_DIM_WHITE 0x003f3f3f
#define STAT_CYAN 0x0000ffff

// NEO_NOSPLIT: the full length is written to all 4 pins, which means the strips have identical data.
// NEO_SPLIT2: the first half of the array is written to pins 1 and 3, the seconed half is reversed in order
//      and written to pins 2 and 4. This results in having the full array on the strings with the controller in the middle.
// NEO_SPLIT4: the array is cut into 4 segments, and each segment is written to one pin.
// D3 => 16, D2 => 4, D1 => 5, D7 => 13
Adafruit_NeoPixel strip = Adafruit_NeoPixel (LED_CNT, D3, D2, D1, D7, LED_PIXT+NEO_NOSPLIT);
// note, _D3_ = GPIO0 is affected by some weird artifact.

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

void Test_Init(void);
uint8_t Test_Param(void);
uint16_t Test(void);

void loadParam(void);

// ######################################################################

// number of effects
#define NPROG 8

// this datastructure is stored in the EEPROM
// and holds all necessary config entries for all effects.
struct
{
  uint32_t magic, ctrid; // controller ID
  uint16_t len; // size of this struct
  uint8_t wifi, slen, srgb, split; // configuration of WiFi and LED strip/string
  // effect configurations
  uint8_t type, scol, sacc, ccs, cdel, cdens, speed, stype, density;
  uint8_t sparks, cooling, flicker;
  uint8_t lavacool, heat, lavaspeed;
  uint8_t rbspeed, rbspread;
  uint8_t bri[NPROG];
} conf;

#define EE_MAGIC 0x1eddaffe

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
  uint16_t off, speed;     // starting offset
  uint8_t walk, twin; // twin: toggle 02/13
  uint16_t val[MAX_COL];
  float grad[MAX_COL];
  uint8_t left[MAX_COL];
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
volatile uint8_t type=0, col=0, bri=3, del=0, dens=0, slen=0, srgb=0, split=0, wifimode=0;
volatile uint8_t re_max, *re_val_ptr, re_flag=0; // re_flag: flag for change
uint8_t re_param, re_debounce=0, re_selector=0;  // re_selector: parameter, re_param set to re_selector when re_flag=1
uint8_t re_click=0; // switch request, 1..click, 2..long click
uint8_t re_level = 1; // UI parameter level, 1..effects, 2..settings
uint16_t reqts; // ts of switch press
uint16_t wifi_param; // bitmap which parameters have been set from UDP/TCP packets
void re_read(void);
// cyclic parameters
uint16_t sparks_hue, colors_ts, colors_gts, colors_off;

// ######################################################################

IPAddress local_IP(192,168,100,1);
IPAddress gateway(192,168,100,1);
IPAddress subnet(255,255,255,0);
IPAddress bcast(192,168,100,255);
IPAddress act_bcast;

ESP8266WebServer server(80);
WrapUDP udp_endpoint;
#define UDP_PORT 5700
#define ALIVE_PKT_LEN 128

uint16_t ts_rec, ts_prev=0, ts_diff=0, ts_hist=0, tsa[8], tscnt=0;

void handleUDP(pbuf *pb);

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
    <br/><p>Programs and parameters c=color, s=speed, d=density, b=brightness:
    <br>p0: Stars c [0..H: 0=full,1=slow wheel,2..H:red-green-blue] s [0..8] d [0..F] b [0..F]
    <br>p1: Candles b [0..F]
    <br>p2: Duco c [0..5, 6-9=morphing] s [0=stop, 1..G] d [0..5] b [0..F]
    <br>p3: Sprites s [0..F] d [0..F] b [0..F]
    <br>p4: Fire c [0..F] s [0..F] d [0..F] b [0..F]
    <br>p5: Lava c [0..F] s [0..F] d [0..F] b [0..F]
    <br>p6: Rainbow s [0=stop, 1..F] d [0..O] b [0..F]
    <br>i<abcd1234>: 4 digit hex id + 4 digit strip mapping
    <br>L: Strip length, 0=100, 1=120, 2=181, 3=200
    <br>O: Order, 0=RGB, 1=GRB
    <br>X: Split, 0=A=B=C=D, 1=A-B+C-D, 2=ABCD
    <br>W: Wifi, 0=off, 1=klausb, 2=leo, 3=follow, 4=lead
    </p>
</body>
</html>
)rawliteral";

enum WiFiModes {
  WIFI_NONE,
  WIFI_KLAUS,
  WIFI_LEO,
  WIFI_FOLLOW,
  WIFI_LEAD
};
#define WIFI_MAX_PARM WIFI_LEAD
void wifi_config(uint8_t wm) {
  WiFi.disconnect(true);  // first reset
  server.stop();
  udp_endpoint.close();
  switch (wm) {
    case WIFI_NONE: break;
    case WIFI_KLAUS: WiFi.begin("klausb", "buskatze"); break;
    case WIFI_LEO: WiFi.begin("kanaan", "welcome2home"); break;
    case WIFI_FOLLOW: WiFi.begin("katzenwildbahn", "buskatze"); break;
    case WIFI_LEAD:
    // ssid, psk, channel, hidden, max_connection
    WiFi.softAP("katzenwildbahn", "buskatze", 1, false, 8);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    break;
  }
  if (wm == WIFI_LEAD) act_bcast = bcast;
  else act_bcast = WiFi.broadcastIP();
  if (wm) {
    server.begin(80);
    server.on("/", HTTP_GET, handleRoot);
    server.on("/set", HTTP_POST, handlePost);
    udp_endpoint.listen(wm==WIFI_FOLLOW ? UDP_PORT+1 : UDP_PORT);  // bind to port
  }
  udp_endpoint.onPacket(handleUDP);
  wifi_param = 0; // wifi_param bitmap holds which parameters have been set via WiFi
}

// callback for the webserver getting a request for the root page
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

// process a HTTP request or UDP packet:
// input is a string with letters followed by a single HEX digit,
// e.g. p0c0s5dFb4
void handleCommand(uint8_t *rt, uint16_t len) {
  uint32_t hexval=0;
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
        case 'p': cmd=0; if (cval != type) { wifi_param|=0x001; type = cval; } break;
        case 'b': cmd=0; if (cval != bri) { wifi_param|=0x002; bri = cval; } break;
        case 'c': cmd=0; if (cval != col) { wifi_param|=0x004; col = cval; } break;
        case 's': cmd=0; if (cval != del) { wifi_param|=0x008; del = cval; } break;
        case 'd': cmd=0; if (cval != dens) { wifi_param|=0x010; dens = cval; } break;
        // receive a new 8 HEX digit board ID
        case 'i': wifi_param|=0x020;
        hexval <<= 4; hexval += cval; idc++;
        if (idc >= 8) { conf.ctrid=hexval; idc=0; cmd=0; }
        break;
        // receive a new 4 HEX digit delta timestamp
        case 't': wifi_param|=0x040;
        hexval <<= 4; hexval += cval; idc++;
        if (idc >= 4) { ts_rec=hexval; hexval=0; idc=0; cmd=0; }
        break;
        case 'h':
        hexval <<= 4; hexval += cval; idc++;
        if (idc >= 4) { sparks_hue=hexval; hexval=0; idc=0; cmd=0; }
        break;
        case 'e':
        hexval <<= 4; hexval += cval; idc++;
        if (idc >= 4) { colors_ts=hexval; hexval=0; idc=0; cmd=0; }
        break;
        case 'g':
        hexval <<= 4; hexval += cval; idc++;
        if (idc >= 4) { colors_gts=hexval; hexval=0; idc=0; cmd=0; }
        break;
        case 'o': cmd=0; colors_off = cval; break;
        case 'L': cmd=0; if (cval != slen) { wifi_param|=0x080; slen = cval; } break;
        case 'O': cmd=0; if (cval != srgb) { wifi_param|=0x080; srgb = cval; } break;
        case 'X': cmd=0; if (cval != split) { wifi_param|=0x080; split = cval; } break;
        case 'W': cmd=0; if (cval != wifimode) { wifi_param|=0x100; wifimode = cval; } break;
      }
    }
  }
  if (wifi_param & 0x040) { // timestamp received => calculate most probable time
    ts_diff = ts_rec - now + 10;
    tsa[tscnt++] = (ts_rec - ts_prev) - (now - ts_hist);
    ts_prev = ts_rec;
    if (tscnt >= 8) {
      tscnt = 0;
    }
  }
}

// 0..9, a..z
char as_exthex(uint8_t v) {
  if (v < 10) return '0'+v;
  else return 87+v; // 'a'+v-10
}

// always broadcast the full configuration to peer nodes
void share_config() {
  pbuf* alivePkt = pbuf_alloc(PBUF_TRANSPORT, ALIVE_PKT_LEN, PBUF_RAM);
  uint8_t * pktptr = (uint8_t*)(alivePkt->payload);
  uint16_t wlen = snprintf((char*)pktptr, ALIVE_PKT_LEN, "cp%xb%xc%cs%cd%ct%04x",
      type, bri, as_exthex(col), as_exthex(del), as_exthex(dens), now);
  switch (type) {
    case 0: // sparks
    wlen += snprintf((char*)pktptr+wlen, ALIVE_PKT_LEN-wlen,
              "h%04x", sparks.hue);
    break;
    case 2: // duco
    wlen += snprintf((char*)pktptr+wlen, ALIVE_PKT_LEN-wlen,
              "e%04xg%04xo%x", colors.ts, colors.gts, colors.off);
    break;
  }
  alivePkt->len = wlen;
  alivePkt->tot_len = wlen;
  udp_endpoint.writeTo(alivePkt, act_bcast, UDP_PORT+1);
  pbuf_free(alivePkt);
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
  if (type != 255) { // first UDP packet => reconfigure strip
    type = 255;
    strip.clear();
    strip.setBrightness(255); // server does all brightness scaling
    split = 2; // set strip config to NEO_SPLIT4
    strip_config();
  }
  if (len > led_cnt) len = led_cnt;
  // the map defines for which pin the data shall go
  // it is a set of 4 hex nibbles
  while (map) {
    if (map & cmd & 0x0f) memcpy (pix, rt, len);
    pix += led_cnt;
    map >>= 4;
  }
  if (cmd & 0x10) {   // bit 's' is set => show 
    alive_tim = now;  // update alive flag, data has been received
    strip.show();
  }
}

// process the pbuf we got from udp_recv callback
// the pbuf is returned in the callback
void handleUDP(pbuf *pb) {
  uint8_t * recPkt = (uint8_t*)(pb->payload);
  switch (recPkt[0]) {
      case 'c': handleCommand(recPkt+1, pb->len-1); break;
      case 's': handleSync(recPkt+1, pb->len-1); break;
      case 'a': break; // ignore, alive packets are meant for the server
      default: handleBinary(recPkt, pb->len);
  }
}

// called at every loop cycle when WiFi is connected:
// process HTTP server requests
// when UDP LED packets have been received,
// we check if the data stream is still alive, if not, switch back
// to the last program, and send out broadcast packets with our ID.
void handleIP() {
  server.handleClient();
  d = now - alive_tim;
  if (d > 1000 && wifimode == WIFI_LEAD) {
    share_config();  // send out config and timestamp every second
    alive_tim = now;
  }
  else if (d > 2000 && wifimode<WIFI_FOLLOW) {
    // the "alive" packet is broadcasted to port +1 and shares the controller ID,
    // the server then can collect controller IDs and corresponding IP addresses
    pbuf* alivePkt = pbuf_alloc(PBUF_TRANSPORT, ALIVE_PKT_LEN, PBUF_RAM);
    uint8_t * pktptr = (uint8_t*)(alivePkt->payload);
    uint16_t wlen = snprintf((char*)pktptr, ALIVE_PKT_LEN, "a%08x", conf.ctrid);
    alivePkt->len = wlen;
    alivePkt->tot_len = wlen;
    udp_endpoint.writeTo(alivePkt, act_bcast, UDP_PORT+1);
    pbuf_free(alivePkt);
    alive_tim = now;
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

// NEO_NOSPLIT: the full length is written to all 4 pins,
//     which means the strips have identical data.
// NEO_SPLIT2: the first half of the array is written to pins 1 and 3,
//     the seconed half is reversed in order and written to pins 2 and 4.
//     This results in having the full array on the strings
//     with the controller in the middle.
// NEO_SPLIT4: the array is cut into 4 segments,
//     and each segment is written to a different pin.

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
  alive_tim = 0;
  inacnt = 0;
  memset(tsa, 0, sizeof(tsa));

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
    conf.wifi = 0;
    conf.slen = 0; // 0=100, 1=120, 2=181, 3=200
    conf.srgb = 0; // 0=RGB for string, 1=GRB for strip
    conf.split = 0; // 0=NOSPLIT, 1=SPLIT2, 2=SPLIT4
    conf.type = 3; // effect
    conf.scol = 0; // stars color
    conf.sacc = 4; // stars acceleration
    conf.density = 15; // stars density
    conf.ccs = 0; // duco color subtype
    conf.cdel = 4; // duco delay
    conf.cdens = 0; // duco delay spread
    for (d=0; d<NPROG; d++) conf.bri[d] = 3;
    conf.speed = 4; // sprite speed
    conf.stype = 3; // sprite density
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
  wifimode = conf.wifi;
  strip_config();
  wifi_config(wifimode);
  paramsel(0);
  proginit(1);
  wifi_param = 0;
  re_param = 0;
  // rotary encoder uses interrupts
  attachInterrupt (D5, re_read, CHANGE);
  attachInterrupt (D6, re_read, CHANGE);
  base = millis();
  ts_rec = base & 0x0000ffff;
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
      case 7: re_selector = Test_Param(); break;
    }
    if (re_selector == 0) { // no parameter => select prog type
      stateCol = STAT_DIM_WHITE; // dim wite
      re_val_ptr = &type; re_max = NPROG-1; // select the effect
    }
    break;
    case 2: // set string/strip config
    if (re_selector > 3) re_selector = 0;
    switch (re_selector) {
      case 0: re_val_ptr = &slen; re_max = 3; stateCol = STAT_BLUE; break;
      case 1: re_val_ptr = &srgb; re_max = 1; stateCol = STAT_GREEN; break;
      case 2: re_val_ptr = &split; re_max = 2; stateCol = STAT_VIOLET; break;
      case 3: re_val_ptr = &wifimode; re_max = WIFI_MAX_PARM; stateCol = STAT_RED; break;
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
    case 7: break;
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
    case 7: Test_Init(); break;
  }
}

// main loop
// - check timer
// - updated values from the rotary encoder,
// - build user interface menu
// - call effects program
// - handle UI and time steps

void loop() {
  ts_hist = now;
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
        case 1: stateCol = STAT_DIM_YELLOW; break;
        case 2: stateCol = STAT_DIM_VIOLET; break;
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
  // check TCP; UDP are processed when arrived
  if (WiFi.status() == WL_CONNECTED || wifimode==WIFI_LEAD) handleIP();
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
  if (re_param == 0x10 || wifi_param & 0x001) {   // new program type
    if (type != 255) conf.type = type;
    proginit(!wifi_param); // don't load parameters if from wifi
  }
  if (re_param == 0x23 || wifi_param & 0x100) { // wifi mode
    conf.wifi = wifimode;
    paramcol();
    wifi_config(wifimode);
  }
  if (re_param || wifi_param & 0x002) { // any other parameter
    // store brightness, all other parameter are handled in their effect context
    if (type < NPROG) conf.bri[type] = bri;
    strip.setBrightness(convertBrightness());
  }
  if ((re_param & 0xf0) == 0x20 || wifi_param & 0x080) { // configure strip, see handleCommand
    conf.slen = slen;
    conf.srgb = srgb;
    conf.split = split;
    paramcol();
    strip_config();
    proginit(1);
  }
// #### LED strip calculations except when type = 255, udp to pixel ####
  if (type != 255) {
    // finally we actually run our effect programs:
    d = 1;
    switch (type) {
      case 0: Stars(); break;
      case 1: Candles(); break;
      case 2: Duco(); break;
      case 3: Sprites(); break;
      case 4: Fire(); break;
      case 5: Lava(); break;
      case 6: Rainbow(); break;
      case 7: d = Test(); break;
    }
    if (d) strip.show(); // takes ~6 ms
  }
  // share parameters after effect processing as some time values are updated there
  if (re_param & 0x10 && wifimode == WIFI_LEAD) share_config();
  wifi_param = 0; // WiFi params processed
  re_param = 0;
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
  uint8_t init = 0;

  // parameter update
  if (wifi_param & 0x040) {
    sparks.hue = sparks_hue; // color wheel walk, just take over value from leading node
  }
  if (re_param == 0x11 || wifi_param & 0x004) { // color wheel
    if (col > 17) col = 17;
    Stars_DispCol(col);
    conf.scol = col;
  }
  if (re_param == 0x12 || wifi_param & 0x008) { // acceleration
    if (del > 8) del = 8;
    conf.sacc = del;
  }
  if (re_param == 0x13 || wifi_param & 0x010) { // density
    if (dens > 15) dens = 15;
    conf.density = dens;
    // with new program type, _Init() is already done
    if (! wifi_param & 0x001) Stars_Init();
    init = 1;
  }
  // update all stars
  if (!init) for (i=0; i<dcnt; i++)
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
      case 1: re_val_ptr = &col; re_max = 17; stateCol = STAT_VIOLET; Stars_DispCol(col); break;
      case 2: re_val_ptr = &del; re_max = 8; stateCol = STAT_GREEN; break;
      case 3: re_val_ptr = &dens; re_max = 15; stateCol = STAT_BLUE; break;
      case 4: re_val_ptr = &bri; re_max = 15; stateCol = STAT_RED; break;
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
      case 1: re_val_ptr = &bri; re_max = 15; stateCol = STAT_RED; break;
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
  float td = 7.155 * exp (0.334 * (17-del));
  return td;
}

// the gradient is used to walk through the color wheel.
// a = color to start with, b = color to walk to.
// left=true: take the left side of the color wheel
float getGradient(int32_t a, int32_t b, uint8_t left) {
  int32_t d = b-a;
  if (left) d = (d > 0) ? -(65526-d) : 65536+d;
  return (float)d/colors.speed;
}

// set new colors and calculate a gradient with the previous color
// for each of the MAX_COL colors.
// the current implementation uses a fixed number of parameters
// whereas it should be MAX_COL... future improvement.
void Duco_Set(uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
  uint8_t i = 0;
  colors.grad[i] = getGradient(a, colors.val[i], colors.left[i]);
  colors.left[i] = false;
  colors.val[i++] = a;
  colors.grad[i] = getGradient(b, colors.val[i], colors.left[i]);
  colors.left[i] = false;
  colors.val[i++] = b;
  colors.grad[i] = getGradient(c, colors.val[i], colors.left[i]);
  colors.left[i] = false;
  colors.val[i++] = c;
  colors.grad[i] = getGradient(d, colors.val[i], colors.left[i]);
  colors.left[i] = false;
  colors.val[i++] = d;
  colors.walk = 0;
  colors.twin = 0;
}

void Duco_DensSet() {
  switch (dens) {
    case 0: colors.speed = 3000; break;
    case 1: colors.speed = 6000; break;
    case 2: colors.speed = 10000; break;
    case 3: colors.speed = 16000; break;
    case 4: colors.speed = 24000; break;
    case 5: colors.speed = 32000; break; // max value as *2 needs to be < 65k
  }
}

void Duco_DirSet(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  uint8_t i = 0;
  colors.left[i++] = a;
  colors.left[i++] = b;
  colors.left[i++] = c;
  colors.left[i++] = d;
}

#define DUCO_MAX 9
void Duco_Select(uint8_t i) {
  switch (i) {
    case 0: Duco_Set (0, 10923, 41000, 21845 ); break; // red, yellow, light blue, green (shift)
    case 1: Duco_Set (45000, 56000, 62000, 50000); break; // magenta/violett (shift)
    case 2: Duco_Set (3000, 8000, 14000, 21845); break; // orange-green (shift)
    case 3: Duco_Set (23000, 29000, 37000, 43000); break; // green-turquiose (shift)
    case 4: Duco_Set (52000, 45000, 65500, 60000); break; // red-violet-blue (shift)
    case 5: Duco_Set (0, 43690, 10923, 21845 ); colors.twin = 1; break; // toggle red, blue <=> yellow, green
    case 6: Duco_Select(0); Duco_Select(1); colors.walk = 1; break; // shift and color morphing
    case 7: Duco_Select(2); Duco_Select(3); colors.walk = 1; break; // shift and color morphing
    case 8: Duco_Select(2); Duco_Select(4); colors.walk = 1; break; // shift and color morphing
    case 9: Duco_Set (54613, 21845, 32768, 43690);
            Duco_DirSet (true, false, false, true);
            Duco_Set (10923, 43690, 54613, 0);
            colors.walk = 1;
            break;
  }
  if (wifi_param & 0x040) { // sync only when the new type is set
    colors.gts = colors_gts;
    colors.ts = colors_ts;
    colors.off = colors_off;
  } else {
    colors.gts = now;
    colors.ts = now;
    colors.off = 0;   
  }
}

void Duco_Load()
{
  col = conf.ccs;
  del = conf.cdel;
  dens = conf.cdens;
  Duco_DensSet();
}

void Duco_Init()
{
  Duco_Load();
  colors.del = Duco_Delay();
  colors.ts = now - colors.del; // when the next shift is due
  colors.gts = now;
  colors.off = 0;
  Duco_Select(col);
}

void Duco()
{
  uint16_t v=0, i, c = colors.off, tnow = now + ts_diff;
  // update parameters
  if (re_param == 0x11 || wifi_param & 0x004) { // color selection
    if (col > DUCO_MAX) col = DUCO_MAX;
    conf.ccs = col;
    v = 1; // reload color settings
  }
  if (re_param == 0x12 || wifi_param & 0x008) { // speed, 0=stop, 1..16
    if (del > 16) del = 16;
    conf.cdel = del;
    colors.del = Duco_Delay();
  }
  if (re_param == 0x13 || wifi_param & 0x010) { // speed variation
    if (dens > 5) dens = 5;
    conf.cdens = dens;
    Duco_DensSet();
    v = 1; // reload color settings
  }
  if (v) Duco_Select(col);
  // calculate the position on the gradient depending on time and speed
  uint16_t gd = tnow - colors.gts, gul = colors.speed*2;
  if (gd >= gul) colors.gts += gul, gd -= gul; // reached limit, new circle
  else if (gd > colors.speed) gd = gul - gd; // in the second half we walk back

  uint16_t d = tnow - colors.ts;
  if (del && d >= colors.del) {   // update was triggered timeout
    colors.ts = tnow;
    colors.off++; // advance offset
    if (colors.off == MAX_COL) colors.off = 0;
  }
  for (i=0; i<led_cnt; i++) {
    v = colors.val[c];
    if (colors.walk) v += (uint16_t)(colors.grad[c] * gd); // gradient handling
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
      case 1: re_val_ptr = &col; re_max = DUCO_MAX; stateCol = STAT_VIOLET; break;
      case 2: re_val_ptr = &del; re_max = 16; stateCol = STAT_GREEN; break;
      case 3: re_val_ptr = &dens; re_max = 5; stateCol = STAT_BLUE; break;
      case 4: re_val_ptr = &bri; re_max = 15; stateCol = STAT_RED; break;
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
  if (re_param == 0x11 || wifi_param & 0x008) { // speed, 0=stop, 1..16
    if (del > 16) del = 16;
    rainbow.stsz = Rainbow_Step();
    conf.rbspeed = del;
  }
  if (re_param == 0x12 || wifi_param & 0x010) { // color spread factor
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
      case 1: re_val_ptr = &del; re_max = 16; stateCol = STAT_GREEN; break;
      case 2: re_val_ptr = &dens; re_max = 31; stateCol = STAT_BLUE; break;
      case 3: re_val_ptr = &bri; re_max = 15; stateCol = STAT_RED; break;
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
  if (re_param == 0x11 || wifi_param & 0x010) { // density
    if (dens > 15) dens = 15;
    conf.stype = dens;
    actdens = sprites_density(dens);
  }
  ramp=500*actdens;
  if (re_param == 0x12 || wifi_param & 0x008) { // acceleration
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
      case 1: re_val_ptr = &dens; re_max = 15; stateCol = STAT_BLUE; break;
      case 2: re_val_ptr = &del; re_max = 15; stateCol = STAT_GREEN; break;
      case 3: re_val_ptr = &bri; re_max = 15; stateCol = STAT_RED; break;
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
  if (re_param == 0x11 || wifi_param & 0x004) { // cooling
    if (col > 15) col = 15;
    conf.cooling = col;
  }
  if (re_param == 0x12 || wifi_param & 0x008) { // flicker speed
    if (del > 15) del = 15;
    conf.flicker = del;
    fire.del = Fire_Delay();
  }
  if (re_param == 0x13 || wifi_param & 0x010) { // density
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
      case 1: re_val_ptr = &col; re_max = 15; stateCol = STAT_BLUE; break;
      case 2: re_val_ptr = &del; re_max = 15; stateCol = STAT_GREEN; break;
      case 3: re_val_ptr = &dens; re_max = 15; stateCol = STAT_VIOLET; break;
      case 4: re_val_ptr = &bri; re_max = 15; stateCol = STAT_RED; break;
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
  if (re_param == 0x11 || wifi_param & 0x004) { // cooling
    if (col > 15) col = 15;
    conf.lavacool = col;
  }
  if (re_param == 0x12 || wifi_param & 0x008) { // lavaspeed
    if (del > 15) del = 15;
    conf.lavaspeed = del;
    lava.del = Fire_Delay();
  }
  if (re_param == 0x13 || wifi_param & 0x010) { // heat
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
      case 1: re_val_ptr = &col; re_max = 15; stateCol = STAT_BLUE; break;
      case 2: re_val_ptr = &del; re_max = 15; stateCol = STAT_GREEN; break;
      case 3: re_val_ptr = &dens; re_max = 15; stateCol = STAT_VIOLET; break;
      case 4: re_val_ptr = &bri; re_max = 15; stateCol = STAT_RED; break;
      default: return 0;
  }
  return re_selector;
}



// ######################################################################
// was only written to distinguish between RGB and RBG chips

void Test_Init(void)
{
  col = 0;
  if (bri > 8) bri = 8;
}

uint16_t Test(void)
{
  uint16_t i;
  uint32_t hsv;
  for (i=0; i<led_cnt; i++) {
    switch (col) {
      case 0: hsv = STAT_BLUE; break;
      case 1: hsv = STAT_GREEN; break;
      case 2: hsv = STAT_RED; break;
      case 3: hsv = 0x00ffffff; break;
      case 4: hsv = 0x00000000; break;
    }
    strip.setPixelColor(i, hsv); // RGB
  }
  return re_param; // update at parameter change
}

uint8_t Test_Param() {
  switch (re_selector) {
      case 1: re_val_ptr = &col; re_max = 4; stateCol = STAT_BLUE; break;
      case 2: re_val_ptr = &bri; re_max = 8; stateCol = STAT_RED; break;
      default: return 0;
  }
  return re_selector;
}

// ######################################################################
