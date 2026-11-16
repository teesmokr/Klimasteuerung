// Host-side simulation of the CN105 link: the real HeatPump library talks to a
// fake indoor unit over a fake serial port on a virtual clock. It replays the
// command sequences that went wrong in the field and checks both what the unit
// ends up doing and what the firmware would report as status.
//
//   test/hp_sim/run.sh
//
#include "HeatPump.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>

static unsigned long now_ms = 100000;
HardwareSerial ser;

// ---- fake indoor unit ------------------------------------------------------
struct Unit
{
  uint8_t power = 0, mode = 1, fan = 0, vane = 0, wv = 3;
  float temp = 21;
  bool isee = false;      // reports the i-See bit while running
  bool roundTemp = false; // only accepts whole degrees
  bool ackSets = true;    // answers set packets (0x41)
  bool silent = false;    // answers nothing at all (link down)
};
static int setPackets = 0; // set packets (0x41 with settings) seen by the unit
static Unit u;
static std::vector<std::pair<unsigned long, std::vector<uint8_t>>> pendingReplies;

static void reply(std::vector<uint8_t> p)
{
  uint8_t s = 0;
  for (auto b : p)
    s += b;
  p.push_back((0xfc - s) & 0xff);
  pendingReplies.push_back({now_ms + 60, p});
}

static void unitProcess()
{
  while (ser.tx.size() >= 5)
  {
    if (ser.tx[0] != 0xfc)
    {
      ser.tx.pop_front();
      continue;
    }
    size_t len = 5 + ser.tx[4] + 1;
    if (ser.tx.size() < len)
      break;
    std::vector<uint8_t> p(ser.tx.begin(), ser.tx.begin() + len);
    ser.tx.erase(ser.tx.begin(), ser.tx.begin() + len);
    if (u.silent)
      continue;
    if (p[1] == 0x5a)
    {
      reply({0xfc, 0x7a, 0x01, 0x30, 0x01, 0x00});
    }
    else if (p[1] == 0x41)
    {
      if (p[5] == 0x01)
        setPackets++;
      if (!u.ackSets)
        continue;
      if (p[5] == 0x01)
      {
        if (p[6] & 1) u.power = p[8];
        if (p[6] & 2) u.mode = p[9];
        if (p[6] & 4)
        {
          u.temp = p[19] ? (p[19] - 128) / 2.0f : 31 - p[10];
          if (u.roundTemp) u.temp = floorf(u.temp);
        }
        if (p[6] & 8) u.fan = p[11];
        if (p[6] & 16) u.vane = p[12];
        if (p[7] & 1) u.wv = p[18] & 0x0f;
      }
      std::vector<uint8_t> r = {0xfc, 0x61, 0x01, 0x30, 0x10};
      r.resize(21, 0);
      reply(r);
    }
    else if (p[1] == 0x42)
    {
      std::vector<uint8_t> r = {0xfc, 0x62, 0x01, 0x30, 0x10};
      r.resize(21, 0);
      r[5] = p[5];
      if (p[5] == 0x02)
      {
        r[8] = u.power;
        r[9] = u.mode + ((u.isee && u.power) ? 8 : 0);
        r[10] = 31 - (int)u.temp;
        r[11] = u.fan;
        r[12] = u.vane;
        r[15] = u.wv;
        r[16] = (uint8_t)(u.temp * 2 + 128);
      }
      if (p[5] == 0x03)
      {
        r[8] = 10;
        r[11] = (uint8_t)(20 * 2 + 128);
      }
      reply(r);
    }
  }
  for (size_t i = 0; i < pendingReplies.size();)
  {
    if (pendingReplies[i].first <= now_ms)
    {
      for (auto b : pendingReplies[i].second)
        ser.rx.push_back(b);
      pendingReplies.erase(pendingReplies.begin() + i);
    }
    else
      i++;
  }
}

// ---- firmware side (mirrors main.cpp) --------------------------------------
static HeatPump *hp;
static bool requestHpUpdate = false;

static heatpumpSettings hpBaseSettings()
{
  return hp->isUpdatePending() ? hp->getWantedSettings() : hp->getSettings();
}

// like change_states(): build on the base state, send only when something changed
static void cmd(const char *power, float temp)
{
  heatpumpSettings s = hpBaseSettings();
  if (power) s.power = power;
  if (temp > 0) s.temperature = temp;
  hp->setSettings(s);
  if (!(hp->getSettings() == hp->getWantedSettings()))
    requestHpUpdate = true;
}

// a web request can arrive while the main loop is blocked inside the library
static unsigned long injectAt = 0;
static float injectTemp = 0;

unsigned long millis() { return now_ms; }
void delay(unsigned long ms)
{
  for (unsigned long i = 0; i < ms; i++)
  {
    now_ms++;
    unitProcess();
    if (injectAt && now_ms >= injectAt)
    {
      injectAt = 0;
      cmd(NULL, injectTemp);
    }
  }
}

static unsigned long maxBlock = 0; // longest single library call (virtual ms)
static void run(unsigned long ms)
{
  unsigned long end = now_ms + ms;
  while (now_ms < end)
  {
    unsigned long t0 = now_ms;
    if (requestHpUpdate)
    {
      requestHpUpdate = false;
      hp->update();
    }
    hp->sync();
    if (now_ms - t0 > maxBlock)
      maxBlock = now_ms - t0;
    delay(5);
  }
}

// ---- checks ----------------------------------------------------------------
static int failures = 0;
static const char *scenario = "";
static void check(bool ok, const char *what)
{
  printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok)
    failures++;
}
static bool statusIs(const char *power) // what /api/status would show
{
  heatpumpSettings s = hpBaseSettings();
  return s.power != NULL && strcmp(s.power, power) == 0;
}
static bool reportedIs(const char *power) // what the unit last reported
{
  heatpumpSettings s = hp->getSettings();
  return s.power != NULL && strcmp(s.power, power) == 0;
}
static void start(const char *name, bool isee = false, bool roundTemp = false, unsigned long clock = 100000)
{
  now_ms = clock;
  setPackets = 0;
  maxBlock = 0;
  scenario = name;
  printf("%s\n", name);
  delete hp;
  hp = new HeatPump();
  u = Unit();
  u.isee = isee;
  u.roundTemp = roundTemp;
  ser = HardwareSerial();
  pendingReplies.clear();
  requestHpUpdate = false;
  injectAt = 0;
  hp->setSettingsChangedCallback([]() {});
  hp->enableExternalUpdate();
  hp->enableAutoUpdate();
  hp->setFastSync(true);
  hp->connect(&ser);
  run(20000);
}

int main()
{
  // 1. power on, temperature right behind it (the multi-device report)
  const int gaps[] = {1, 5, 300, 800, 1500, 2500};
  for (int gap : gaps)
  {
    char name[80];
    snprintf(name, sizeof(name), "power on, temperature %d ms later", gap);
    start(name);
    injectAt = now_ms + gap;
    injectTemp = 23;
    cmd("ON", 0);
    check(statusIs("ON"), "panel shows ON right after the command");
    run(1500);
    check(statusIs("ON"), "panel still shows ON after 1.5 s");
    run(8500);
    check(u.power == 1 && u.temp == 23, "unit runs at 23 degrees");
    check(reportedIs("ON") && !hp->isUpdatePending(), "status confirmed within 10 s");
    run(120000);
    check(u.power == 1 && u.temp == 23 && reportedIs("ON"), "still running after 2 min");
  }

  // 2. IR remote switches off shortly after a software power-on
  start("software on, IR remote off 10 s later");
  cmd("ON", 0);
  run(10000);
  check(u.power == 1 && reportedIs("ON"), "unit on");
  u.power = 0;
  run(5000);
  check(reportedIs("OFF") && statusIs("OFF"), "status shows OFF within 5 s");
  run(60000);
  check(u.power == 0, "unit stays off (not switched back on)");

  // 3. IR remote switches off 15 minutes later
  start("software on, IR remote off 15 min later");
  cmd("ON", 0);
  run(15 * 60000UL);
  u.power = 0;
  run(5000);
  check(reportedIs("OFF") && statusIs("OFF"), "status shows OFF within 5 s");
  run(60000);
  check(u.power == 0, "unit stays off");

  // 4. IR remote switches on, software switches off again
  start("IR remote on, software off");
  u.power = 1;
  run(5000);
  check(reportedIs("ON"), "status shows ON within 5 s");
  cmd("OFF", 0);
  run(10000);
  check(u.power == 0 && reportedIs("OFF"), "unit off");

  // 5. unit never reports exactly what was asked for
  start("unit reports the i-See bit while running", true, false);
  cmd("ON", 0);
  run(6000);
  check(u.power == 1 && reportedIs("ON"), "status shows ON within 6 s");
  run(60000);
  check(u.power == 1 && reportedIs("ON"), "unit stays on");
  u.power = 0;
  run(5000);
  check(reportedIs("OFF"), "IR remote off shows within 5 s");
  run(60000);
  check(u.power == 0, "unit stays off");

  start("unit rounds the temperature", false, true);
  cmd("ON", 22.5f);
  run(6000);
  check(u.power == 1 && reportedIs("ON"), "status shows ON within 6 s");
  run(60000);
  check(u.power == 1 && reportedIs("ON") && !hp->isUpdatePending(), "settles on what the unit accepted");
  u.power = 0;
  run(5000);
  check(reportedIs("OFF"), "IR remote off shows within 5 s");
  run(60000);
  check(u.power == 0, "unit stays off");

  // 6. unit that never reports exactly what was asked: an IR change shortly
  //    after the command must still win
  start("i-See unit: software on, IR remote off 10 s later", true, false);
  cmd("ON", 0);
  run(10000);
  check(u.power == 1 && !hp->isUpdatePending(), "command confirmed although the report carries the i-See bit");
  u.power = 0;
  run(5000);
  check(reportedIs("OFF") && statusIs("OFF"), "status shows OFF within 5 s");
  run(60000);
  check(u.power == 0, "unit stays off");

  start("rounding unit: software on at 22.5, IR remote off 20 s later", false, true);
  cmd("ON", 22.5f);
  run(20000);
  check(u.power == 1 && !hp->isUpdatePending(), "gave up re-sending within 20 s");
  check(setPackets <= 4, "at most 4 set packets for one command");
  u.power = 0;
  run(5000);
  check(reportedIs("OFF"), "status shows OFF within 5 s");
  run(60000);
  check(u.power == 0, "unit stays off");

  // 7. link outage while a command is pending: never execute it later
  start("command, then the unit goes silent for 10 minutes");
  u.silent = true;
  cmd("ON", 0);
  run(10 * 60000UL);
  u.silent = false;
  setPackets = 0;
  run(60000);
  check(u.power == 0, "stale power-on is not executed after the link is back");
  check(setPackets == 0, "no set packet sent after reconnect");
  check(reportedIs("OFF") && !hp->isUpdatePending(), "status settled on the unit's state");

  // 8. unit that answers info requests but ignores set packets
  start("unit does not acknowledge set packets");
  u.ackSets = false;
  cmd("ON", 0);
  run(5 * 60000UL);
  check(setPackets <= 4, "command is not re-sent forever");
  check(!hp->isUpdatePending() && statusIs("OFF"), "panel falls back to the real state");
  u.ackSets = true;
  u.temp = 25; // IR change is still noticed, i.e. polling kept running
  run(6000);
  check(hp->getSettings().temperature == 25, "status polling still works");

  // 9. corrupted data on the line must not crash or hang
  start("garbage and an oversized length byte on the line");
  {
    const uint8_t junk[] = {0x00, 0xfc, 0x62, 0x01, 0x30, 0xf0, 0x11, 0x22, 0xfc, 0x62, 0x01, 0x30, 0x40};
    for (uint8_t b : junk)
      ser.rx.push_back(b);
    for (int i = 0; i < 300; i++)
      ser.rx.push_back((uint8_t)(i * 7));
  }
  run(30000);
  cmd("ON", 0);
  run(10000);
  check(u.power == 1 && reportedIs("ON"), "link recovers and a command still works");

  // 10. command before the first state report must not crash
  start("command right after connect, before any report");
  {
    delete hp;
    hp = new HeatPump();
    ser = HardwareSerial();
    pendingReplies.clear();
    hp->setSettingsChangedCallback([]() {});
    hp->enableExternalUpdate();
    hp->enableAutoUpdate();
    hp->setFastSync(true);
    hp->connect(&ser);
    cmd("ON", 23); // settings are still empty (NULL strings)
    run(20000);
    check(true, "no crash");
    cmd("ON", 23);
    run(10000);
    check(u.power == 1 && u.temp == 23, "a command after the first report works");
  }

  // 11. millis() wraps after 49.7 days
  start("clock wraps (49.7 days) around a command", false, false, 4294967296UL - 30000UL);
  run(25000);
  cmd("ON", 0);
  run(20000); // crosses the wrap
  check(u.power == 1 && reportedIs("ON"), "command applied across the wrap");
  u.power = 0;
  run(6000);
  check(reportedIs("OFF"), "polling continues after the wrap");
  cmd("ON", 24);
  run(10000);
  check(u.power == 1 && u.temp == 24, "commands work after the wrap");

  // 12. the main loop is never blocked for long
  start("blocking time");
  cmd("ON", 22);
  run(15000);
  cmd("OFF", 0);
  run(15000);
  printf("  longest single library call: %lu ms\n", maxBlock);
  check(maxBlock <= 3500, "no library call blocks longer than 3.5 s");

  printf(failures ? "\n%d check(s) FAILED\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
