# Zuko (RP2350) — host-native DSP harness
#
# Builds host/harness.cpp against the SAME DSP header the firmware uses
# (src/dsp/dsp.h). `make test` runs the full host-side check:
#   1. selftest: waveshape/FIR/pass-through vs double-precision references
#   2. WAV pass-through: sine in, sine out, bit-exact comparison

CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
REPO     := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

.PHONY: all test clean

all: harness

harness: harness.cpp $(REPO)/src/dsp/dsp.h $(REPO)/src/dsp/jcm800.h $(REPO)/src/dsp/cab_ir.h
	$(CXX) $(CXXFLAGS) -I$(REPO)/src -o $@ harness.cpp

test: harness
	./harness --selftest
	python3 test.py

clean:
	rm -f harness sine.wav out.wav
