#!/bin/sh
# Builds and runs the host-side HeatPump simulation (needs only g++).
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/hp_sim_$$"
# AddressSanitizer catches out-of-bounds writes from corrupted packets
g++ -std=c++17 -g -w -fsanitize=address -DARDUINO=200 -DESP32 -I. -I../../components/HeatPump/src \
    sim.cpp ../../components/HeatPump/src/HeatPump.cpp -o "$OUT"
"$OUT"; rc=$?
rm -f "$OUT"
exit $rc
