// Klimasteuerung feature block (v1.21): measurements from the unit, history
// that survives restarts, external room sensor, open-window detection,
// scenes and sun-based schedule times.
//
// Included once from main.cpp (single translation unit), after the helpers it
// builds on: hpBaseSettings(), nightSendSettings(), ksRequestOk(), apiAuthOk().
#pragma once

#ifdef ESP32
#define KS_FS_PREFIX "/"
#else
#define KS_FS_PREFIX ""
#endif

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
unsigned long meas_power_ms = 0; // when the value last arrived
unsigned long meas_out_ms = 0;
uint8_t meas_raw03[10] = {0};
uint8_t meas_raw06[10] = {0};
bool meas_seen03 = false;
bool meas_seen06 = false;
#define MEAS_MAX_AGE_MS 180000UL

// a value counts only while the unit keeps reporting it: after a lost link or
// when the unit stops sending it, the last figure must not live on
static bool measPowerValid()
{
  return meas_power_w >= 0 && hp.isConnected() && millis() - meas_power_ms < MEAS_MAX_AGE_MS;
}

static bool measOutValid()
{
  return meas_out > -100 && hp.isConnected() && millis() - meas_out_ms < MEAS_MAX_AGE_MS;
}

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
    {
      meas_power_w = (int16_t)w;
      meas_power_ms = millis();
    }
    else if (w == 0 && meas_power_w >= 0)
    {
      meas_power_w = 0;
      meas_power_ms = millis();
    }
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
      {
        meas_out = t;
        meas_out_ms = millis();
      }
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
bool hist_no_save = false;   // factory reset: the filesystem must stay empty
time_t hist_saved_epoch = 0;
unsigned long hist_boot_ms = 0;
#define HIST_FILE KS_FS_PREFIX "hist.bin"
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
  // while the stored curve still waits for the clock nothing new was recorded,
  // so the file is current - and saving now would lose its time stamp
  if (!hist_ready || hist_gap_pending || hist_no_save)
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
  uint32_t sum = 0;
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
bool rsens_unit_remote = false; // the unit may still regulate on a value it was given
unsigned long rsens_ms = 0;   // when the value arrived
unsigned long rsens_sent_ms = 0;
unsigned long rsens_poll_ms = 0;
uint8_t rsens_fails = 0;      // polls in a row without a reading
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
  // the indoor unit keeps the last value across a restart of this module
  rsens_unit_remote = rsens_mode != 0;
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
  float r = roundf(c * 2.0f) / 2.0f;
  if (!rsens_valid || r != rsens_value)
    rsens_dirty = true; // only a changed value is worth a packet to the unit
  rsens_value = r;
  rsens_ms = millis();
  rsens_valid = true;
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

static bool rsensPoll()
{
  bool got = false;
  WiFiClient client;
  HTTPClient http;
  // a source in the own network answers quickly; an absent one must not hold
  // the main loop (heat pump link, timers) for long
  http.setTimeout(1500);
#ifdef ESP32
  http.setConnectTimeout(1500);
#endif
  if (http.begin(client, rsens_url))
  {
    // only short replies with a known length: a large page must not eat the heap
    if (http.GET() == 200 && http.getSize() > 0 && http.getSize() <= 4096)
    {
      String body = http.getString();
      float v;
      // a linked unit without its heat pump link still answers with its last
      // reading: that is no measurement
      if (body.indexOf(F("\"connected\":false")) < 0 && rsensParse(body, v))
      {
        // a linked unit answers in its display scale; everything else is celsius
        if (body.indexOf(F("\"scale\":\"F\"")) >= 0)
          v = (v - 32.0f) / 1.8f;
        got = rsensTake(v);
      }
    }
    http.end();
  }
  return got;
}

void checkRemoteSensor()
{
  // after three misses in a row look only every five minutes
  if (rsens_mode == 1 && rsens_url[0] != '\0' && WiFi.status() == WL_CONNECTED &&
      (rsens_poll_ms == 0 || millis() - rsens_poll_ms > (rsens_fails >= 3 ? 300000UL : 60000UL)))
  {
    rsens_poll_ms = millis();
    if (rsensPoll())
      rsens_fails = 0;
    else if (rsens_fails < 255)
      rsens_fails++;
  }
  if (rsens_mode == 0)
    rsens_valid = false;
  if (rsens_valid && millis() - rsens_ms > (unsigned long)rsens_timeout_min * 60000UL)
    rsens_valid = false; // the sensor went silent: do not regulate on a stale value
  if (!hp.isConnected())
    return;
  if (!rsens_valid)
  {
    // hand the unit back to its own sensor: when the feature was switched
    // off, when the sensor went silent, and after a restart when no reading
    // followed within three minutes (the unit still has the old value then).
    // A remote temperature fed over MQTT has its own owner and is left alone.
    if (rsens_unit_remote && !remoteTempActive && (rsens_mode == 0 || rsens_sent_ms != 0 || millis() > 180000UL))
    {
      hp.setRemoteTemperature(0);
      rsens_unit_remote = false;
    }
    return;
  }
  // tell the unit about a changed value (at most every 20 s: each packet
  // delays the status polling) and repeat it every five minutes
  bool first = rsens_sent_ms == 0;
  if ((rsens_dirty && (first || millis() - rsens_sent_ms > 20000UL)) || (!first && millis() - rsens_sent_ms > 300000UL))
  {
    rsens_dirty = false;
    rsens_sent_ms = millis();
    if (rsens_sent_ms == 0)
      rsens_sent_ms = 1;
    rsens_unit_remote = true;
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
    long m = request->arg(F("m")).toInt();
    String u = request->arg(F("u"));
    u.trim();
    String lower = u;
    lower.toLowerCase();
    const char *err = NULL;
    if (m < 0 || m > 2)
      err = "mode";
    else if (lower.startsWith("https://"))
      err = "https"; // no TLS on this path: refuse instead of failing silently
    else
    {
      if (lower.startsWith("http://"))
        u = u.substring(7);
      for (unsigned int i = 0; i < u.length() && err == NULL; i++)
      {
        char c = u.charAt(i);
        if (c <= ' ' || c == '"' || c == '\\' || c == 0x7f)
          err = "url";
      }
      if (err == NULL && u.length() > 0)
      {
        // a bare address means another Klimasteuerung unit
        if (u.indexOf('/') < 0)
          u += F("/api/status");
        u = String(F("http://")) + u;
      }
      if (err == NULL && u.length() >= sizeof(rsens_url))
        err = "long";
      if (err == NULL && m == 1 && u.length() == 0)
        err = "url";
    }
    long to = request->hasArg(F("to")) ? request->arg(F("to")).toInt() : (long)rsens_timeout_min;
    if (err == NULL && (to < 5 || to > 1440))
      err = "range";
    if (err != NULL)
    {
      request->send(400, "application/json", String(F("{\"err\":\"")) + err + F("\"}"));
      return;
    }
    rsens_mode = (uint8_t)m;
    strlcpy(rsens_url, u.c_str(), sizeof(rsens_url));
    rsens_timeout_min = (uint16_t)to;
    if (request->hasArg(F("newkey")))
      rsensNewKey();
    rsens_poll_ms = 0;
    rsens_fails = 0;
    bool ok = saveRsens();
    request->send(ok ? 200 : 500, "application/json", String(ok ? F("{\"ok\":true}") : F("{\"err\":\"save\"}")));
    return;
  }
  String json;
  json.reserve(260);
  json += F("{\"m\":");
  json += rsens_mode;
  json += F(",\"u\":\"");
  json += rsens_url; // free of quotes and backslashes (checked on save)
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
uint8_t win_state = 0;        // 0 = closed, 1 = open (hint), 2 = open and switched off
bool win_paused = false;      // we switched the unit off and owe it a restart (mode 2)
uint32_t win_cmd_mark = 0;    // command counter of the heat pump right after our power-off
uint8_t win_rec_cnt = 0;      // checks in a row that saw the temperature recover
#define WIN_SAMPLES 30        // one sample every 30 s = 15 minutes
int8_t win_ring[WIN_SAMPLES];
uint8_t win_head = 0;
uint8_t win_count = 0;
float win_extreme = 0;        // lowest (heating) / highest (cooling) reading since it opened
bool win_heating = true;
unsigned long win_open_ms = 0;
unsigned long win_check_ms = 0;
unsigned long win_settle_ms = 0; // last change of power/mode/target/sensor: no judging right after it
char win_prev_power[8] = "";
char win_prev_mode[9] = "";
float win_prev_temp = 0;
bool win_prev_ext = false;
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

static void winClear()
{
  win_state = 0;
  win_paused = false;
  win_count = 0;
  win_rec_cnt = 0;
  win_settle_ms = millis();
}

// A command that does not decide about power (night mode ending, a new
// target, fan or vanes) carries the pause's own "off" along and must not end
// the pause: ask before its setters run, confirm afterwards.
static bool winKeepBegin()
{
  return win_paused && hp.getWantedSeq() == win_cmd_mark;
}

static void winKeepEnd(bool keep)
{
  if (keep)
    win_cmd_mark = hp.getWantedSeq();
}

// A pause that ends with the unit off is the real end of the cooling: the
// drying run after power-off was held back during the pause, let it start now.
static void winCleanRearm()
{
  heatpumpSettings s = hpBaseSettings();
  if (s.power != NULL && strcmp(s.power, "OFF") == 0)
    strlcpy(clean_prev_power, "ON", sizeof(clean_prev_power));
}

// The window counts as closed again. A paused unit is switched back on - but
// only if nobody else gave a command in the meantime: a schedule that ended,
// "all off", a scene, the ad-hoc timer or MQTT all leave their trace in the
// command counter even when their "off" changed nothing, and then the unit
// stays the way that command left it.
static void winResume()
{
  bool was = win_paused;
  bool ours = win_paused && hp.getWantedSeq() == win_cmd_mark;
  winClear();
  if (!hp.isConnected())
    return;
  if (!ours)
  {
    if (was)
      winCleanRearm();
    return;
  }
  heatpumpSettings s = hpBaseSettings();
  if (s.power != NULL && strcmp(s.power, "OFF") == 0)
  {
    winPower(true);
    strlcpy(win_prev_power, "ON", sizeof(win_prev_power));
  }
}

void checkWindow()
{
  if (win_check_ms != 0 && millis() - win_check_ms < 30000UL)
    return;
  win_check_ms = millis();
  if (win_mode == 0 || (win_paused && win_mode != 2))
  {
    // switched off or changed during a pause: hand the unit back first
    if (win_state != 0 || win_paused)
      winResume();
    if (win_mode == 0)
    {
      win_count = 0;
      return;
    }
  }
  if (!hp.isConnected())
  {
    win_count = 0; // a running pause is kept and judged again once the link is back
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
    if (win_state == 2)
    {
      // somebody else took over: switched on again (a clean run after the
      // switch-off does not count), or any other command during a pause
      bool taken = (on && cleanEndMillis == 0) || (win_paused && hp.getWantedSeq() != win_cmd_mark);
      if (taken)
      {
        if (win_paused)
          winCleanRearm();
        winClear();
        strlcpy(win_prev_power, pw, sizeof(win_prev_power));
        strlcpy(win_prev_mode, md, sizeof(win_prev_mode));
        return;
      }
    }
    if (win_heating ? t < win_extreme : t > win_extreme)
      win_extreme = t;
    // recovered: half a degree back from the extreme, seen on two checks in a
    // row and not within the first three minutes (right after switching off
    // the reading at the unit still moves by itself)
    bool back = win_heating ? (t >= win_extreme + 0.5f) : (t <= win_extreme - 0.5f);
    if (back && millis() - win_open_ms > 180000UL)
    {
      if (win_rec_cnt < 2)
        win_rec_cnt++;
    }
    else
    {
      win_rec_cnt = 0;
    }
    bool timeUp = millis() - win_open_ms > (unsigned long)win_pause_min * 60000UL;
    if (win_rec_cnt >= 2 || timeUp)
      winResume();
    return;
  }

  // judge only a unit that has been heating or cooling steadily: a change of
  // power, mode, target or of the sensor in use moves the reading by itself
  bool ext = rsens_valid || remoteTempActive;
  if (strcmp(pw, win_prev_power) != 0 || strcmp(md, win_prev_mode) != 0 || s.temperature != win_prev_temp || ext != win_prev_ext)
  {
    strlcpy(win_prev_power, pw, sizeof(win_prev_power));
    strlcpy(win_prev_mode, md, sizeof(win_prev_mode));
    win_prev_temp = s.temperature;
    win_prev_ext = ext;
    win_settle_ms = millis();
    win_count = 0;
  }
  bool heat = strcmp(md, "HEAT") == 0;
  bool cool = strcmp(md, "COOL") == 0;
  // the frost guard of the holiday mode must never be switched off from here
  if (!on || (!heat && !cool) || cleanEndMillis != 0 || frost_active)
  {
    win_count = 0;
    return;
  }
  if (millis() - win_settle_ms < 600000UL)
  {
    // the first ten minutes after a change move the temperature anyway: no
    // samples from that time, so the look-back can never reach into it
    win_count = 0;
    return;
  }
  win_ring[win_head] = (int8_t)lroundf(t * 2.0f);
  win_head = (win_head + 1) % WIN_SAMPLES;
  if (win_count < WIN_SAMPLES)
    win_count++;
  uint8_t span = (uint8_t)(win_minutes * 2);
  if (span > (uint8_t)(win_count - 1))
    span = (uint8_t)(win_count - 1); // judge with what is there; fewer samples only see less
  if (span == 0)
    return;
  float ext_t = t;
  for (uint8_t n = 1; n <= span; n++)
  {
    float v = win_ring[(win_head + WIN_SAMPLES - 1 - n) % WIN_SAMPLES] / 2.0f;
    if (heat ? v > ext_t : v < ext_t)
      ext_t = v;
  }
  float moved = heat ? (ext_t - t) : (t - ext_t);
  if (moved >= win_threshold)
  {
    win_heating = heat;
    win_extreme = t;
    win_open_ms = millis();
    win_rec_cnt = 0;
    if (win_mode == 1)
    {
      win_state = 1;
    }
    else
    {
      win_state = 2;
      winPower(false);
      win_paused = win_mode == 2;
      win_cmd_mark = hp.getWantedSeq();
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
    long m = request->arg(F("m")).toInt();
    String ths = request->arg(F("th"));
    ths.replace(',', '.');
    float th = request->hasArg(F("th")) ? ths.toFloat() : win_threshold;
    long n = request->hasArg(F("n")) ? request->arg(F("n")).toInt() : (long)win_minutes;
    long p = request->hasArg(F("p")) ? request->arg(F("p")).toInt() : (long)win_pause_min;
    if (m < 0 || m > 3 || !(th >= 0.5f && th <= 5.0f) || n < 2 || n > 14 || p < 5 || p > 240)
    {
      request->send(400, "application/json", String(F("{\"err\":\"range\"}")));
      return;
    }
    win_mode = (uint8_t)m;
    win_threshold = th;
    win_minutes = (uint8_t)n;
    win_pause_min = (uint8_t)p;
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
// Stored as JSON text: [{"n":"Abend","a":[{"d":"","p":1,"m":"HEAT","t":21,"f":"AUTO","s":0},
// {"d":"192.168.2.210","p":0}]}]. "d" empty = this unit. Temperatures are in
// the display scale of the target unit, like the schedules. "s" 1 = Automatik:
// the unit picks heating or cooling itself when the scene runs.
#define SCENES_FILE KS_FS_PREFIX "scenes.json"
#define MAX_SCENES 6
#define MAX_SCENE_ACTIONS 6
#define SCENES_MAX_LEN 2800
String scenes_json = "[]";
uint8_t scene_count = 0;
uint32_t scene_hash[MAX_SCENES]; // of the names, to refuse a tap on an outdated page
int8_t sceneRunRequest = -1;     // set by the web handler
int8_t scene_idx = -1;           // scene being executed by the main loop
uint8_t scene_act = 0;           // next action of that scene

static uint32_t sceneNameHash(const char *s)
{
  uint32_t h = 2166136261UL;
  while (*s)
  {
    h ^= (uint8_t)*s++;
    h *= 16777619UL;
  }
  return h;
}

static bool sceneWordOk(const char *v, const char *const *list, uint8_t n)
{
  if (v[0] == '\0')
    return true;
  for (uint8_t i = 0; i < n; i++)
  {
    if (strcmp(v, list[i]) == 0)
      return true;
  }
  return false;
}

// checks the stored format strictly: what passes here is executed later
static bool scenesParse(const String &json, bool apply)
{
  static const char *const modes[] = {"HEAT", "DRY", "COOL", "FAN", "AUTO"};
  static const char *const fans[] = {"AUTO", "QUIET", "1", "2", "3", "4"};
  if (json.length() > SCENES_MAX_LEN)
    return false;
  DynamicJsonDocument doc(5120);
  if (deserializeJson(doc, json) || !doc.is<JsonArray>())
    return false;
  JsonArray arr = doc.as<JsonArray>();
  if (arr.size() > MAX_SCENES)
    return false;
  uint32_t hashes[MAX_SCENES];
  uint8_t count = 0;
  for (JsonVariant scv : arr)
  {
    if (!scv.is<JsonObject>() || !scv["n"].is<const char *>() || !scv["a"].is<JsonArray>())
      return false;
    JsonArray acts = scv["a"].as<JsonArray>();
    if (acts.size() == 0 || acts.size() > MAX_SCENE_ACTIONS)
      return false;
    for (JsonVariant av : acts)
    {
      if (!av.is<JsonObject>())
        return false;
      if (!av["p"].is<int>() && !av["p"].is<bool>())
        return false;
      const char *d = av["d"] | "";
      if (!av["d"].isNull() && !av["d"].is<const char *>())
        return false;
      size_t dl = strlen(d);
      if (dl > 63)
        return false;
      for (size_t i = 0; i < dl; i++)
      {
        char c = d[i];
        if (!(isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == ':'))
          return false; // a host name or address, nothing that could form another URL
      }
      if ((!av["m"].isNull() && !av["m"].is<const char *>()) || (!av["f"].isNull() && !av["f"].is<const char *>()))
        return false;
      if (!sceneWordOk(av["m"] | "", modes, 5) || !sceneWordOk(av["f"] | "", fans, 6))
        return false;
      float t = av["t"] | 0.0f;
      if (t != 0 && (t < 10 || t > 90))
        return false;
    }
    hashes[count++] = sceneNameHash(scv["n"] | "");
  }
  if (apply)
  {
    scene_count = count;
    memcpy(scene_hash, hashes, sizeof(hashes));
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
  if (!scenesParse(json, true))
    return false;
  scenes_json = json;
  return true;
}

bool saveScenes(const String &json)
{
  if (!scenesParse(json, false))
    return false;
  File f = SPIFFS.open(SCENES_FILE, "w");
  if (!f)
    return false;
  f.print(json);
  f.close();
  scene_idx = -1; // a running scene belongs to the old list
  scenes_json = json;
  scenesParse(json, true);
  return true;
}

// Automatik: heating or cooling follows from target versus room temperature,
// the same rule the control page uses. NULL = no reason to change the mode.
static const char *smartPickMode(float targetC)
{
  float room = hp.getRoomTemperature();
  if (!(room > -40 && room < 60) || room == 0)
    return NULL;
  if (targetC < room)
    return "COOL";
  if (targetC > room && supportHeatMode)
    return "HEAT";
  return NULL;
}

static void sceneAction(JsonObject a)
{
  const char *dev = a["d"] | "";
  bool on = a["p"].as<bool>();
  const char *mode = a["m"] | "";
  float temp = a["t"] | 0.0f;
  const char *fan = a["f"] | "";
  int smart = a["s"] | -1; // -1 = leave the Automatik setting alone
  if (dev[0] == '\0')
  {
    if (!hp.isConnected())
      return;
    heatpumpSettings s = hpBaseSettings();
    s.power = on ? "ON" : "OFF";
    if (on)
    {
      cleanEndMillis = 0;
      if (temp > 0)
      {
        float c = convertLocalUnitToCelsius(temp, useFahrenheit);
        s.temperature = c < min_temp ? min_temp : (c > max_temp ? max_temp : c);
      }
      if (mode[0] != '\0')
        s.mode = mode;
      else if (smart == 1)
      {
        const char *pick = smartPickMode(s.temperature);
        if (pick != NULL)
          s.mode = pick;
      }
      if (fan[0] != '\0')
        s.fan = fan;
      if (smart >= 0 && (smart == 1) != smart_mode)
      {
        smart_mode = smart == 1;
        saveSmart();
      }
    }
    nightSendSettings(s);
    return;
  }
  String body = F("PWRCHK=");
  if (on)
  {
    body += F("&POWER=ON");
    if (temp > 0)
    {
      body += F("&TEMP=");
      body += String(temp, 1);
    }
    if (mode[0] != '\0')
    {
      body += F("&MODE=");
      body += mode;
    }
    if (fan[0] != '\0')
    {
      body += F("&FAN=");
      body += fan;
    }
    if (smart == 1)
      body += F("&SMART=1&SMARTPICK=1"); // the other unit picks the mode from its own room temperature
    else if (smart == 0)
      body += F("&SMART=0");
  }
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(2500);
#ifdef ESP32
  http.setConnectTimeout(2000);
#endif
  if (http.begin(client, String(F("http://")) + dev + F("/api/control")))
  {
    http.addHeader(F("Content-Type"), F("application/x-www-form-urlencoded"));
    http.POST(body);
    http.end();
  }
}

// Runs from the main loop, one action per pass: an unreachable unit costs a
// connect timeout, and the heat pump link and all timers must keep running
// between those calls (a long block made the link reconnect and drop the
// command for this unit).
void checkScenes()
{
  if (scene_idx < 0 && sceneRunRequest >= 0)
  {
    scene_idx = sceneRunRequest;
    scene_act = 0;
    sceneRunRequest = -1;
  }
  if (scene_idx < 0)
    return;
  if (requestHpUpdate)
    return; // let the command for this unit go out before the next call blocks
  DynamicJsonDocument doc(5120);
  if (deserializeJson(doc, scenes_json))
  {
    scene_idx = -1;
    return;
  }
  JsonArray arr = doc.as<JsonArray>();
  if ((size_t)scene_idx >= arr.size())
  {
    scene_idx = -1;
    return;
  }
  JsonArray acts = arr[scene_idx]["a"].as<JsonArray>();
  if (scene_act >= acts.size())
  {
    scene_idx = -1;
    return;
  }
  JsonObject a = acts[scene_act++];
  sceneAction(a);
  if (scene_act >= acts.size())
    scene_idx = -1;
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
    long idx = request->arg(F("run")).toInt();
    // the page names the scene it shows: a list that changed meanwhile must
    // not make the tap run a different scene
    if (idx < 0 || idx >= scene_count ||
        (request->hasArg(F("n")) && sceneNameHash(request->arg(F("n")).c_str()) != scene_hash[idx]))
    {
      request->send(409, "application/json", String(F("{\"err\":\"changed\"}")));
      return;
    }
    if (scene_idx >= 0 || sceneRunRequest >= 0)
    {
      request->send(409, "application/json", String(F("{\"err\":\"busy\"}")));
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
int16_t sun_yday = -1;     // local day the two values belong to
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

// clock time of an instant, as minutes of the local day `yday`; an instant on
// the day before/after is pinned to the start/end of the day
static int16_t sunLocalMinutes(time_t t, int yday)
{
  struct tm x;
  localtime_r(&t, &x);
  int diff = x.tm_yday - yday;
  if (diff > 1)
    diff = -1; // year change
  else if (diff < -1)
    diff = 1;
  if (diff < 0)
    return 0;
  if (diff > 0)
    return 1439;
  return (int16_t)(x.tm_hour * 60 + x.tm_min);
}

// refresh today's sunrise/sunset in local time (once per local day). The two
// instants are computed for the local calendar date, so they carry the clock
// offset of that day - also where local midnight is far from UTC midnight and
// around a daylight-saving change.
static void sunUpdate(time_t now)
{
  if (!geo_set)
  {
    sun_rise_min = -1;
    sun_set_min = -1;
    return;
  }
  struct tm l;
  localtime_r(&now, &l);
  if (l.tm_yday == sun_yday)
    return;
  sun_yday = l.tm_yday;
  struct tm g;
  gmtime_r(&now, &g);
  int dd = l.tm_yday - g.tm_yday; // local date ahead of / behind the UTC date
  if (dd > 1)
    dd = -1;
  else if (dd < -1)
    dd = 1;
  // 00:00 UTC of the local calendar date
  time_t midUtc = now - ((time_t)g.tm_hour * 3600 + g.tm_min * 60 + g.tm_sec) + (time_t)dd * 86400;
  float r, s;
  if (!sunCalcUtc(l.tm_yday + 1, geo_lat, geo_lon, r, s))
  {
    sun_rise_min = -1;
    sun_set_min = -1;
    return;
  }
  sun_rise_min = sunLocalMinutes(midUtc + (time_t)lroundf(r * 60.0f), l.tm_yday);
  sun_set_min = sunLocalMinutes(midUtc + (time_t)lroundf(s * 60.0f), l.tm_yday);
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

// Last words before the module restarts itself (called from the main loop).
// wifiLost: the restart follows a lost WiFi, after which the unit may run
// without this module for a long time.
void featuresBeforeRestart(bool wifiLost)
{
  histSave(); // keep the curve across the restart
  if (!hp.isConnected())
    return;
  // a window pause lives in RAM only: give the unit back its power now
  if (win_paused)
  {
    winResume();
    hp.update();
  }
  // without WiFi no reading can arrive: do not leave the unit on the last one
  if (wifiLost && rsens_unit_remote && !remoteTempActive)
  {
    hp.setRemoteTemperature(0);
    rsens_unit_remote = false;
  }
}
