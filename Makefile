# SPDX-License-Identifier: BSD-3-Clause
SDCC ?= sdcc
HOST_CC ?= cc
PYTHON ?= python3
S51 ?= s51
BOARD ?= generic
IMAGE ?= bringup
BUILD ?= build/$(BOARD)$(if $(filter debug_fixture,$(IMAGE)),/debug_fixture)

ifeq ($(BOARD),generic)
BOARD_NUMBER := 0
else ifeq ($(BOARD),lg_esl29_rev03)
BOARD_NUMBER := 1
else
$(error BOARD must be generic or lg_esl29_rev03)
endif

ifeq ($(IMAGE),bringup)
else ifeq ($(IMAGE),debug_fixture)
else
$(error IMAGE must be bringup or debug_fixture)
endif

TARGET := $(BUILD)/$(IMAGE)
HEADERS := $(wildcard include/*.h)
DEFINES := -Iinclude -DCC2530_BOARD=$(BOARD_NUMBER)
SDCC_FLAGS := -mmcs51 --model-large --std-c99 --debug --opt-code-size --Werror $(DEFINES)
LINK_FLAGS := --iram-size 0x100 --xram-loc 0 --xram-size 0x1e00 --code-size 0x8000
HOST_FLAGS := -std=c99 -O2 -Wall -Wextra -Werror -pedantic -DCC2530_HOST_TEST $(DEFINES) -Itests
OBJECTS := $(BUILD)/startup_$(BOARD).rel $(BUILD)/status_$(BOARD).rel \
           $(BUILD)/board_$(BOARD).rel $(BUILD)/example_$(IMAGE)_$(BOARD).rel
ifeq ($(IMAGE),debug_fixture)
OBJECTS += $(BUILD)/debug_fixture_$(BOARD).rel
endif

.PHONY: all test force-link
all: $(TARGET).hex $(TARGET).bin
	$(PYTHON) -B tools/verify_firmware.py --board $(BOARD) --image $(IMAGE) --compiler "$(SDCC)" --output $(BUILD)

$(BUILD):
	mkdir -p $@

$(BUILD)/startup_$(BOARD).rel: src/startup.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/status_$(BOARD).rel: src/status.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/board_$(BOARD).rel: boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/example_$(IMAGE)_$(BOARD).rel: examples/$(IMAGE).c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/debug_fixture_$(BOARD).rel: src/debug_pattern.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

# Relink with the selected board even when an explicitly shared BUILD is reused.
$(TARGET).ihx: $(OBJECTS) force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(OBJECTS)

$(TARGET).hex: $(TARGET).ihx
	packihx $< > $@

$(TARGET).bin: $(TARGET).ihx
	makebin -p -s 32768 $< $@

$(BUILD)/host-tests_$(BOARD): tests/test_bootstrap.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_bootstrap.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-fixture-tests_$(BOARD): tests/test_debug_fixture.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c src/debug_pattern.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_debug_fixture.c tests/host_mmio.c src/startup.c src/status.c src/debug_pattern.c boards/$(BOARD).c -o $@

$(BUILD)/host-mac-frame-tests: tests/test_mac_frame.c src/mac_frame.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_frame.c src/mac_frame.c -o $@

$(BUILD)/mac_frame.rel: src/mac_frame.c include/mac_frame.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_frame_test.rel: tests/test_mac_frame.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_frame_test.ihx: $(BUILD)/mac_frame.rel $(BUILD)/mac_frame_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_frame.rel $(BUILD)/mac_frame_test.rel

test: all $(BUILD)/host-tests_$(BOARD) $(BUILD)/host-mac-frame-tests $(BUILD)/mac_frame_test.ihx $(if $(filter debug_fixture,$(IMAGE)),$(BUILD)/host-fixture-tests_$(BOARD))
	$(BUILD)/host-tests_$(BOARD)
	$(BUILD)/host-mac-frame-tests
ifeq ($(IMAGE),debug_fixture)
	$(BUILD)/host-fixture-tests_$(BOARD)
endif
	$(PYTHON) -B -m unittest discover -s tools -p 'test_*.py' -v
	$(PYTHON) -B tests/boot_image.py --board $(BOARD) --image $(IMAGE) --output $(BUILD) --simulator "$(S51)"
	$(PYTHON) -B tests/boot_mac_frame.py --output $(BUILD) --simulator "$(S51)"
