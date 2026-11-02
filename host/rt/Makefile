# Zuko (RP2350) — real-time host simulator (PortAudio duplex)
#
# Runs the same DSP chain as the firmware on the Mac: guitar into a USB audio
# interface (Tank G), out through the amp/tone/cab chain to the output device.
#
#   make list          enumerate audio devices
#   make run           live with presets/clean.ini (edit PRESET to switch)

CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
REPO     := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
PA_FLAGS := $(shell pkg-config --cflags --libs portaudio-2.0)
PRESET   ?= presets/jcm800.ini

.PHONY: all list run clean

all: rt

rt: rt.cpp $(REPO)/src/dsp/dsp.h $(REPO)/src/dsp/preamp.h $(REPO)/src/dsp/tone_stack.h \
    $(REPO)/src/dsp/cab_sim.h $(REPO)/src/dsp/cab_ir.h $(REPO)/src/dsp/jcm800.h
	$(CXX) $(CXXFLAGS) -I$(REPO)/src -o $@ rt.cpp $(PA_FLAGS)

list: rt
	./rt --list

run: rt
	cd $(REPO)/host && ./rt/rt --preset $(PRESET)

clean:
	rm -f rt
