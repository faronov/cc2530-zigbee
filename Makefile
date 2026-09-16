# SPDX-License-Identifier: BSD-3-Clause
SDCC ?= sdcc
HOST_CC ?= cc
PYTHON ?= python3
S51 ?= s51
BOARD ?= generic
IMAGE ?= bringup
BUILD ?= build/$(BOARD)$(if $(filter-out bringup,$(IMAGE)),/$(IMAGE))

ifeq ($(BOARD),generic)
BOARD_NUMBER := 0
else ifeq ($(BOARD),lg_esl29_rev03)
BOARD_NUMBER := 1
else
$(error BOARD must be generic or lg_esl29_rev03)
endif

ifeq ($(IMAGE),bringup)
else ifeq ($(IMAGE),debug_fixture)
else ifeq ($(IMAGE),timebase_fixture)
else ifeq ($(IMAGE),clock_fixture)
else
$(error IMAGE must be bringup, debug_fixture, timebase_fixture or clock_fixture)
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
ifeq ($(IMAGE),timebase_fixture)
OBJECTS += $(BUILD)/timebase_fixture_state.rel $(BUILD)/timebase.rel
endif
ifeq ($(IMAGE),clock_fixture)
OBJECTS += $(BUILD)/clock_fixture_state.rel $(BUILD)/timebase.rel $(BUILD)/clock.rel
endif

.PHONY: all test test-timebase test-clock test-irq force-link
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

$(BUILD)/timebase_fixture_state.rel: src/timebase_fixture_state.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/clock_fixture_state.rel: src/clock_fixture_state.c $(HEADERS) Makefile | $(BUILD)
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

$(BUILD)/host-timebase-tests: tests/test_timebase.c tests/host_mmio.c tests/host_mmio.h src/timebase.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_timebase.c tests/host_mmio.c src/timebase.c -o $@

$(BUILD)/timebase.rel: src/timebase.c include/timebase.h include/cc2530_mmio.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/timebase_test.rel: tests/test_timebase.c include/timebase.h include/cc2530_mmio.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/timebase_test.ihx: $(BUILD)/timebase.rel $(BUILD)/timebase_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/timebase_test.rel

test-timebase: $(BUILD)/host-timebase-tests $(BUILD)/timebase_test.ihx
	$(BUILD)/host-timebase-tests
	$(PYTHON) -B tests/boot_timebase.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-clock-tests: tests/test_clock.c tests/host_mmio.c tests/host_mmio.h src/clock.c src/timebase.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_clock.c tests/host_mmio.c src/clock.c src/timebase.c -o $@

$(BUILD)/host-clock-failure-tests: tests/test_clock.c tests/host_mmio.c tests/host_mmio.h src/clock.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCLOCK_FAILURE_TEST tests/test_clock.c tests/host_mmio.c src/clock.c -o $@

$(BUILD)/clock.rel: src/clock.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/clock_test.rel: tests/test_clock.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/clock_test.ihx: $(BUILD)/timebase.rel $(BUILD)/clock.rel $(BUILD)/clock_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/clock.rel $(BUILD)/clock_test.rel

test-clock: $(BUILD)/host-clock-tests $(BUILD)/host-clock-failure-tests $(BUILD)/clock_test.ihx
	$(BUILD)/host-clock-tests
	$(BUILD)/host-clock-failure-tests
	$(PYTHON) -B tests/boot_clock.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-irq-tests: tests/test_irq.c tests/host_mmio.c tests/host_mmio.h src/irq.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_irq.c tests/host_mmio.c src/irq.c -o $@

$(BUILD)/irq.rel: src/irq.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/irq_test.rel: tests/test_irq.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/irq_test.ihx: $(BUILD)/irq.rel $(BUILD)/irq_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/irq.rel $(BUILD)/irq_test.rel

test-irq: $(BUILD)/host-irq-tests $(BUILD)/irq_test.ihx
	$(BUILD)/host-irq-tests
	$(PYTHON) -B tests/boot_irq.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-timebase-fixture-tests_$(BOARD): tests/test_timebase_fixture.c src/timebase_fixture_state.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_timebase_fixture.c src/timebase_fixture_state.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-timebase-failure-tests_$(BOARD): tests/test_timebase_fixture.c src/timebase_fixture_state.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DTIMEBASE_FIXTURE_FAILURE_TEST tests/test_timebase_fixture.c src/timebase_fixture_state.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-clock-fixture-tests_$(BOARD): tests/test_clock_fixture.c src/clock_fixture_state.c src/clock.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_clock_fixture.c src/clock_fixture_state.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

test: all test-timebase test-clock test-irq $(BUILD)/host-tests_$(BOARD) $(BUILD)/host-mac-frame-tests $(BUILD)/mac_frame_test.ihx $(if $(filter debug_fixture,$(IMAGE)),$(BUILD)/host-fixture-tests_$(BOARD)) $(if $(filter timebase_fixture,$(IMAGE)),$(BUILD)/host-timebase-fixture-tests_$(BOARD) $(BUILD)/host-timebase-failure-tests_$(BOARD)) $(if $(filter clock_fixture,$(IMAGE)),$(BUILD)/host-clock-fixture-tests_$(BOARD))
	$(BUILD)/host-tests_$(BOARD)
	$(BUILD)/host-mac-frame-tests
ifeq ($(IMAGE),debug_fixture)
	$(BUILD)/host-fixture-tests_$(BOARD)
endif
ifeq ($(IMAGE),timebase_fixture)
	$(BUILD)/host-timebase-fixture-tests_$(BOARD)
	$(BUILD)/host-timebase-failure-tests_$(BOARD)
endif
ifeq ($(IMAGE),clock_fixture)
	$(BUILD)/host-clock-fixture-tests_$(BOARD)
endif
	$(PYTHON) -B -m unittest discover -s tools -p 'test_*.py' -v
	$(PYTHON) -B tests/boot_image.py --board $(BOARD) --image $(IMAGE) --output $(BUILD) --simulator "$(S51)"
	$(PYTHON) -B tests/boot_mac_frame.py --output $(BUILD) --simulator "$(S51)"
