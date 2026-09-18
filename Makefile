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
else ifeq ($(IMAGE),irq_fixture)
else ifeq ($(IMAGE),radio_fifo_fixture)
else ifeq ($(IMAGE),dma_fixture)
else ifeq ($(IMAGE),aes_fixture)
else ifeq ($(IMAGE),prng_fixture)
else ifeq ($(IMAGE),radio_rx_fixture)
else
$(error IMAGE must be bringup, debug_fixture, timebase_fixture, clock_fixture, irq_fixture, radio_fifo_fixture, dma_fixture, aes_fixture, prng_fixture or radio_rx_fixture)
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
ifeq ($(IMAGE),irq_fixture)
OBJECTS += $(BUILD)/irq_fixture_state.rel $(BUILD)/timebase.rel $(BUILD)/irq.rel
endif
ifeq ($(IMAGE),radio_fifo_fixture)
OBJECTS += $(BUILD)/radio_fifo_fixture_state.rel $(BUILD)/timebase.rel $(BUILD)/clock.rel $(BUILD)/radio_fifo.rel
endif
ifeq ($(IMAGE),dma_fixture)
OBJECTS += $(BUILD)/timebase.rel $(BUILD)/clock.rel $(BUILD)/dma.rel $(BUILD)/dma_fixture_state.rel
endif
ifeq ($(IMAGE),aes_fixture)
OBJECTS += $(BUILD)/timebase.rel $(BUILD)/clock.rel $(BUILD)/aes.rel $(BUILD)/aes_fixture_state.rel
endif
ifeq ($(IMAGE),prng_fixture)
OBJECTS += $(BUILD)/timebase.rel $(BUILD)/clock.rel $(BUILD)/prng.rel $(BUILD)/prng_fixture_state.rel
endif
ifeq ($(IMAGE),radio_rx_fixture)
OBJECTS += $(BUILD)/timebase.rel $(BUILD)/clock.rel $(BUILD)/radio_rx.rel $(BUILD)/radio_rx_fixture_state.rel
endif

.PHONY: all test test-timebase test-clock test-irq test-radio-fifo test-dma test-aes test-prng test-nwk-beacon test-nwk-frame test-aps-frame test-protocol-frame force-link
.PHONY: test-zcl-frame test-zcl-value test-zcl-attributes test-zcl-dispatch
.PHONY: test-protocol-budget
.PHONY: test-radio-rx
PROTOCOL_MODULES := mac_frame nwk_frame aps_frame zcl_frame zcl_value zcl_attributes zcl_dispatch
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

$(BUILD)/irq_fixture_state.rel: src/irq_fixture_state.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_fifo_fixture_state.rel: src/radio_fifo_fixture_state.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/dma_fixture_state.rel: src/dma_fixture_state.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/aes_fixture_state.rel: src/aes_fixture_state.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/prng_fixture_state.rel: src/prng_fixture_state.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_rx_fixture_state.rel: src/radio_rx_fixture_state.c $(HEADERS) Makefile | $(BUILD)
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

$(BUILD)/host-mac-frame-tests: tests/test_mac_frame.c src/mac_frame.c src/nwk_beacon.c src/nwk_frame.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_frame.c src/mac_frame.c src/nwk_beacon.c src/nwk_frame.c -o $@

$(BUILD)/mac_frame.rel: src/mac_frame.c include/mac_frame.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_frame_test.rel: tests/test_mac_frame.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

# Place the larger NWK spill area before Beacon so it fits below bit-addressable RAM.
$(BUILD)/mac_frame_test.ihx: $(BUILD)/mac_frame.rel $(BUILD)/nwk_frame.rel $(BUILD)/nwk_beacon.rel $(BUILD)/mac_frame_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_frame.rel $(BUILD)/nwk_frame.rel $(BUILD)/nwk_beacon.rel $(BUILD)/mac_frame_test.rel

$(BUILD)/host-nwk-beacon-tests: tests/test_nwk_beacon.c src/nwk_beacon.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_nwk_beacon.c src/nwk_beacon.c -o $@

$(BUILD)/nwk_beacon.rel: src/nwk_beacon.c include/nwk_beacon.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/nwk_beacon_test.rel: tests/test_nwk_beacon.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/nwk_beacon_test.ihx: $(BUILD)/nwk_beacon.rel $(BUILD)/nwk_beacon_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/nwk_beacon.rel $(BUILD)/nwk_beacon_test.rel

test-nwk-beacon: $(BUILD)/host-nwk-beacon-tests $(BUILD)/nwk_beacon_test.ihx
	$(BUILD)/host-nwk-beacon-tests
	$(PYTHON) -B tests/boot_nwk_beacon.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-nwk-frame-tests: tests/test_nwk_frame.c src/nwk_frame.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_nwk_frame.c src/nwk_frame.c -o $@

$(BUILD)/nwk_frame.rel: src/nwk_frame.c include/nwk_frame.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/nwk_frame_test.rel: tests/test_nwk_frame.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/nwk_frame_test.ihx: $(BUILD)/nwk_frame.rel $(BUILD)/nwk_frame_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/nwk_frame.rel $(BUILD)/nwk_frame_test.rel

test-nwk-frame: $(BUILD)/host-nwk-frame-tests $(BUILD)/nwk_frame_test.ihx
	$(BUILD)/host-nwk-frame-tests
	$(PYTHON) -B tests/boot_nwk_frame.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-aps-frame-tests: tests/test_aps_frame.c src/aps_frame.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_aps_frame.c src/aps_frame.c -o $@

$(BUILD)/aps_frame.rel: src/aps_frame.c include/aps_frame.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/aps_frame_test.rel: tests/test_aps_frame.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/aps_frame_test.ihx: $(BUILD)/aps_frame.rel $(BUILD)/aps_frame_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/aps_frame.rel $(BUILD)/aps_frame_test.rel

test-aps-frame: $(BUILD)/host-aps-frame-tests $(BUILD)/aps_frame_test.ihx
	$(BUILD)/host-aps-frame-tests
	$(PYTHON) -B tests/boot_aps_frame.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-protocol-frame-tests: tests/test_protocol_frame.c src/mac_frame.c src/nwk_frame.c src/aps_frame.c src/zcl_frame.c src/zcl_value.c src/zcl_attributes.c src/zcl_dispatch.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_protocol_frame.c src/mac_frame.c src/nwk_frame.c src/aps_frame.c src/zcl_frame.c src/zcl_value.c src/zcl_attributes.c src/zcl_dispatch.c -o $@

$(BUILD)/protocol_frame_test.rel: tests/test_protocol_frame.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/protocol_frame_test.ihx: $(BUILD)/mac_frame.rel $(BUILD)/nwk_frame.rel $(BUILD)/aps_frame.rel $(BUILD)/protocol_frame_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_frame.rel $(BUILD)/nwk_frame.rel $(BUILD)/aps_frame.rel $(BUILD)/protocol_frame_test.rel

test-protocol-frame: $(BUILD)/host-protocol-frame-tests $(BUILD)/protocol_frame_test.ihx
	$(BUILD)/host-protocol-frame-tests
	$(PYTHON) -B tests/boot_protocol_frame.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-protocol-budget-tests: tests/test_protocol_budget.c $(addprefix src/,$(addsuffix .c,$(PROTOCOL_MODULES))) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) $< $(addprefix src/,$(addsuffix .c,$(PROTOCOL_MODULES))) -o $@

$(BUILD)/protocol_budget_test.rel: tests/test_protocol_budget.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/protocol_budget_test.ihx: $(addprefix $(BUILD)/,$(addsuffix .rel,$(PROTOCOL_MODULES))) $(BUILD)/protocol_budget_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(addprefix $(BUILD)/,$(addsuffix .rel,$(PROTOCOL_MODULES))) $(BUILD)/protocol_budget_test.rel

test-protocol-budget: $(BUILD)/host-protocol-budget-tests $(BUILD)/protocol_budget_test.ihx
	$(BUILD)/host-protocol-budget-tests
	$(PYTHON) -B tests/boot_protocol_budget.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-zcl-frame-tests: tests/test_zcl_frame.c src/zcl_frame.c src/aps_frame.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_frame.c src/zcl_frame.c src/aps_frame.c -o $@

$(BUILD)/zcl_frame.rel: src/zcl_frame.c include/zcl_wire.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_frame_test.rel: tests/test_zcl_frame.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_frame_test.ihx: $(BUILD)/zcl_frame.rel $(BUILD)/aps_frame.rel $(BUILD)/zcl_frame_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/zcl_frame.rel $(BUILD)/aps_frame.rel $(BUILD)/zcl_frame_test.rel

test-zcl-frame: $(BUILD)/host-zcl-frame-tests $(BUILD)/zcl_frame_test.ihx
	$(BUILD)/host-zcl-frame-tests
	$(PYTHON) -B tests/boot_zcl_frame.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-zcl-value-tests: tests/test_zcl_value.c src/zcl_value.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_value.c src/zcl_value.c -o $@

$(BUILD)/zcl_value.rel: src/zcl_value.c include/zcl_wire.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_value_test.rel: tests/test_zcl_value.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_value_test.ihx: $(BUILD)/zcl_value.rel $(BUILD)/zcl_value_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/zcl_value.rel $(BUILD)/zcl_value_test.rel

test-zcl-value: $(BUILD)/host-zcl-value-tests $(BUILD)/zcl_value_test.ihx
	$(BUILD)/host-zcl-value-tests
	$(PYTHON) -B tests/boot_zcl_value.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-zcl-attributes-tests: tests/test_zcl_attributes.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_attributes.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $@

$(BUILD)/zcl_attributes.rel: src/zcl_attributes.c include/zcl_attributes.h include/zcl_wire.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_attributes_test.rel: tests/test_zcl_attributes.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_attributes_test.ihx: $(BUILD)/zcl_attributes.rel $(BUILD)/zcl_frame.rel $(BUILD)/zcl_value.rel $(BUILD)/zcl_attributes_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/zcl_attributes.rel $(BUILD)/zcl_frame.rel $(BUILD)/zcl_value.rel $(BUILD)/zcl_attributes_test.rel

test-zcl-attributes: $(BUILD)/host-zcl-attributes-tests $(BUILD)/zcl_attributes_test.ihx
	$(BUILD)/host-zcl-attributes-tests
	$(PYTHON) -B tests/boot_zcl_attributes.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-zcl-dispatch-tests: tests/test_zcl_dispatch.c src/zcl_dispatch.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_dispatch.c src/zcl_dispatch.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $@

$(BUILD)/zcl_dispatch.rel: src/zcl_dispatch.c include/zcl_dispatch.h include/zcl_attributes.h include/zcl_wire.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_dispatch_test.rel: tests/test_zcl_dispatch.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_dispatch_test.ihx: $(BUILD)/zcl_dispatch.rel $(BUILD)/zcl_attributes.rel $(BUILD)/zcl_frame.rel $(BUILD)/zcl_value.rel $(BUILD)/zcl_dispatch_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/zcl_dispatch.rel $(BUILD)/zcl_attributes.rel $(BUILD)/zcl_frame.rel $(BUILD)/zcl_value.rel $(BUILD)/zcl_dispatch_test.rel

test-zcl-dispatch: $(BUILD)/host-zcl-dispatch-tests $(BUILD)/zcl_dispatch_test.ihx
	$(BUILD)/host-zcl-dispatch-tests
	$(PYTHON) -B tests/boot_zcl_dispatch.py --output $(BUILD) --simulator "$(S51)"

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

$(BUILD)/host-radio-fifo-tests: tests/test_radio_fifo.c tests/host_mmio.c tests/host_mmio.h src/radio_fifo.c src/timebase.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_fifo.c tests/host_mmio.c src/radio_fifo.c src/timebase.c -o $@

$(BUILD)/radio_fifo.rel: src/radio_fifo.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_fifo_test.rel: tests/test_radio_fifo.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_fifo_test.ihx: $(BUILD)/timebase.rel $(BUILD)/radio_fifo.rel $(BUILD)/radio_fifo_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/radio_fifo.rel $(BUILD)/radio_fifo_test.rel

test-radio-fifo: $(BUILD)/host-radio-fifo-tests $(BUILD)/radio_fifo_test.ihx
	$(BUILD)/host-radio-fifo-tests
	$(PYTHON) -B tests/boot_radio_fifo.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-dma-tests: tests/test_dma.c tests/host_mmio.c tests/host_mmio.h src/dma.c src/timebase.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_dma.c tests/host_mmio.c src/dma.c src/timebase.c -o $@

$(BUILD)/dma.rel: src/dma.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/dma_test.rel: tests/test_dma.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/dma_test.ihx: $(BUILD)/timebase.rel $(BUILD)/dma.rel $(BUILD)/dma_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/dma.rel $(BUILD)/dma_test.rel

test-dma: $(BUILD)/host-dma-tests $(BUILD)/dma_test.ihx
	$(BUILD)/host-dma-tests
	$(PYTHON) -B tests/boot_dma.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/aes-reference: tests/aes_reference.c tests/aes_reference.h tests/aes_vectors.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DAES_REFERENCE_MAIN tests/aes_reference.c -o $@

$(BUILD)/host-aes-tests: tests/test_aes.c tests/aes_reference.c tests/aes_reference.h tests/aes_vectors.h tests/host_mmio.c tests/host_mmio.h src/aes.c src/timebase.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_aes.c tests/aes_reference.c tests/host_mmio.c src/aes.c src/timebase.c -o $@

$(BUILD)/aes.rel: src/aes.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/aes_test.rel: tests/test_aes.c tests/aes_vectors.h $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/aes_test.ihx: $(BUILD)/timebase.rel $(BUILD)/aes.rel $(BUILD)/aes_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/aes.rel $(BUILD)/aes_test.rel

test-aes: $(BUILD)/aes-reference $(BUILD)/host-aes-tests $(BUILD)/aes_test.ihx
	$(BUILD)/aes-reference
	$(BUILD)/host-aes-tests
	$(PYTHON) -B tests/boot_aes.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-prng-tests: tests/test_prng.c src/prng.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_prng.c src/prng.c tests/host_mmio.c -o $@

$(BUILD)/prng.rel: src/prng.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_rx.rel: src/radio_rx.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_rx_test.rel: tests/test_radio_rx.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_rx_test.ihx: $(BUILD)/timebase.rel $(BUILD)/radio_rx.rel $(BUILD)/radio_rx_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/radio_rx.rel $(BUILD)/radio_rx_test.rel

$(BUILD)/host-radio-rx-tests: tests/test_radio_rx.c src/radio_rx.c src/timebase.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_rx.c src/radio_rx.c src/timebase.c tests/host_mmio.c -o $@

test-radio-rx: $(BUILD)/host-radio-rx-tests $(BUILD)/radio_rx_test.ihx
	$(BUILD)/host-radio-rx-tests
	$(PYTHON) -B tests/boot_radio_rx.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/prng_test.rel: tests/test_prng.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/prng_test.ihx: $(BUILD)/prng.rel $(BUILD)/prng_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/prng.rel $(BUILD)/prng_test.rel

test-prng: $(BUILD)/host-prng-tests $(BUILD)/prng_test.ihx
	$(BUILD)/host-prng-tests
	$(PYTHON) -B tests/boot_prng.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-timebase-fixture-tests_$(BOARD): tests/test_timebase_fixture.c src/timebase_fixture_state.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_timebase_fixture.c src/timebase_fixture_state.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-timebase-failure-tests_$(BOARD): tests/test_timebase_fixture.c src/timebase_fixture_state.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DTIMEBASE_FIXTURE_FAILURE_TEST tests/test_timebase_fixture.c src/timebase_fixture_state.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-clock-fixture-tests_$(BOARD): tests/test_clock_fixture.c src/clock_fixture_state.c src/clock.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_clock_fixture.c src/clock_fixture_state.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-irq-fixture-tests_$(BOARD): tests/test_irq_fixture.c src/irq_fixture_state.c src/irq.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DIRQ_FIXTURE_HOST_TEST tests/test_irq_fixture.c src/irq_fixture_state.c src/irq.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-radio-fifo-fixture-tests_$(BOARD): tests/test_radio_fifo_fixture.c src/radio_fifo_fixture_state.c src/radio_fifo.c src/clock.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DRADIO_FIFO_FIXTURE_HOST_TEST tests/test_radio_fifo_fixture.c src/radio_fifo_fixture_state.c src/radio_fifo.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-dma-fixture-tests_$(BOARD): tests/test_dma_fixture.c src/dma_fixture_state.c src/dma.c src/clock.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DDMA_FIXTURE_HOST_TEST tests/test_dma_fixture.c src/dma_fixture_state.c src/dma.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-aes-fixture-tests_$(BOARD): tests/test_aes_fixture.c tests/aes_reference.c tests/aes_reference.h tests/aes_vectors.h src/aes_fixture_state.c src/aes.c src/clock.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DAES_FIXTURE_HOST_TEST tests/test_aes_fixture.c tests/aes_reference.c src/aes_fixture_state.c src/aes.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-prng-fixture-tests_$(BOARD): tests/test_prng_fixture.c src/prng_fixture_state.c src/prng.c src/clock.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DPRNG_FIXTURE_HOST_TEST tests/test_prng_fixture.c src/prng_fixture_state.c src/prng.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-radio-rx-fixture-tests_$(BOARD): tests/test_radio_rx_fixture.c tests/test_radio_rx.c src/radio_rx_fixture_state.c src/radio_rx.c src/clock.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_rx_fixture.c src/radio_rx_fixture_state.c src/radio_rx.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

.PHONY: test-radio-rx-fixture
test-radio-rx-fixture: all $(BUILD)/host-radio-rx-fixture-tests_$(BOARD)
	$(BUILD)/host-radio-rx-fixture-tests_$(BOARD)
	$(PYTHON) -B tests/boot_image.py --board $(BOARD) --image radio_rx_fixture --output $(BUILD) --simulator "$(S51)"

test: $(if $(filter radio_rx_fixture,$(IMAGE)),$(BUILD)/host-radio-rx-fixture-tests_$(BOARD))
test: $(if $(filter aes_fixture,$(IMAGE)),$(BUILD)/host-aes-fixture-tests_$(BOARD))
test: $(if $(filter prng_fixture,$(IMAGE)),$(BUILD)/host-prng-fixture-tests_$(BOARD))
test: $(if $(filter radio_fifo_fixture,$(IMAGE)),$(BUILD)/host-radio-fifo-fixture-tests_$(BOARD))
test: $(if $(filter dma_fixture,$(IMAGE)),$(BUILD)/host-dma-fixture-tests_$(BOARD))
test: test-protocol-frame test-protocol-budget test-zcl-frame test-zcl-value test-zcl-attributes test-zcl-dispatch test-radio-rx
test: all test-timebase test-clock test-irq test-radio-fifo test-dma test-aes test-prng test-nwk-beacon test-nwk-frame test-aps-frame $(BUILD)/host-tests_$(BOARD) $(BUILD)/host-mac-frame-tests $(BUILD)/mac_frame_test.ihx $(if $(filter debug_fixture,$(IMAGE)),$(BUILD)/host-fixture-tests_$(BOARD)) $(if $(filter timebase_fixture,$(IMAGE)),$(BUILD)/host-timebase-fixture-tests_$(BOARD) $(BUILD)/host-timebase-failure-tests_$(BOARD)) $(if $(filter clock_fixture,$(IMAGE)),$(BUILD)/host-clock-fixture-tests_$(BOARD)) $(if $(filter irq_fixture,$(IMAGE)),$(BUILD)/host-irq-fixture-tests_$(BOARD))
	$(BUILD)/host-tests_$(BOARD)
	$(BUILD)/host-mac-frame-tests
ifeq ($(IMAGE),radio_fifo_fixture)
	$(BUILD)/host-radio-fifo-fixture-tests_$(BOARD)
endif
ifeq ($(IMAGE),dma_fixture)
	$(BUILD)/host-dma-fixture-tests_$(BOARD)
endif
ifeq ($(IMAGE),aes_fixture)
	$(BUILD)/host-aes-fixture-tests_$(BOARD)
endif
ifeq ($(IMAGE),prng_fixture)
	$(BUILD)/host-prng-fixture-tests_$(BOARD)
endif
ifeq ($(IMAGE),radio_rx_fixture)
	$(BUILD)/host-radio-rx-fixture-tests_$(BOARD)
endif
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
ifeq ($(IMAGE),irq_fixture)
	$(BUILD)/host-irq-fixture-tests_$(BOARD)
endif
	$(PYTHON) -B -m unittest discover -s tools -p 'test_*.py' -v
	$(PYTHON) -B tests/boot_image.py --board $(BOARD) --image $(IMAGE) --output $(BUILD) --simulator "$(S51)"
	$(PYTHON) -B tests/boot_mac_frame.py --output $(BUILD) --simulator "$(S51)"
