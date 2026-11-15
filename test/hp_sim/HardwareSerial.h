// Minimal Arduino stand-in for the host-side HeatPump simulation (see sim.cpp).
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <deque>
#ifndef ESP32
#define ESP32 1
#endif
#define SERIAL_8E1 0
unsigned long millis(); void delay(unsigned long ms);
struct HardwareSerial {
  std::deque<uint8_t> rx; std::deque<uint8_t> tx;
  void begin(int,int,int=-1,int=-1){}
  void write(uint8_t b){ tx.push_back(b); }
  int available(){ return (int)rx.size(); }
  int read(){ if(rx.empty()) return -1; uint8_t b=rx.front(); rx.pop_front(); return b; }
};
