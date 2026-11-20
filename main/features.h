// Klimasteuerung feature block (v1.21): measurements from the unit, history
// that survives restarts, external room sensor, open-window detection,
// scenes and sun-based schedule times.
//
// Included once from main.cpp (single translation unit), after the helpers it
// builds on: hpBaseSettings(), nightSendSettings(), ksRequestOk(), apiAuthOk().
#pragma once

// ---------------------------------------------------------------------------
// measurements decoded from the raw CN105 replies
// ---------------------------------------------------------------------------
// The status reply (0x06) carries input power and an energy counter on units
// that support it (layout as used by MitsubishiCN105ESPHome: data[5..6] = W,
// data[7..8] = kWh * 10). The outside air temperature in the room temperature
// reply (0x03, data[5], (v - 128) / 2) is not confirmed for every model, so a
// value is only taken when it is plausible. Raw bytes are kept for the
// diagnosis page.
int16_t meas_power_w = -1;   // -1 = not reported
float meas_kwh = -1;         // energy counter of the unit, -1 = not reported
float meas_out = -100;       // outside air temperature (celsius), -100 = not reported
uint8_t meas_raw03[10] = {0};
uint8_t meas_raw06[10] = {0};
bool meas_seen03 = false;
bool meas_seen06 = false;

static void measDecode(const byte *packet, unsigned int length)
{
  // callback layout: [0..4] header, [5] = data[0] (reply type), [6+i] = data[i+1]
  if (length < 16 || packet[1] != 0x62)
    return;
  if (packet[5] == 0x06)
  {
    memcpy(meas_raw06, packet + 6, sizeof(meas_raw06));
    meas_seen06 = true;
    uint16_t w = ((uint16_t)packet[10] << 8) | packet[11];
    uint16_t e = ((uint16_t)packet[12] << 8) | packet[13];
    // units without these fields send zeros: keep "not reported" until a
    // non-zero value shows up once
    if (w > 0 && w <= 8000)
      meas_power_w = (int16_t)w;
    else if (w == 0 && meas_power_w >= 0)
      meas_power_w = 0;
    if (e > 0 && e != 0xFFFF)
      meas_kwh = e / 10.0f;
  }
  else if (packet[5] == 0x03)
  {
    memcpy(meas_raw03, packet + 6, sizeof(meas_raw03));
    meas_seen03 = true;
    uint8_t v = packet[10];
    if (v > 1)
    {
      float t = (v - 128) / 2.0f;
      if (t > -40 && t < 60)
        meas_out = t;
    }
  }
}

// ---------------------------------------------------------------------------
// history: outside temperature and energy next to the room temperature rings,
// stored in flash so a restart or update keeps the curve
// ---------------------------------------------------------------------------
int8_t histOut[HIST_LEN];    // outside temperature * 2, HIST_NONE = no sample
int8_t hist7Out[HIST7_LEN];
uint8_t hist7E[HIST7_LEN];   // energy per 30-minute slot in 0.1 kWh
float hist_kwh_mark = -1;    // counter value at the last 30-minute slot
bool hist_gap_pending = false; // restored from flash, waiting for the clock to place the gap
time_t hist_saved_epoch = 0;
unsigned long hist_boot_ms = 0;
#ifdef ESP32
#define HIST_FILE "/hist.bin"
#else
#define HIST_FILE "hist.bin"
#endif
#define HIST_MAGIC 0x4B534832UL // "KSH2"

struct HistHeader
{
  uint32_t magic;
  uint32_t epoch; // wall clock of the last sample, 0 = clock was not set
  uint16_t head;
  uint16_t head7;
  uint8_t ticks;
  uint8_t oncnt;
  uint8_t cnt7;
  uint8_t reserved;
  float sum7;
  float kwhMark;
};

void histSave()
{
  if (!hist_ready)
    return;
  File f = SPIFFS.open(HIST_FILE, "w");
  if (!f)
    return;
  HistHeader h;
  memset(&h, 0, sizeof(h));
  h.magic = HIST_MAGIC;
  time_t now;
  time(&now);
  h.epoch = now >= min_valid_date ? (uint32_t)now : 0;
  h.head = hist_head;
  h.head7 = hist7_head;
  h.ticks = hist7_ticks;
  h.oncnt = hist7_oncnt;
  h.cnt7 = hist7_cnt;
  h.sum7 = hist7_sum;
  h.kwhMark = hist_kwh_mark;
  f.write((const uint8_t *)&h, sizeof(h));
  f.write((const uint8_t *)hist, sizeof(hist));
  f.write((const uint8_t *)histOn, sizeof(histOn));
  f.write((const uint8_t *)hist7, sizeof(hist7));
  f.write((const uint8_t *)hist7On, sizeof(hist7On));
  f.write((const uint8_t *)histOut, sizeof(histOut));
  f.write((const uint8_t *)hist7Out, sizeof(hist7Out));
  f.write((const uint8_t *)hist7E, sizeof(hist7E));
  f.close();
}

// true when a stored history was loaded into the rings
static bool histLoad()
{
  if (!SPIFFS.exists(HIST_FILE))
    return false;
  File f = SPIFFS.open(HIST_FILE, "r");
  if (!f)
    return false;
  const size_t want = sizeof(HistHeader) + sizeof(hist) + sizeof(histOn) + sizeof(hist7) + sizeof(hist7On) +
                      sizeof(histOut) + sizeof(hist7Out) + sizeof(hist7E);
  HistHeader h;
  bool ok = f.size() == want && f.read((uint8_t *)&h, sizeof(h)) == sizeof(h) && h.magic == HIST_MAGIC &&
            h.head < HIST_LEN && h.head7 < HIST7_LEN;
  if (ok)
  {
    f.read((uint8_t *)hist, sizeof(hist));
    f.read((uint8_t *)histOn, sizeof(histOn));
    f.read((uint8_t *)hist7, sizeof(hist7));
    f.read((uint8_t *)hist7On, sizeof(hist7On));
    f.read((uint8_t *)histOut, sizeof(histOut));
    f.read((uint8_t *)hist7Out, sizeof(hist7Out));
    f.read((uint8_t *)hist7E, sizeof(hist7E));
    hist_head = h.head;
    hist7_head = h.head7;
    hist7_ticks = h.ticks < 6 ? h.ticks : 0;
    hist7_oncnt = h.oncnt;
    hist7_cnt = h.cnt7;
    hist7_sum = h.sum7;
    hist_kwh_mark = h.kwhMark;
    hist_saved_epoch = (time_t)h.epoch;
  }
  f.close();
  return ok;
}

// the device was off for a while: move both rings forward by the missing time
// so the gap shows up as a gap instead of gluing the old curve to the new one
static void histSkip(uint32_t seconds)
{
  uint32_t n5 = seconds / 300UL;
  if (n5 > HIST_LEN)
    n5 = HIST_LEN;
  for (uint32_t i = 0; i < n5; i++)
  {
    hist[hist_head] = HIST_NONE;
    histOut[hist_head] = HIST_NONE;
    histOn[hist_head / 8] &= (uint8_t)~(1 << (hist_head % 8));
    hist_head = (hist_head + 1) % HIST_LEN;
  }
  uint32_t n30 = seconds / 1800UL;
  if (n30 > HIST7_LEN)
    n30 = HIST7_LEN;
  for (uint32_t i = 0; i < n30; i++)
  {
    hist7[hist7_head] = HIST_NONE;
    hist7Out[hist7_head] = HIST_NONE;
    hist7On[hist7_head] = 0;
    hist7E[hist7_head] = 0;
    hist7_head = (hist7_head + 1) % HIST7_LEN;
  }
  if (n30 > 0)
  {
    hist7_ticks = 0;
    hist7_oncnt = 0;
    hist7_cnt = 0;
    hist7_sum = 0;
    hist_kwh_mark = -1; // do not book the whole outage into one slot
  }
}

// energy of the last 24 hours / 7 days from the slot ring, -1 when the unit reports none
static float histEnergy(uint16_t slots)
{
  if (meas_kwh < 0)
    return -1;
  uint16_t sum = 0;
  for (uint16_t n = 0; n < slots && n < HIST7_LEN; n++)
  {
    uint16_t idx = (hist7_head + HIST7_LEN - 1 - n) % HIST7_LEN;
    sum += hist7E[idx];
  }
  return sum / 10.0f;
}

// ---------------------------------------------------------------------------
// external room sensor
// ---------------------------------------------------------------------------
// mode 0: sensor of the indoor unit, 1: fetch a URL every minute (another
// Klimasteuerung unit, a Shelly, anything that answers with JSON or a plain
// number), 2: a sensor pushes its value to /api/rtemp?k=<key>&t=<celsius>.
uint8_t rsens_mode = 0;
char rsens_url[100] = "";
char rsens_key[9] = "";
uint16_t rsens_timeout_min = 120;
float rsens_value = 0;        // celsius
bool rsens_valid = false;     // a value is in use
bool rsens_dirty = false;     // new value waiting to be sent to the unit
unsigned long rsens_ms = 0;   // when the value arrived
unsigned long rsens_sent_ms = 0;
unsigned long rsens_poll_ms = 0;
#ifdef ESP32
#define KS_FS_PREFIX "/"
#else
#define KS_FS_PREFIX ""
#endif
#define RSENS_FILE KS_FS_PREFIX "rsens.json"

static void rsensNewKey()
{
#ifdef ESP32
  uint32_t r = esp_random();
#else
  uint32_t r = ESP.random();
#endif
  snprintf(rsens_key, sizeof(rsens_key), "%08x", (unsigned int)r);
}

bool loadRsens()
{
  if (SPIFFS.exists(RSENS_FILE))
  {
    File f = SPIFFS.open(RSENS_FILE, "r");
    if (f)
    {
      StaticJsonDocument<384> doc;
      if (!deserializeJson(doc, f))
      {
        uint8_t m = doc["m"] | 0;
        rsens_mode = m <= 2 ? m : 0;
        strlcpy(rsens_url, doc["u"] | "", sizeof(rsens_url));
        strlcpy(rsens_key, doc["k"] | "", sizeof(rsens_key));
        uint16_t to = doc["to"] | 120;
        rsens_timeout_min = (to >= 5 && to <= 1440) ? to : 120;
      }
      f.close();
    }
  }
  if (strlen(rsens_key) != 8)
    rsensNewKey();
  return true;
}

bool saveRsens()
{
  StaticJsonDocument<384> doc;
  doc["m"] = rsens_mode;
  doc["u"] = rsens_url;
  doc["k"] = rsens_key;
  doc["to"] = rsens_timeout_min;
  File f = SPIFFS.open(RSENS_FILE, "w");
  if (!f)
    return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

// accept a reading (celsius); the unit is told from the main loop
static bool rsensTake(float c)
{
  if (!(c > 0 && c < 45))
    return false;
  rsens_value = roundf(c * 2.0f) / 2.0f;
  rsens_ms = millis();
  rsens_valid = true;
  rsens_dirty = true;
  return true;
}

// find a temperature in a reply: a plain number, or the first of the usual
// JSON keys ("room" = another Klimasteuerung unit, "tC" = Shelly)
static bool rsensParse(const String &body, float &out)
{
  static const char *const keys[] = {"\"room\":", "\"tC\":", "\"temperature\":", "\"Temperature\":", "\"temp\":"};
  for (uint8_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
  {
    int p = body.indexOf(keys[i]);
    if (p >= 0)
    {
      out = body.substring(p + strlen(keys[i])).toFloat();
      return true;
    }
  }
  String t = body;
  t.trim();
  if (t.length() > 0 && t.length() < 8 && (isdigit((unsigned char)t.charAt(0)) || t.charAt(0) == '-'))
  {
    out = t.toFloat();
    return true;
  }
  return false;
}

void checkRemoteSensor()
{
  if (rsens_mode == 0)
  {
    if (rsens_valid)
    {
      rsens_valid = false;
      if (hp.isConnected())
        hp.setRemoteTemperature(0); // back to the unit's own sensor
    }
    return;
  }
  if (rsens_mode == 1 && rsens_url[0] != '\0' && WiFi.status() == WL_CONNECTED && (rsens_poll_ms == 0 || millis() - rsens_poll_ms > 60000UL))
  {
    rsens_poll_ms = millis();
    WiFiClient client;
    HTTPClient http;
    http.setTimeout(2500);
    if (http.begin(client, rsens_url))
    {
      // only short replies with a known length: a large page must not eat the heap
      if (http.GET() == 200 && http.getSize() > 0 && http.getSize() <= 4096)
      {
        String body = http.getString();
        float v;
        if (rsensParse(body, v))
        {
          // a linked unit answers in its display scale; everything else is celsius
          if (body.indexOf("\"scale\":\"F\"") >= 0)
            v = (v - 32.0f) / 1.8f;
          rsensTake(v);
        }
      }
      http.end();
    }
  }
  if (!rsens_valid || !hp.isConnected())
    return;
  if (millis() - rsens_ms > (unsigned long)rsens_timeout_min * 60000UL)
  {
    // the sensor went silent: do not regulate on a stale value
    rsens_valid = false;
    hp.setRemoteTemperature(0);
    return;
  }
  // tell the unit on every new value, and repeat it every five minutes
  if (rsens_dirty || millis() - rsens_sent_ms > 300000UL)
  {
    rsens_dirty = false;
    rsens_sent_ms = millis();
    hp.setRemoteTemperature(rsens_value);
  }
}

// push endpoint for sensors: GET or POST /api/rtemp?k=<key>&t=<celsius>
void handleApiRtemp(AsyncWebServerRequest *request)
{
  if (!ksRequestOk(request))
    return;
  if (rsens_mode != 2 || !request->hasArg(F("k")) || request->arg(F("k")) != rsens_key)
  {
    request->send(403, "application/json", String(F("{\"err\":\"key\"}")));
    return;
  }
  String t = request->hasArg(F("t")) ? request->arg(F("t")) : request->arg(F("tC"));
  t.replace(',', '.');
  if (t.length() == 0 || !rsensTake(t.toFloat()))
  {
    request->send(400, "application/json", String(F("{\"err\":\"value\"}")));
    return;
  }
  request->send(200, "application/json", String(F("{\"ok\":true}")));
}

void handleApiRsens(AsyncWebServerRequest *request)
{
  if (!apiAuthOk(request))
    return;
  if (request->hasArg(F("cfg")))
  {
    uint8_t m = (uint8_t)request->arg(F("m")).toInt();
    rsens_mode = m <= 2 ? m : 0;
    String u = request->arg(F("u"));
    u.trim();
    if (u.length() > 0 && !u.startsWith("http://"))
      u = String(F("http://")) + u;
    // a bare address means another Klimasteuerung unit
    if (u.length() > 7 && u.indexOf('/', 7) < 0)
      u += F("/api/status");
    strlcpy(rsens_url, u.c_str(), sizeof(rsens_url));
    uint16_t to = (uint16_t)request->arg(F("to")).toInt();
    if (to >= 5 && to <= 1440)
      rsens_timeout_min = to;
    if (request->hasArg(F("newkey")))
      rsensNewKey();
    rsens_poll_ms = 0;
    bool ok = saveRsens();
    request->send(ok ? 200 : 500, "application/json", String(ok ? F("{\"ok\":true}") : F("{\"err\":\"save\"}")));
    return;
  }
  String json;
  json.reserve(260);
  json += F("{\"m\":");
  json += rsens_mode;
  json += F(",\"u\":\"");
  json += rsens_url;
  json += F("\",\"k\":\"");
  json += rsens_key;
  json += F("\",\"to\":");
  json += rsens_timeout_min;
  json += F(",\"valid\":");
  json += rsens_valid ? F("true") : F("false");
  json += F(",\"v\":");
  json += String(convertCelsiusToLocalUnit(rsens_value, useFahrenheit), 1);
  json += F(",\"age\":");
  json += rsens_valid ? (unsigned long)((millis() - rsens_ms) / 60000UL) : 0UL;
  json += F("}");
  request->send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// open-window detection
// ---------------------------------------------------------------------------
// mode 0: off, 1: only show a hint, 2: pause and carry on by itself,
// 3: switch off and stay off.
uint8_t win_mode = 0;
float win_threshold = 1.5f;   // degrees celsius within win_minutes
uint8_t win_minutes = 5;
uint8_t win_pause_min = 30;   // longest pause in mode 2
uint8_t win_state = 0;        // 0 = closed, 1 = open (hint), 2 = open and paused/switched off
#define WIN_SAMPLES 30        // one sample every 30 s = 15 minutes
int8_t win_ring[WIN_SAMPLES];
uint8_t win_head = 0;
uint8_t win_count = 0;
float win_extreme = 0;        // lowest (heating) / highest (cooling) reading since it opened
bool win_heating = true;
unsigned long win_open_ms = 0;
unsigned long win_check_ms = 0;
unsigned long win_settle_ms = 0; // last power/mode change: no judging right after it
char win_prev_power[8] = "";
char win_prev_mode[9] = "";
#define WIN_FILE KS_FS_PREFIX "window.json"

bool loadWindow()
{
  if (!SPIFFS.exists(WIN_FILE))
    return false;
  File f = SPIFFS.open(WIN_FILE, "r");
  if (!f)
    return false;
  StaticJsonDocument<192> doc;
  if (!deserializeJson(doc, f))
  {
    uint8_t m = doc["m"] | 0;
    win_mode = m <= 3 ? m : 0;
    float th = doc["th"] | 1.5f;
    win_threshold = (th >= 0.5f && th <= 5.0f) ? th : 1.5f;
    uint8_t n = doc["n"] | 5;
    win_minutes = (n >= 2 && n <= 14) ? n : 5;
    uint8_t p = doc["p"] | 30;
    win_pause_min = (p >= 5 && p <= 240) ? p : 30;
  }
  f.close();
  return true;
}

bool saveWindow()
{
  StaticJsonDocument<192> doc;
  doc["m"] = win_mode;
  doc["th"] = win_threshold;
  doc["n"] = win_minutes;
  doc["p"] = win_pause_min;
  File f = SPIFFS.open(WIN_FILE, "w");
  if (!f)
    return false;
  serializeJson(doc, f);
  f.close();
  return true;
}

static void winPower(bool on)
{
  heatpumpSettings s = hpBaseSettings();
  s.power = on ? "ON" : "OFF";
  nightSendSettings(s);
}

void checkWindow()
{
  if (win_check_ms != 0 && millis() - win_check_ms < 30000UL)
    return;
  win_check_ms = millis();
  if (win_mode == 0 || !hp.isConnected())
  {
    win_state = 0;
    win_count = 0;
    return;
  }
  heatpumpSettings s = hpBaseSettings();
  const char *pw = s.power != NULL ? s.power : "";
  const char *md = s.mode != NULL ? s.mode : "";
  bool on = strcmp(pw, "ON") == 0;
  float t = hp.getRoomTemperature();
  if (!(t > -40 && t < 60) || t == 0)
    return;

  if (win_state != 0)
  {
    // recovered: half a degree back from the extreme, or the longest pause is over
    if (win_heating ? t < win_extreme : t > win_extreme)
      win_extreme = t;
    bool recovered = win_heating ? (t >= win_extreme + 0.5f) : (t <= win_extreme - 0.5f);
    bool timeUp = millis() - win_open_ms > (unsigned long)win_pause_min * 60000UL;
    bool userOn = win_state == 2 && on; // switched on again by hand
    if (recovered || timeUp || userOn)
    {
      if (win_state == 2 && win_mode == 2 && !on)
        winPower(true);
      win_state = 0;
      win_count = 0;
      win_settle_ms = millis();
      strlcpy(win_prev_power, "ON", sizeof(win_prev_power));
    }
    return;
  }

  // judge only a unit that has been heating or cooling steadily
  if (strcmp(pw, win_prev_power) != 0 || strcmp(md, win_prev_mode) != 0)
  {
    strlcpy(win_prev_power, pw, sizeof(win_prev_power));
    strlcpy(win_prev_mode, md, sizeof(win_prev_mode));
    win_settle_ms = millis();
    win_count = 0;
  }
  bool heat = strcmp(md, "HEAT") == 0;
  bool cool = strcmp(md, "COOL") == 0;
  if (!on || (!heat && !cool) || cleanEndMillis != 0)
  {
    win_count = 0;
    return;
  }
  win_ring[win_head] = (int8_t)lroundf(t * 2.0f);
  win_head = (win_head + 1) % WIN_SAMPLES;
  if (win_count < WIN_SAMPLES)
    win_count++;
  if (millis() - win_settle_ms < 600000UL)
    return; // the first ten minutes after switching on move the temperature anyway
  uint8_t span = (uint8_t)(win_minutes * 2);
  if (win_count <= span)
    return;
  float ext = t;
  for (uint8_t n = 1; n <= span; n++)
  {
    float v = win_ring[(win_head + WIN_SAMPLES - 1 - n) % WIN_SAMPLES] / 2.0f;
    if (heat ? v > ext : v < ext)
      ext = v;
  }
  float moved = heat ? (ext - t) : (t - ext);
  if (moved >= win_threshold)
  {
    win_heating = heat;
    win_extreme = t;
    win_open_ms = millis();
    if (win_mode == 1)
    {
      win_state = 1;
    }
    else
    {
      win_state = 2;
      winPower(false);
    }
    ESP_LOGI(TAG, "Open window detected (%.1f in %u min)", moved, win_minutes);
  }
}

void handleApiWindow(AsyncWebServerRequest *request)
{
  if (!apiAuthOk(request))
    return;
  if (request->hasArg(F("cfg")))
  {
    uint8_t m = (uint8_t)request->arg(F("m")).toInt();
    win_mode = m <= 3 ? m : 0;
    String ths = request->arg(F("th"));
    ths.replace(',', '.');
    float th = ths.toFloat();
    if (th >= 0.5f && th <= 5.0f)
      win_threshold = th;
    uint8_t n = (uint8_t)request->arg(F("n")).toInt();
    if (n >= 2 && n <= 14)
      win_minutes = n;
    uint8_t p = (uint8_t)request->arg(F("p")).toInt();
    if (p >= 5 && p <= 240)
      win_pause_min = p;
    win_count = 0;
    bool ok = saveWindow();
    request->send(ok ? 200 : 500, "application/json", String(ok ? F("{\"ok\":true}") : F("{\"err\":\"save\"}")));
    return;
  }
  String json;
  json.reserve(96);
  json += F("{\"m\":");
  json += win_mode;
  json += F(",\"th\":");
  json += String(win_threshold, 1);
  json += F(",\"n\":");
  json += win_minutes;
  json += F(",\"p\":");
  json += win_pause_min;
  json += F(",\"state\":");
  json += win_state;
  json += F("}");
  request->send(200, "application/json", json);
}

// ---------------------------------------------------------------------------
// scenes: one tap (or one HTTP call) sets several units
// ---------------------------------------------------------------------------
// Stored as JSON text: [{"n":"Abend","a":[{"d":"","p":1,"m":"HEAT","t":21,"f":"AUTO"},
// {"d":"192.168.2.210","p":0}]}]. "d" empty = this unit. Temperatures are in
// the display scale of the target unit, like the schedules.
#define SCENES_FILE KS_FS_PREFIX "scenes.json"
#define MAX_SCENES 6
#define MAX_SCENE_ACTIONS 6
#define SCENES_MAX_LEN 2400
String scenes_json = "[]";
int8_t sceneRunRequest = -1;

static bool scenesValid(const String &json)
{
  if (json.length() > SCENES_MAX_LEN)
    return false;
  DynamicJsonDocument doc(5120);
  if (deserializeJson(doc, json) || !doc.is<JsonArray>())
    return false;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() > MAX_SCENES)
    return false;
  for (JsonObject sc : arr)
  {
    if (!sc["a"].is<JsonArray>() || sc["a"].as<JsonArray>().size() > MAX_SCENE_ACTIONS)
      return false;
  }
  return true;
}

bool loadScenes()
{
  if (!SPIFFS.exists(SCENES_FILE))
    return false;
  File f = SPIFFS.open(SCENES_FILE, "r");
  if (!f)
    return false;
  String json = f.readString();
  f.close();
  if (!scenesValid(json))
    return false;
  scenes_json = json;
  return true;
}

bool saveScenes(const String &json)
{
  if (!scenesValid(json))
    return false;
  File f = SPIFFS.open(SCENES_FILE, "w");
  if (!f)
    return false;
  f.print(json);
  f.close();
  scenes_json = json;
  return true;
}

// runs from the main loop: talks to the heat pump and to the other units
static void sceneRun(uint8_t idx)
{
  DynamicJsonDocument doc(5120);
  if (deserializeJson(doc, scenes_json))
    return;
  JsonArray arr = doc.as<JsonArray>();
  if (idx >= arr.size())
    return;
  for (JsonObject a : arr[idx]["a"].as<JsonArray>())
  {
    const char *dev = a["d"] | "";
    bool on = (a["p"] | 0) != 0;
    const char *mode = a["m"] | "";
    float temp = a["t"] | 0.0f;
    const char *fan = a["f"] | "";
    if (dev[0] == '\0')
    {
      if (!hp.isConnected())
        continue;
      heatpumpSettings s = hpBaseSettings();
      s.power = on ? "ON" : "OFF";
      if (on)
      {
        cleanEndMillis = 0;
        if (mode[0] != '\0')
          s.mode = mode;
        if (temp > 0)
        {
          float c = convertLocalUnitToCelsius(temp, useFahrenheit);
          s.temperature = c < min_temp ? min_temp : (c > max_temp ? max_temp : c);
        }
        if (fan[0] != '\0')
          s.fan = fan;
      }
      nightSendSettings(s);
    }
    else
    {
      String body = F("PWRCHK=");
      if (on)
      {
        body += F("&POWER=ON");
        if (mode[0] != '\0')
        {
          body += F("&MODE=");
          body += mode;
        }
        if (temp > 0)
        {
          body += F("&TEMP=");
          body += String(temp, 1);
        }
        if (fan[0] != '\0')
        {
          body += F("&FAN=");
          body += fan;
        }
      }
      WiFiClient client;
      HTTPClient http;
      http.setTimeout(3000);
      if (http.begin(client, String(F("http://")) + dev + F("/api/control")))
      {
        http.addHeader(F("Content-Type"), F("application/x-www-form-urlencoded"));
        http.POST(body);
        http.end();
      }
    }
  }
}

void checkScenes()
{
  if (sceneRunRequest >= 0)
  {
    uint8_t idx = (uint8_t)sceneRunRequest;
    sceneRunRequest = -1;
    sceneRun(idx);
  }
}

void handleApiScenes(AsyncWebServerRequest *request)
{
  if (!apiAuthOk(request))
    return;
  if (request->hasArg(F("scenes")))
  {
    bool ok = saveScenes(request->arg(F("scenes")));
    request->send(ok ? 200 : 400, "application/json", String(ok ? F("{\"ok\":true}") : F("{\"err\":\"invalid\"}")));
    return;
  }
  if (request->hasArg(F("run")))
  {
    int idx = request->arg(F("run")).toInt();
    if (idx < 0 || idx >= MAX_SCENES)
    {
      request->send(400, "application/json", String(F("{\"err\":\"index\"}")));
      return;
    }
    sceneRunRequest = (int8_t)idx; // executed by the main loop
    request->send(200, "application/json", String(F("{\"ok\":true}")));
    return;
  }
  request->send(200, "application/json", scenes_json);
}

// ---------------------------------------------------------------------------
// sunrise / sunset for schedules
// ---------------------------------------------------------------------------
float geo_lat = 0;
float geo_lon = 0;
bool geo_set = false;
int16_t sun_rise_min = -1; // local minutes since midnight, -1 = unknown
int16_t sun_set_min = -1;
int16_t sun_yday = -1;
#define GEO_FILE KS_FS_PREFIX "geo.json"

bool loadGeo()
{
  if (!SPIFFS.exists(GEO_FILE))
    return false;
  File f = SPIFFS.open(GEO_FILE, "r");
  if (!f)
    return false;
  StaticJsonDocument<128> doc;
  if (!deserializeJson(doc, f))
  {
    geo_lat = doc["lat"] | 0.0f;
    geo_lon = doc["lon"] | 0.0f;
    geo_set = (doc["set"] | 0) != 0;
  }
  f.close();
  return true;
}

bool saveGeo()
{
  StaticJsonDocument<128> doc;
  doc["lat"] = geo_lat;
  doc["lon"] = geo_lon;
  doc["set"] = geo_set ? 1 : 0;
  File f = SPIFFS.open(GEO_FILE, "w");
  if (!f)
    return false;
  serializeJson(doc, f);
  f.close();
  sun_yday = -1;
  return true;
}

// NOAA approximation, good to about a minute. Result in minutes after 00:00 UTC
// of the given day; false during polar day/night.
static bool sunCalcUtc(int dayOfYear, float lat, float lon, float &riseUtc, float &setUtc)
{
  const double PI_D = 3.14159265358979;
  double g = 2.0 * PI_D / 365.0 * (dayOfYear - 1);
  double eqtime = 229.18 * (0.000075 + 0.001868 * cos(g) - 0.032077 * sin(g) - 0.014615 * cos(2 * g) - 0.040849 * sin(2 * g));
  double decl = 0.006918 - 0.399912 * cos(g) + 0.070257 * sin(g) - 0.006758 * cos(2 * g) + 0.000907 * sin(2 * g) -
                0.002697 * cos(3 * g) + 0.00148 * sin(3 * g);
  double latr = lat * PI_D / 180.0;
  double cosH = cos(90.833 * PI_D / 180.0) / (cos(latr) * cos(decl)) - tan(latr) * tan(decl);
  if (cosH > 1.0 || cosH < -1.0)
    return false;
  double ha = acos(cosH) * 180.0 / PI_D;
  riseUtc = (float)(720.0 - 4.0 * (lon + ha) - eqtime);
  setUtc = (float)(720.0 - 4.0 * (lon - ha) - eqtime);
  return true;
}

// refresh today's sunrise/sunset in local time (once per day)
static void sunUpdate(time_t now)
{
  if (!geo_set)
  {
    sun_rise_min = -1;
    sun_set_min = -1;
    return;
  }
  struct tm g;
  gmtime_r(&now, &g);
  if (g.tm_yday == sun_yday)
    return;
  sun_yday = g.tm_yday;
  float r, s;
  if (!sunCalcUtc(g.tm_yday + 1, geo_lat, geo_lon, r, s))
  {
    sun_rise_min = -1;
    sun_set_min = -1;
    return;
  }
  time_t midUtc = now - ((time_t)g.tm_hour * 3600 + g.tm_min * 60 + g.tm_sec);
  struct tm l;
  time_t t = midUtc + (time_t)lroundf(r * 60.0f);
  localtime_r(&t, &l);
  sun_rise_min = (int16_t)(l.tm_hour * 60 + l.tm_min);
  t = midUtc + (time_t)lroundf(s * 60.0f);
  localtime_r(&t, &l);
  sun_set_min = (int16_t)(l.tm_hour * 60 + l.tm_min);
}

// a schedule time: fixed minutes, or sunrise/sunset plus an offset. -1 = not available today
static int16_t sunResolve(uint8_t src, uint16_t fixedMin, int16_t offset)
{
  if (src == 0)
    return (int16_t)fixedMin;
  int16_t base = src == 1 ? sun_rise_min : sun_set_min;
  if (base < 0)
    return -1;
  int16_t v = base + offset;
  return v < 0 ? 0 : (v > 1439 ? 1439 : v);
}

void handleApiGeo(AsyncWebServerRequest *request)
{
  if (!apiAuthOk(request))
    return;
  if (request->hasArg(F("cfg")))
  {
    String la = request->arg(F("lat"));
    String lo = request->arg(F("lon"));
    la.replace(',', '.');
    lo.replace(',', '.');
    float lat = la.toFloat();
    float lon = lo.toFloat();
    if (la.length() == 0 || lo.length() == 0 || lat < -66 || lat > 66 || lon < -180 || lon > 180)
    {
      request->send(400, "application/json", String(F("{\"err\":\"range\"}")));
      return;
    }
    geo_lat = lat;
    geo_lon = lon;
    geo_set = true;
    bool ok = saveGeo();
    request->send(ok ? 200 : 500, "application/json", String(ok ? F("{\"ok\":true}") : F("{\"err\":\"save\"}")));
    return;
  }
  time_t now;
  time(&now);
  if (now >= min_valid_date)
  {
    setenv("TZ", timezone.c_str(), 1);
    tzset();
    sunUpdate(now);
  }
  String json;
  json.reserve(96);
  json += F("{\"set\":");
  json += geo_set ? F("true") : F("false");
  json += F(",\"lat\":");
  json += String(geo_lat, 3);
  json += F(",\"lon\":");
  json += String(geo_lon, 3);
  json += F(",\"rise\":");
  json += sun_rise_min;
  json += F(",\"sset\":");
  json += sun_set_min;
  json += F("}");
  request->send(200, "application/json", json);
}

void loadFeatures()
{
  loadRsens();
  loadWindow();
  loadScenes();
  loadGeo();
}

void checkFeatures()
{
  checkRemoteSensor();
  checkWindow();
  checkScenes();
}
