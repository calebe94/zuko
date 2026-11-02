##
# Zuko (RP2350) — digital guitar preamp / cab sim
#
# @project https://github.com/ (local only)
# One entry point for everything: firmware (Docker), host harness,
# real-time simulator, demo riff generator and tests.
#
# Quick reference:
#   make test      — host selftest + WAV pass-through checks
#   make firmware  — cross-compile the RP2350 firmware in Docker
#   make verify    — firmware + FPU/SRAM evidence checks
#   make demo      — regenerate the Blitzkrieg-style riff through the chain
#   make live      — real-time guitar through the DSP chain (PRESET=)
#   make clean     — host build artifacts
##

REPO      := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
HOST      := $(REPO)/host

# --- Docker (firmware toolchain; image built once, volume caches the core) ---
DOCKER_IMAGE := zuko-rp2350-build
DOCKER_CACHE := zuko-pio-cache
DOCKER_RUN   := docker run --rm -v $(DOCKER_CACHE):/root/.platformio \
                -v $(REPO):/workspace -w /workspace $(DOCKER_IMAGE)

# --- Host compiler: see host/Makefile and host/rt/Makefile ---

# --- Live defaults (override on the command line: make live PRESET=...) ---
PRESET  ?= presets/jcm800-punk.ini
IN_DEV  ?= 1
OUT_DEV ?= 0

.PHONY: all test firmware verify demo rt live devices clean host help

default: all

all: host

host: harness rt

help:
	@echo "Targets: test | firmware | verify | demo | live | devices | clean"
	@echo "Variables: PRESET= (default $(PRESET)), IN_DEV=, OUT_DEV="

# ---------------------------------------------------------------- host builds
# Delegated to the sub-Makefiles: they own the dependency rules, so these
# targets always do the right thing (build if changed, say nothing if not).

harness:
	$(MAKE) --no-print-directory -C $(HOST) harness

rt:
	$(MAKE) --no-print-directory -C $(HOST)/rt rt

# ---------------------------------------------------------------- tests

test: harness
	cd $(HOST) && ./harness --selftest && python3 test.py

# ---------------------------------------------------------------- firmware

firmware:
	$(DOCKER_RUN) pio run -e rpipico2

# FPU + SRAM placement evidence (same checks as docs/BUILD-NOTES.md)
verify: firmware
	$(DOCKER_RUN) bash -lc '\
	  TC=/root/.platformio/packages/toolchain-rp2040-earlephilhower/bin; \
	  E=.pio/build/rpipico2/firmware.elf; \
	  echo "vfma.f32:  $$($$TC/arm-none-eabi-objdump -d $$E | grep -c -w vfma.f32) (must be > 0)"; \
	  echo "soft-float: $$($$TC/arm-none-eabi-objdump -d $$E | grep -c __aeabi_fmul) (must be 0)"; \
	  $$TC/arm-none-eabi-nm --print-size --defined-only $$E | grep -iE "dma_irq_handler|audio_passthrough"'

# ---------------------------------------------------------------- demo

demo: harness
	python3 $(REPO)/scripts/demo_blitzkrieg.py $(HOST)
	cd $(HOST) && ./harness riff_clean.wav riff_zuko.wav \
	  --preset presets/jcm800-punk.ini

# ---------------------------------------------------------------- live

devices: rt
	$(HOST)/rt/rt --list

live: rt
	cd $(HOST) && ./rt/rt --in $(IN_DEV) --out $(OUT_DEV) --preset $(PRESET)

# ---------------------------------------------------------------- clean

clean:
	rm -f $(HOST)/harness $(HOST)/rt/rt
