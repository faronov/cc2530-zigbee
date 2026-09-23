# SPDX-License-Identifier: BSD-3-Clause
SDCC ?= sdcc
HOST_CC ?= cc
PYTHON ?= python3
S51 ?= s51
BOARD ?= generic
IMAGE ?= bringup
BUILD ?= build/$(BOARD)$(if $(filter-out bringup,$(IMAGE)),/$(IMAGE))
LOCAL_BUILD ?= build/local
BOARD_IMAGES := bringup debug_fixture timebase_fixture clock_fixture irq_fixture radio_fifo_fixture dma_fixture aes_fixture prng_fixture radio_rx_fixture flash_fixture radio_tx_fixture radio_noise_fixture radio_link_fixture

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
else ifeq ($(IMAGE),flash_fixture)
else ifeq ($(IMAGE),radio_tx_fixture)
else ifeq ($(IMAGE),radio_noise_fixture)
else ifeq ($(IMAGE),radio_link_fixture)
else
$(error IMAGE must be one of $(BOARD_IMAGES))
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
ifeq ($(IMAGE),flash_fixture)
# The entire hardware-service/compiler prefix must precede every caller object.
OBJECTS := $(BUILD)/flash_exec.rel $(BUILD)/flash.rel $(BUILD)/flash_write.rel $(OBJECTS) $(BUILD)/flash_fixture_state.rel
endif
ifeq ($(IMAGE),radio_tx_fixture)
OBJECTS := $(BUILD)/timebase.rel $(BUILD)/radio_fifo.rel $(BUILD)/radio_tx.rel $(BUILD)/clock.rel $(OBJECTS) $(BUILD)/radio_tx_fixture_state.rel
endif
ifeq ($(IMAGE),radio_noise_fixture)
OBJECTS := $(BUILD)/timebase.rel $(BUILD)/clock.rel $(BUILD)/noise_health.rel $(BUILD)/radio_noise.rel $(OBJECTS) $(BUILD)/radio_noise_fixture_state.rel
endif
ifeq ($(IMAGE),radio_link_fixture)
OBJECTS := $(BUILD)/timebase.rel $(BUILD)/clock.rel $(BUILD)/radio_autoack.rel $(OBJECTS) $(BUILD)/radio_link_fixture_state.rel
endif

.PHONY: all test test-timebase test-clock test-irq test-radio-fifo test-dma test-aes test-prng test-nwk-beacon test-nwk-frame test-aps-frame test-protocol-frame force-link
.PHONY: test-zcl-frame test-zcl-value test-zcl-attributes test-zcl-dispatch test-zcl-basic test-zcl-identify
.PHONY: test-zcl-temperature test-zdo-node test-zdo-srv
.PHONY: test-zigbee-security test-zigbee-mmo test-zigbee-key-hash test-security-counter
.PHONY: test-protocol-budget
.PHONY: test-mac-radio
.PHONY: test-mac-attempt
.PHONY: test-mac-join
.PHONY: test-mac-stamp
.PHONY: test-radio-rx test-radio-autoack test-radio-queue test-radio-tx test-flash test-flash-exec test-flash-write
.PHONY: test-nv-record test-mac-tx
.PHONY: test-nwk-candidates test-nwk-parent test-nwk-parent-sanitize test-mac-time test-mac-scan test-mac-association test-mac-poll
.PHONY: test-common test-common-core test-tools test-board test-local
.PHONY: test-noise-health test-radio-noise
.PHONY: test-radio-noise-fixture test-radio-noise-fixture-sanitize
PROTOCOL_MODULES := mac_frame nwk_frame aps_frame zcl_frame zcl_value zcl_attributes zcl_dispatch zcl_write
ZCL_DISPATCH_SRC := src/zcl_dispatch.c src/zcl_write.c
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

$(BUILD)/flash_fixture_state.rel: src/flash_fixture_state.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_tx_fixture_state.rel: src/radio_tx_fixture_state.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_noise_fixture_state.rel: src/radio_noise_fixture_state.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_link_fixture_state.rel: src/radio_link_fixture_state.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

# Relink with the selected board even when an explicitly shared BUILD is reused.
$(TARGET).ihx: $(OBJECTS) force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(OBJECTS)
ifneq ($(filter clock_fixture radio_fifo_fixture dma_fixture aes_fixture prng_fixture radio_rx_fixture,$(IMAGE)),)
	cp $(BUILD)/clock.rst $(TARGET).clock.rst
endif
ifneq ($(filter radio_fifo_fixture dma_fixture aes_fixture prng_fixture radio_rx_fixture,$(IMAGE)),)
	cp $(BUILD)/$(IMAGE:_fixture=).rst $(TARGET).$(IMAGE:_fixture=).rst
endif
ifeq ($(IMAGE),flash_fixture)
	cp $(BUILD)/flash_exec.rst $(TARGET).exec.rst
	cp $(BUILD)/flash.rst $(TARGET).reader.rst
	cp $(BUILD)/flash_write.rst $(TARGET).service.rst
	cp $(BUILD)/flash_fixture_state.rst $(TARGET).state.rst
endif
ifeq ($(IMAGE),radio_tx_fixture)
	cp $(BUILD)/timebase.rst $(TARGET).timebase.rst
	cp $(BUILD)/radio_fifo.rst $(TARGET).radio_fifo.rst
	cp $(BUILD)/radio_tx.rst $(TARGET).radio_tx.rst
	cp $(BUILD)/clock.rst $(TARGET).clock.rst
	cp $(BUILD)/startup_$(BOARD).rst $(TARGET).startup.rst
	cp $(BUILD)/status_$(BOARD).rst $(TARGET).status.rst
	cp $(BUILD)/board_$(BOARD).rst $(TARGET).$(BOARD).rst
	cp $(BUILD)/example_$(IMAGE)_$(BOARD).rst $(TARGET).radio_tx_fixture.rst
	cp $(BUILD)/radio_tx_fixture_state.rst $(TARGET).radio_tx_fixture_state.rst
endif
ifeq ($(IMAGE),radio_noise_fixture)
	cp $(BUILD)/timebase.rst $(TARGET).timebase.rst
	cp $(BUILD)/clock.rst $(TARGET).clock.rst
	cp $(BUILD)/noise_health.rst $(TARGET).noise_health.rst
	cp $(BUILD)/radio_noise.rst $(TARGET).radio_noise.rst
	cp $(BUILD)/startup_$(BOARD).rst $(TARGET).startup.rst
	cp $(BUILD)/status_$(BOARD).rst $(TARGET).status.rst
	cp $(BUILD)/board_$(BOARD).rst $(TARGET).$(BOARD).rst
	cp $(BUILD)/example_$(IMAGE)_$(BOARD).rst $(TARGET).radio_noise_fixture.rst
	cp $(BUILD)/radio_noise_fixture_state.rst $(TARGET).radio_noise_fixture_state.rst
endif
ifeq ($(IMAGE),radio_link_fixture)
	cp $(BUILD)/timebase.rst $(TARGET).timebase.rst
	cp $(BUILD)/clock.rst $(TARGET).clock.rst
	cp $(BUILD)/radio_autoack.rst $(TARGET).radio_autoack.rst
	cp $(BUILD)/startup_$(BOARD).rst $(TARGET).startup.rst
	cp $(BUILD)/status_$(BOARD).rst $(TARGET).status.rst
	cp $(BUILD)/board_$(BOARD).rst $(TARGET).$(BOARD).rst
	cp $(BUILD)/example_$(IMAGE)_$(BOARD).rst $(TARGET).radio_link_fixture.rst
	cp $(BUILD)/radio_link_fixture_state.rst $(TARGET).radio_link_fixture_state.rst
endif

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

$(BUILD)/host-noise-health-tests: tests/test_noise_health.c src/noise_health.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_noise_health.c src/noise_health.c -o $@

$(BUILD)/noise_health.rel: src/noise_health.c include/noise_health.h Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/noise_health_test.rel: tests/test_noise_health.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/noise_health_test.ihx: $(BUILD)/noise_health.rel $(BUILD)/noise_health_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/noise_health.rel $(BUILD)/noise_health_test.rel

test-noise-health: $(BUILD)/host-noise-health-tests $(BUILD)/noise_health_test.ihx
	$(BUILD)/host-noise-health-tests
	$(PYTHON) -B tests/boot_noise_health.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-radio-noise-tests: tests/test_radio_noise.c tests/host_mmio.c src/radio_noise.c src/timebase.c src/noise_health.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_noise.c tests/host_mmio.c src/radio_noise.c src/timebase.c src/noise_health.c -o $@

$(BUILD)/host-radio-noise-fixture-tests_$(BOARD): tests/test_radio_noise_fixture.c tests/host_mmio.c src/radio_noise_fixture_state.c src/radio_noise.c src/timebase.c src/clock.c src/noise_health.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_noise_fixture.c tests/host_mmio.c src/radio_noise_fixture_state.c src/radio_noise.c src/timebase.c src/clock.c src/noise_health.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-radio-noise-fixture-sanitize_$(BOARD): tests/test_radio_noise_fixture.c tests/host_mmio.c src/radio_noise_fixture_state.c src/radio_noise.c src/timebase.c src/clock.c src/noise_health.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie tests/test_radio_noise_fixture.c tests/host_mmio.c src/radio_noise_fixture_state.c src/radio_noise.c src/timebase.c src/clock.c src/noise_health.c src/startup.c src/status.c boards/$(BOARD).c -o $@

test-radio-noise-fixture-sanitize: $(BUILD)/host-radio-noise-fixture-sanitize_$(BOARD)
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD)/host-radio-noise-fixture-sanitize_$(BOARD)
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 $(PYTHON) -c 'import json,subprocess,sys; r=subprocess.run(sys.argv[1:]+["--vectors"],check=True,capture_output=True,text=True,timeout=15); v=[json.loads(s) for s in r.stdout.splitlines()]; assert len(v)==49; print("IRND sanitized synthetic stimulus serialization: 49 scenarios PASS")' $(BUILD)/host-radio-noise-fixture-sanitize_$(BOARD)

ifeq ($(IMAGE),radio_noise_fixture)
test-radio-noise-fixture: test-board
else
test-radio-noise-fixture:
	$(error test-radio-noise-fixture requires IMAGE=radio_noise_fixture)
endif

$(BUILD)/radio_noise.rel: src/radio_noise.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_noise_test.rel: tests/test_radio_noise.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_noise_test.ihx: $(BUILD)/timebase.rel $(BUILD)/noise_health.rel $(BUILD)/radio_noise.rel $(BUILD)/radio_noise_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/noise_health.rel $(BUILD)/radio_noise.rel $(BUILD)/radio_noise_test.rel
	cp $(BUILD)/timebase.rst $(BUILD)/radio_noise_test.timebase.rst
	cp $(BUILD)/noise_health.rst $(BUILD)/radio_noise_test.noise_health.rst
	cp $(BUILD)/radio_noise.rst $(BUILD)/radio_noise_test.radio_noise.rst
	cp $(BUILD)/radio_noise_test.rst $(BUILD)/radio_noise_test.radio_noise_test.rst

test-radio-noise: $(BUILD)/host-radio-noise-tests $(BUILD)/radio_noise_test.ihx
	$(BUILD)/host-radio-noise-tests
	$(PYTHON) -B tests/boot_radio_noise.py --output $(BUILD) --simulator "$(S51)"

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

SECURITY_SRC := src/timebase.c src/aes.c src/ccm_star.c src/nwk_frame.c src/aps_frame.c src/zigbee_security.c
SECURITY_MODELS := tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c
SECURITY_OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SECURITY_SRC:.c=.rel))) $(BUILD)/security_test.rel
$(BUILD)/ccm_star.rel: src/ccm_star.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zigbee_security.rel: src/zigbee_security.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/security_test.rel: tests/test_zigbee_security.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/security_test.ihx: $(SECURITY_OBJECTS) force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(SECURITY_OBJECTS)
	cp $(BUILD)/timebase.rst $(BUILD)/security_test.timebase.rst
	cp $(BUILD)/aes.rst $(BUILD)/security_test.aes.rst
	cp $(BUILD)/ccm_star.rst $(BUILD)/security_test.ccm_star.rst
	cp $(BUILD)/nwk_frame.rst $(BUILD)/security_test.nwk_frame.rst
	cp $(BUILD)/aps_frame.rst $(BUILD)/security_test.aps_frame.rst
	cp $(BUILD)/zigbee_security.rst $(BUILD)/security_test.zigbee_security.rst
	cp $(BUILD)/security_test.rst $(BUILD)/security_test.security_test.rst

$(BUILD)/host-zigbee-security-tests: tests/test_zigbee_security.c $(SECURITY_SRC) $(SECURITY_MODELS) $(HEADERS) tests/security_aes_model.h tests/aes_reference.h Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zigbee_security.c $(SECURITY_SRC) $(SECURITY_MODELS) -o $@

$(BUILD)/host-zigbee-security-tests-sanitize: tests/test_zigbee_security.c $(SECURITY_SRC) $(SECURITY_MODELS) $(HEADERS) tests/security_aes_model.h tests/aes_reference.h Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zigbee_security.c $(SECURITY_SRC) $(SECURITY_MODELS) -o $@

test-zigbee-security: $(BUILD)/host-zigbee-security-tests $(BUILD)/host-zigbee-security-tests-sanitize $(BUILD)/security_test.ihx
	$(BUILD)/host-zigbee-security-tests
	$(BUILD)/host-zigbee-security-tests-sanitize
	$(PYTHON) -B tests/boot_zigbee_security.py --output $(BUILD) --simulator "$(S51)"

MMO_SRC := src/timebase.c src/aes.c src/zigbee_mmo.c
MMO_OBJECTS := $(addprefix $(BUILD)/,$(notdir $(MMO_SRC:.c=.rel))) $(BUILD)/mmo_test.rel

$(BUILD)/zigbee_mmo.rel: src/zigbee_mmo.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mmo_test.rel: tests/test_zigbee_mmo.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mmo_test.ihx: $(MMO_OBJECTS) force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(MMO_OBJECTS)
	cp $(BUILD)/timebase.rst $(BUILD)/mmo_test.timebase.rst
	cp $(BUILD)/aes.rst $(BUILD)/mmo_test.aes.rst
	cp $(BUILD)/zigbee_mmo.rst $(BUILD)/mmo_test.zigbee_mmo.rst
	cp $(BUILD)/mmo_test.rst $(BUILD)/mmo_test.mmo_test.rst

$(BUILD)/host-zigbee-mmo-tests: tests/test_zigbee_mmo.c $(MMO_SRC) $(SECURITY_MODELS) $(HEADERS) tests/security_aes_model.h tests/aes_reference.h Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zigbee_mmo.c $(MMO_SRC) $(SECURITY_MODELS) -o $@

$(BUILD)/host-zigbee-mmo-tests-sanitize: tests/test_zigbee_mmo.c $(MMO_SRC) $(SECURITY_MODELS) $(HEADERS) tests/security_aes_model.h tests/aes_reference.h Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zigbee_mmo.c $(MMO_SRC) $(SECURITY_MODELS) -o $@

test-zigbee-mmo: $(BUILD)/host-zigbee-mmo-tests $(BUILD)/host-zigbee-mmo-tests-sanitize $(BUILD)/mmo_test.ihx
	$(BUILD)/host-zigbee-mmo-tests
	$(BUILD)/host-zigbee-mmo-tests-sanitize
	$(PYTHON) -B tests/boot_zigbee_mmo.py --output $(BUILD) --simulator "$(S51)"

KEY_HASH_SRC := $(MMO_SRC) src/zigbee_key_hash.c
KEY_HASH_OBJECTS := $(addprefix $(BUILD)/,$(notdir $(KEY_HASH_SRC:.c=.rel))) $(BUILD)/key_hash_test.rel

$(BUILD)/zigbee_key_hash.rel: src/zigbee_key_hash.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/key_hash_test.rel: tests/test_zigbee_key_hash.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/key_hash_test.ihx: $(KEY_HASH_OBJECTS) force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(KEY_HASH_OBJECTS)
	cp $(BUILD)/timebase.rst $(BUILD)/key_hash_test.timebase.rst
	cp $(BUILD)/aes.rst $(BUILD)/key_hash_test.aes.rst
	cp $(BUILD)/zigbee_mmo.rst $(BUILD)/key_hash_test.zigbee_mmo.rst
	cp $(BUILD)/zigbee_key_hash.rst $(BUILD)/key_hash_test.zigbee_key_hash.rst
	cp $(BUILD)/key_hash_test.rst $(BUILD)/key_hash_test.key_hash_test.rst

$(BUILD)/host-zigbee-key-hash-tests: tests/test_zigbee_key_hash.c $(KEY_HASH_SRC) $(SECURITY_MODELS) $(HEADERS) tests/security_aes_model.h tests/aes_reference.h Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zigbee_key_hash.c $(KEY_HASH_SRC) $(SECURITY_MODELS) -o $@

$(BUILD)/host-zigbee-key-hash-tests-sanitize: tests/test_zigbee_key_hash.c $(KEY_HASH_SRC) $(SECURITY_MODELS) $(HEADERS) tests/security_aes_model.h tests/aes_reference.h Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zigbee_key_hash.c $(KEY_HASH_SRC) $(SECURITY_MODELS) -o $@

test-zigbee-key-hash: $(BUILD)/host-zigbee-key-hash-tests $(BUILD)/host-zigbee-key-hash-tests-sanitize $(BUILD)/key_hash_test.ihx
	$(BUILD)/host-zigbee-key-hash-tests
	$(BUILD)/host-zigbee-key-hash-tests-sanitize
	$(PYTHON) -B tests/boot_zigbee_key_hash.py --output $(BUILD) --simulator "$(S51)"

ZDO_NODE_SRC := src/zdo_node.c src/aps_frame.c
$(BUILD)/zdo_node.rel: src/zdo_node.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zdo_node_test.rel: tests/test_zdo_node.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zdo_node_test.ihx: $(BUILD)/zdo_node.rel $(BUILD)/aps_frame.rel $(BUILD)/zdo_node_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/zdo_node.rel $(BUILD)/aps_frame.rel $(BUILD)/zdo_node_test.rel
	cp $(BUILD)/zdo_node.rst $(BUILD)/zdo_node_test.zdo_node.rst
	cp $(BUILD)/aps_frame.rst $(BUILD)/zdo_node_test.aps_frame.rst
	cp $(BUILD)/zdo_node_test.rst $(BUILD)/zdo_node_test.zdo_node_test.rst

$(BUILD)/host-zdo-node-tests: tests/test_zdo_node.c $(ZDO_NODE_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zdo_node.c $(ZDO_NODE_SRC) -o $@

$(BUILD)/host-zdo-node-tests-sanitize: tests/test_zdo_node.c $(ZDO_NODE_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zdo_node.c $(ZDO_NODE_SRC) -o $@

test-zdo-node: $(BUILD)/host-zdo-node-tests $(BUILD)/host-zdo-node-tests-sanitize $(BUILD)/zdo_node_test.ihx
	$(BUILD)/host-zdo-node-tests
	$(BUILD)/host-zdo-node-tests-sanitize
	$(PYTHON) -B tests/boot_zdo_node.py --output $(BUILD) --simulator "$(S51)"

ZDO_SRV_SRC := src/zdo_srv.c src/zdo_node.c src/aps_frame.c src/nwk_frame.c
ZDO_SRV_OBJECTS := $(BUILD)/zdo_srv.rel $(BUILD)/zdo_node.rel $(BUILD)/aps_frame.rel $(BUILD)/nwk_frame.rel
$(BUILD)/zdo_srv.rel: src/zdo_srv.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zdo_srv_test.rel: tests/test_zdo_srv.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zdo_srv_test.ihx: $(ZDO_SRV_OBJECTS) $(BUILD)/zdo_srv_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(ZDO_SRV_OBJECTS) $(BUILD)/zdo_srv_test.rel
	cp $(BUILD)/zdo_srv.rst $(BUILD)/zdo_srv_test.zdo_srv.rst
	cp $(BUILD)/zdo_node.rst $(BUILD)/zdo_srv_test.zdo_node.rst
	cp $(BUILD)/aps_frame.rst $(BUILD)/zdo_srv_test.aps_frame.rst
	cp $(BUILD)/nwk_frame.rst $(BUILD)/zdo_srv_test.nwk_frame.rst
	cp $(BUILD)/zdo_srv_test.rst $(BUILD)/zdo_srv_test.zdo_srv_test.rst

$(BUILD)/host-zdo-srv-tests: tests/test_zdo_srv.c $(ZDO_SRV_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zdo_srv.c $(ZDO_SRV_SRC) -o $@

$(BUILD)/host-zdo-srv-tests-sanitize: tests/test_zdo_srv.c $(ZDO_SRV_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zdo_srv.c $(ZDO_SRV_SRC) -o $@

test-zdo-srv: $(BUILD)/host-zdo-srv-tests $(BUILD)/host-zdo-srv-tests-sanitize $(BUILD)/zdo_srv_test.ihx
	$(BUILD)/host-zdo-srv-tests
	$(BUILD)/host-zdo-srv-tests-sanitize
	$(PYTHON) -B tests/boot_zdo_srv.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-protocol-frame-tests: tests/test_protocol_frame.c src/mac_frame.c src/nwk_frame.c src/aps_frame.c src/zcl_frame.c src/zcl_value.c src/zcl_attributes.c $(ZCL_DISPATCH_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_protocol_frame.c src/mac_frame.c src/nwk_frame.c src/aps_frame.c src/zcl_frame.c src/zcl_value.c src/zcl_attributes.c $(ZCL_DISPATCH_SRC) -o $@

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
	cp $(BUILD)/mac_frame.rst $(BUILD)/protocol_budget_test.mac_frame.rst
	cp $(BUILD)/nwk_frame.rst $(BUILD)/protocol_budget_test.nwk_frame.rst
	cp $(BUILD)/aps_frame.rst $(BUILD)/protocol_budget_test.aps_frame.rst
	cp $(BUILD)/zcl_frame.rst $(BUILD)/protocol_budget_test.zcl_frame.rst
	cp $(BUILD)/zcl_value.rst $(BUILD)/protocol_budget_test.zcl_value.rst
	cp $(BUILD)/zcl_attributes.rst $(BUILD)/protocol_budget_test.zcl_attributes.rst
	cp $(BUILD)/zcl_dispatch.rst $(BUILD)/protocol_budget_test.zcl_dispatch.rst
	cp $(BUILD)/zcl_write.rst $(BUILD)/protocol_budget_test.zcl_write.rst
	cp $(BUILD)/protocol_budget_test.rst $(BUILD)/protocol_budget_test.protocol_budget_test.rst

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

$(BUILD)/host-zcl-dispatch-tests: tests/test_zcl_dispatch.c $(ZCL_DISPATCH_SRC) src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_dispatch.c $(ZCL_DISPATCH_SRC) src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $@

$(BUILD)/zcl_dispatch.rel: src/zcl_dispatch.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_write.rel: src/zcl_write.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_dispatch_test.rel: tests/test_zcl_dispatch.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_dispatch_test.ihx: $(BUILD)/zcl_dispatch.rel $(BUILD)/zcl_write.rel $(BUILD)/zcl_attributes.rel $(BUILD)/zcl_frame.rel $(BUILD)/zcl_value.rel $(BUILD)/zcl_dispatch_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/zcl_dispatch.rel $(BUILD)/zcl_write.rel $(BUILD)/zcl_attributes.rel $(BUILD)/zcl_frame.rel $(BUILD)/zcl_value.rel $(BUILD)/zcl_dispatch_test.rel
	cp $(BUILD)/zcl_dispatch.rst $(BUILD)/zcl_dispatch_test.zcl_dispatch.rst
	cp $(BUILD)/zcl_write.rst $(BUILD)/zcl_dispatch_test.zcl_write.rst
	cp $(BUILD)/zcl_attributes.rst $(BUILD)/zcl_dispatch_test.zcl_attributes.rst
	cp $(BUILD)/zcl_frame.rst $(BUILD)/zcl_dispatch_test.zcl_frame.rst
	cp $(BUILD)/zcl_value.rst $(BUILD)/zcl_dispatch_test.zcl_value.rst
	cp $(BUILD)/zcl_dispatch_test.rst $(BUILD)/zcl_dispatch_test.zcl_dispatch_test.rst

test-zcl-dispatch: $(BUILD)/host-zcl-dispatch-tests $(BUILD)/zcl_dispatch_test.ihx
	$(BUILD)/host-zcl-dispatch-tests
	$(PYTHON) -B tests/boot_zcl_dispatch.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/zcl_basic.rel: src/zcl_basic.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_basic_test.rel: tests/test_zcl_basic.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_basic_test.ihx: $(BUILD)/zcl_basic.rel $(BUILD)/zcl_dispatch.rel $(BUILD)/zcl_write.rel $(BUILD)/zcl_attributes.rel $(BUILD)/zcl_frame.rel $(BUILD)/zcl_value.rel $(BUILD)/zcl_basic_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/zcl_basic.rel $(BUILD)/zcl_dispatch.rel $(BUILD)/zcl_write.rel $(BUILD)/zcl_attributes.rel $(BUILD)/zcl_frame.rel $(BUILD)/zcl_value.rel $(BUILD)/zcl_basic_test.rel
	cp $(BUILD)/zcl_basic.rst $(BUILD)/zcl_basic_test.zcl_basic.rst
	cp $(BUILD)/zcl_dispatch.rst $(BUILD)/zcl_basic_test.zcl_dispatch.rst
	cp $(BUILD)/zcl_write.rst $(BUILD)/zcl_basic_test.zcl_write.rst
	cp $(BUILD)/zcl_attributes.rst $(BUILD)/zcl_basic_test.zcl_attributes.rst
	cp $(BUILD)/zcl_frame.rst $(BUILD)/zcl_basic_test.zcl_frame.rst
	cp $(BUILD)/zcl_value.rst $(BUILD)/zcl_basic_test.zcl_value.rst
	cp $(BUILD)/zcl_basic_test.rst $(BUILD)/zcl_basic_test.zcl_basic_test.rst

$(BUILD)/host-zcl-basic-tests: tests/test_zcl_basic.c src/zcl_basic.c $(ZCL_DISPATCH_SRC) src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_basic.c src/zcl_basic.c $(ZCL_DISPATCH_SRC) src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $@

$(BUILD)/host-zcl-basic-tests-sanitize: tests/test_zcl_basic.c src/zcl_basic.c $(ZCL_DISPATCH_SRC) src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zcl_basic.c src/zcl_basic.c $(ZCL_DISPATCH_SRC) src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $@

test-zcl-basic: $(BUILD)/host-zcl-basic-tests $(BUILD)/host-zcl-basic-tests-sanitize $(BUILD)/zcl_basic_test.ihx
	$(BUILD)/host-zcl-basic-tests
	$(BUILD)/host-zcl-basic-tests-sanitize
	$(PYTHON) -B tests/boot_zcl_basic.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/zcl_identify.rel: src/zcl_identify.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_identify_test.rel: tests/test_zcl_identify.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_identify_test.ihx: $(BUILD)/zcl_identify.rel $(BUILD)/zcl_dispatch.rel $(BUILD)/zcl_write.rel $(BUILD)/zcl_attributes.rel $(BUILD)/zcl_frame.rel $(BUILD)/zcl_value.rel $(BUILD)/zcl_identify_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/zcl_identify.rel $(BUILD)/zcl_dispatch.rel $(BUILD)/zcl_write.rel $(BUILD)/zcl_attributes.rel $(BUILD)/zcl_frame.rel $(BUILD)/zcl_value.rel $(BUILD)/zcl_identify_test.rel
	cp $(BUILD)/zcl_identify.rst $(BUILD)/zcl_identify_test.zcl_identify.rst
	cp $(BUILD)/zcl_dispatch.rst $(BUILD)/zcl_identify_test.zcl_dispatch.rst
	cp $(BUILD)/zcl_write.rst $(BUILD)/zcl_identify_test.zcl_write.rst
	cp $(BUILD)/zcl_attributes.rst $(BUILD)/zcl_identify_test.zcl_attributes.rst
	cp $(BUILD)/zcl_frame.rst $(BUILD)/zcl_identify_test.zcl_frame.rst
	cp $(BUILD)/zcl_value.rst $(BUILD)/zcl_identify_test.zcl_value.rst
	cp $(BUILD)/zcl_identify_test.rst $(BUILD)/zcl_identify_test.zcl_identify_test.rst

$(BUILD)/host-zcl-identify-tests: tests/test_zcl_identify.c src/zcl_identify.c $(ZCL_DISPATCH_SRC) src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_identify.c src/zcl_identify.c $(ZCL_DISPATCH_SRC) src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $@

$(BUILD)/host-zcl-identify-tests-sanitize: tests/test_zcl_identify.c src/zcl_identify.c $(ZCL_DISPATCH_SRC) src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zcl_identify.c src/zcl_identify.c $(ZCL_DISPATCH_SRC) src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $@

test-zcl-identify: $(BUILD)/host-zcl-identify-tests $(BUILD)/host-zcl-identify-tests-sanitize $(BUILD)/zcl_identify_test.ihx
	$(BUILD)/host-zcl-identify-tests
	$(BUILD)/host-zcl-identify-tests-sanitize
	$(PYTHON) -B tests/boot_zcl_identify.py --output $(BUILD) --simulator "$(S51)"

ZCL_TEMPERATURE_MODULES := zcl_temperature zcl_dispatch zcl_write zcl_attributes zcl_frame zcl_value
ZCL_TEMPERATURE_SRC := $(addprefix src/,$(addsuffix .c,$(ZCL_TEMPERATURE_MODULES)))
ZCL_TEMPERATURE_OBJECTS := $(addprefix $(BUILD)/,$(addsuffix .rel,$(ZCL_TEMPERATURE_MODULES)))
ZCL_TEMPERATURE_IMAGES := $(addprefix $(BUILD)/zcl_temperature_,$(addsuffix _test.ihx,wire config report))
$(BUILD)/zcl_temperature.rel: src/zcl_temperature.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/zcl_temperature_wire_test.rel: tests/test_zcl_temperature.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -DZCL_TEMP_PART=1 -c $< -o $@

$(BUILD)/zcl_temperature_config_test.rel: tests/test_zcl_temperature.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -DZCL_TEMP_PART=2 -c $< -o $@

$(BUILD)/zcl_temperature_report_test.rel: tests/test_zcl_temperature.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -DZCL_TEMP_PART=3 -c $< -o $@

$(BUILD)/zcl_temperature_%_test.ihx: $(ZCL_TEMPERATURE_OBJECTS) $(BUILD)/zcl_temperature_%_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(ZCL_TEMPERATURE_OBJECTS) $(@:.ihx=.rel)
	cp $(BUILD)/zcl_temperature.rst $(@:.ihx=).zcl_temperature.rst
	cp $(BUILD)/zcl_dispatch.rst $(@:.ihx=).zcl_dispatch.rst
	cp $(BUILD)/zcl_write.rst $(@:.ihx=).zcl_write.rst
	cp $(BUILD)/zcl_attributes.rst $(@:.ihx=).zcl_attributes.rst
	cp $(BUILD)/zcl_frame.rst $(@:.ihx=).zcl_frame.rst
	cp $(BUILD)/zcl_value.rst $(@:.ihx=).zcl_value.rst
	cp $(@:.ihx=.rst) $(@:.ihx=).zcl_temperature_$*_test.rst

$(BUILD)/host-zcl-temperature-tests: tests/test_zcl_temperature.c $(ZCL_TEMPERATURE_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_temperature.c $(ZCL_TEMPERATURE_SRC) -o $@

$(BUILD)/host-zcl-temperature-tests-sanitize: tests/test_zcl_temperature.c $(ZCL_TEMPERATURE_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zcl_temperature.c $(ZCL_TEMPERATURE_SRC) -o $@

test-zcl-temperature: $(BUILD)/host-zcl-temperature-tests $(BUILD)/host-zcl-temperature-tests-sanitize $(ZCL_TEMPERATURE_IMAGES)
	$(BUILD)/host-zcl-temperature-tests
	$(BUILD)/host-zcl-temperature-tests-sanitize
	$(PYTHON) -B tests/boot_zcl_temperature.py --output $(BUILD) --simulator "$(S51)"

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
	cp $(BUILD)/clock.rst $(BUILD)/clock_test.clock.rst

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
	cp $(BUILD)/radio_rx.rst $(BUILD)/radio_rx_test.radio_rx.rst

$(BUILD)/host-radio-rx-tests: tests/test_radio_rx.c src/radio_rx.c src/timebase.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_rx.c src/radio_rx.c src/timebase.c tests/host_mmio.c -o $@

test-radio-rx: $(BUILD)/host-radio-rx-tests $(BUILD)/radio_rx_test.ihx
	$(BUILD)/host-radio-rx-tests
	$(PYTHON) -B tests/boot_radio_rx.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/radio_autoack.rel: src/radio_autoack.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_autoack_test.rel: tests/test_radio_autoack.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_autoack_test.ihx: $(BUILD)/timebase.rel $(BUILD)/radio_autoack.rel $(BUILD)/radio_autoack_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/radio_autoack.rel $(BUILD)/radio_autoack_test.rel
	cp $(BUILD)/timebase.rst $(BUILD)/radio_autoack_test.timebase.rst
	cp $(BUILD)/radio_autoack.rst $(BUILD)/radio_autoack_test.radio_autoack.rst
	cp $(BUILD)/radio_autoack_test.rst $(BUILD)/radio_autoack_test.radio_autoack_test.rst

$(BUILD)/host-radio-autoack-tests: tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c -o $@

$(BUILD)/host-radio-autoack-tests-sanitize: tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c -o $@

test-radio-autoack: $(BUILD)/host-radio-autoack-tests $(BUILD)/host-radio-autoack-tests-sanitize $(BUILD)/radio_autoack_test.ihx
	$(BUILD)/host-radio-autoack-tests
	$(BUILD)/host-radio-autoack-tests-sanitize
	$(PYTHON) -B tests/boot_radio_autoack.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/prng_test.rel: tests/test_prng.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/prng_test.ihx: $(BUILD)/prng.rel $(BUILD)/prng_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/prng.rel $(BUILD)/prng_test.rel

test-prng: $(BUILD)/host-prng-tests $(BUILD)/prng_test.ihx
	$(BUILD)/host-prng-tests
	$(PYTHON) -B tests/boot_prng.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/flash.rel: src/flash.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/flash_test.rel: tests/test_flash.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/flash_test.ihx: $(BUILD)/flash.rel $(BUILD)/flash_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/flash.rel $(BUILD)/flash_test.rel
	cp $(BUILD)/flash.rst $(BUILD)/flash_test.reader.rst

$(BUILD)/host-flash-tests: tests/test_flash.c src/flash.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_flash.c src/flash.c tests/host_mmio.c -o $@

test-flash: $(BUILD)/host-flash-tests $(BUILD)/flash_test.ihx
	$(BUILD)/host-flash-tests
	$(PYTHON) -B tests/boot_flash.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/flash_exec.rel: src/flash_exec.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/flash_exec_test.rel: tests/test_flash_exec.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/flash_exec_test.ihx: $(BUILD)/flash_exec.rel $(BUILD)/flash_exec_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/flash_exec.rel $(BUILD)/flash_exec_test.rel
	cp $(BUILD)/flash_exec.rst $(BUILD)/flash_exec_test.exec.rst

$(BUILD)/host-flash-exec-tests: tests/test_flash_exec.c tests/host_flash_engine.c src/flash_exec.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_flash_exec.c tests/host_flash_engine.c src/flash_exec.c tests/host_mmio.c -o $@

test-flash-exec: $(BUILD)/host-flash-exec-tests $(BUILD)/flash_exec_test.ihx
	$(BUILD)/host-flash-exec-tests
	$(PYTHON) -B tests/boot_flash_exec.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/flash_write.rel: src/flash_write.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/flash_write_test.rel: tests/test_flash_write.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/flash_write_test.ihx: $(BUILD)/flash_exec.rel $(BUILD)/flash.rel $(BUILD)/flash_write.rel $(BUILD)/flash_write_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/flash_exec.rel $(BUILD)/flash.rel $(BUILD)/flash_write.rel $(BUILD)/flash_write_test.rel
	cp $(BUILD)/flash_exec.rst $(BUILD)/flash_write-exec.rst
	cp $(BUILD)/flash.rst $(BUILD)/flash_write-reader.rst
	cp $(BUILD)/flash_write.rst $(BUILD)/flash_write-service.rst

$(BUILD)/host-flash-write-tests: tests/test_flash_write.c tests/host_flash_engine.c src/flash_exec.c src/flash.c src/flash_write.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_flash_write.c tests/host_flash_engine.c src/flash_exec.c src/flash.c src/flash_write.c tests/host_mmio.c -o $@

test-flash-write: $(BUILD)/host-flash-write-tests $(BUILD)/flash_write_test.ihx
	$(BUILD)/host-flash-write-tests
	$(PYTHON) -B tests/boot_flash_write.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/host-flash-fixture-tests_$(BOARD): tests/test_flash_fixture.c tests/test_flash_write.c tests/host_flash_engine.c src/flash_fixture_state.c src/flash_exec.c src/flash.c src/flash_write.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_flash_fixture.c tests/host_flash_engine.c src/flash_fixture_state.c src/flash_exec.c src/flash.c src/flash_write.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

.PHONY: test-flash-fixture
test-flash-fixture: test-board
	@test "$(IMAGE)" = flash_fixture

$(BUILD)/radio_queue.rel: src/radio_queue.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_queue_test.rel: tests/test_radio_queue.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_queue_test.ihx: $(BUILD)/timebase.rel $(BUILD)/radio_rx.rel $(BUILD)/irq.rel $(BUILD)/radio_queue.rel $(BUILD)/radio_queue_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/radio_rx.rel $(BUILD)/irq.rel $(BUILD)/radio_queue.rel $(BUILD)/radio_queue_test.rel
	cp $(BUILD)/radio_rx.rst $(BUILD)/radio_queue_test.radio_rx.rst
	cp $(BUILD)/radio_queue.rst $(BUILD)/radio_queue_test.radio_queue.rst

$(BUILD)/host-radio-queue-tests: tests/test_radio_queue.c src/radio_queue.c src/radio_rx.c src/timebase.c src/irq.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_queue.c src/radio_queue.c src/radio_rx.c src/timebase.c src/irq.c tests/host_mmio.c -o $@

test-radio-queue: $(BUILD)/host-radio-queue-tests $(BUILD)/host-radio-rx-tests $(BUILD)/radio_queue_test.ihx
	$(BUILD)/host-radio-queue-tests
	$(PYTHON) -B tests/boot_radio_queue.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/radio_tx.rel: src/radio_tx.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_tx_test.rel: tests/test_radio_tx.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/radio_tx_test.ihx: $(BUILD)/timebase.rel $(BUILD)/radio_fifo.rel $(BUILD)/radio_tx.rel $(BUILD)/radio_tx_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/radio_fifo.rel $(BUILD)/radio_tx.rel $(BUILD)/radio_tx_test.rel
	cp $(BUILD)/timebase.rst $(BUILD)/radio_tx_test.timebase.rst
	cp $(BUILD)/radio_fifo.rst $(BUILD)/radio_tx_test.radio_fifo.rst
	cp $(BUILD)/radio_tx.rst $(BUILD)/radio_tx_test.radio_tx.rst
	cp $(BUILD)/radio_tx_test.rst $(BUILD)/radio_tx_test.test_radio_tx.rst

$(BUILD)/host-radio-tx-tests: tests/test_radio_tx.c src/radio_tx.c src/radio_fifo.c src/timebase.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_tx.c src/timebase.c src/radio_fifo.c src/radio_tx.c tests/host_mmio.c -o $@

test-radio-tx: $(BUILD)/host-radio-tx-tests $(BUILD)/radio_tx_test.ihx
	$(BUILD)/host-radio-tx-tests
	$(PYTHON) -B tests/boot_radio_tx.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/nv_record.rel: src/nv_record.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/nv_record_test.rel: tests/test_nv_record.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/nv_record_test.ihx: $(BUILD)/flash_exec.rel $(BUILD)/flash.rel $(BUILD)/flash_write.rel $(BUILD)/nv_record.rel $(BUILD)/nv_record_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/flash_exec.rel $(BUILD)/flash.rel $(BUILD)/flash_write.rel $(BUILD)/nv_record.rel $(BUILD)/nv_record_test.rel
	cp $(BUILD)/flash_exec.rst $(BUILD)/nv_record_test.flash_exec.rst
	cp $(BUILD)/flash.rst $(BUILD)/nv_record_test.flash.rst
	cp $(BUILD)/flash_write.rst $(BUILD)/nv_record_test.flash_write.rst
	cp $(BUILD)/nv_record.rst $(BUILD)/nv_record_test.nv_record.rst

$(BUILD)/host-nv-record-tests: tests/test_nv_record.c tests/test_flash_write.c tests/host_flash_engine.c src/nv_record.c src/flash_exec.c src/flash.c src/flash_write.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_nv_record.c tests/host_flash_engine.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c tests/host_mmio.c -o $@

test-nv-record: $(BUILD)/host-nv-record-tests $(BUILD)/nv_record_test.ihx
	$(BUILD)/host-nv-record-tests
	$(PYTHON) -B tests/boot_nv_record.py --output $(BUILD) --simulator "$(S51)"

COUNTER_SRC := src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c
COUNTER_OBJECTS := $(addprefix $(BUILD)/,$(notdir $(COUNTER_SRC:.c=.rel))) $(BUILD)/security_counter_test.rel
COUNTER_HOST_INPUTS := tests/test_security_counter.c tests/test_nv_record.c tests/test_flash_write.c tests/host_flash_engine.c tests/host_mmio.c tests/host_mmio.h $(COUNTER_SRC) $(HEADERS) Makefile

$(BUILD)/security_counter.rel: src/security_counter.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/security_counter_test.rel: tests/test_security_counter.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/security_counter_test.ihx: $(COUNTER_OBJECTS) force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(COUNTER_OBJECTS)
	cp $(BUILD)/flash_exec.rst $(BUILD)/security_counter_test.flash_exec.rst
	cp $(BUILD)/flash.rst $(BUILD)/security_counter_test.flash.rst
	cp $(BUILD)/flash_write.rst $(BUILD)/security_counter_test.flash_write.rst
	cp $(BUILD)/nv_record.rst $(BUILD)/security_counter_test.nv_record.rst
	cp $(BUILD)/security_counter.rst $(BUILD)/security_counter_test.security_counter.rst
	cp $(BUILD)/security_counter_test.rst $(BUILD)/security_counter_test.security_counter_test.rst

$(BUILD)/host-security-counter-tests: $(COUNTER_HOST_INPUTS) | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_security_counter.c tests/host_flash_engine.c tests/host_mmio.c $(COUNTER_SRC) -o $@

$(BUILD)/host-security-counter-tests-sanitize: $(COUNTER_HOST_INPUTS) | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_security_counter.c tests/host_flash_engine.c tests/host_mmio.c $(COUNTER_SRC) -o $@

test-security-counter: $(BUILD)/host-security-counter-tests $(BUILD)/host-security-counter-tests-sanitize $(BUILD)/security_counter_test.ihx
	$(BUILD)/host-security-counter-tests
	$(BUILD)/host-security-counter-tests-sanitize
	$(PYTHON) -B tests/boot_security_counter.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/mac_tx.rel: src/mac_tx.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_tx_test.rel: tests/test_mac_tx.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_tx_test.ihx: $(BUILD)/mac_frame.rel $(BUILD)/mac_tx.rel $(BUILD)/mac_tx_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_frame.rel $(BUILD)/mac_tx.rel $(BUILD)/mac_tx_test.rel
	cp $(BUILD)/mac_frame.rst $(BUILD)/mac_tx_test.mac_frame.rst
	cp $(BUILD)/mac_tx.rst $(BUILD)/mac_tx_test.mac_tx.rst
	cp $(BUILD)/mac_tx_test.rst $(BUILD)/mac_tx_test.mac_tx_test.rst

$(BUILD)/host-mac-tx-tests: tests/test_mac_tx.c src/mac_tx.c src/mac_frame.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_tx.c src/mac_tx.c src/mac_frame.c -o $@

test-mac-tx: $(BUILD)/host-mac-tx-tests $(BUILD)/mac_tx_test.ihx
	$(BUILD)/host-mac-tx-tests
	$(PYTHON) -B tests/boot_mac_tx.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/nwk_candidates.rel: src/nwk_candidates.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/nwk_candidates_test.rel: tests/test_nwk_candidates.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/nwk_candidates_test.ihx: $(BUILD)/mac_frame.rel $(BUILD)/nwk_beacon.rel $(BUILD)/nwk_candidates.rel $(BUILD)/nwk_candidates_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_frame.rel $(BUILD)/nwk_beacon.rel $(BUILD)/nwk_candidates.rel $(BUILD)/nwk_candidates_test.rel
	cp $(BUILD)/mac_frame.rst $(BUILD)/nwk_candidates_test.mac_frame.rst
	cp $(BUILD)/nwk_beacon.rst $(BUILD)/nwk_candidates_test.nwk_beacon.rst
	cp $(BUILD)/nwk_candidates.rst $(BUILD)/nwk_candidates_test.nwk_candidates.rst
	cp $(BUILD)/nwk_candidates_test.rst $(BUILD)/nwk_candidates_test.nwk_candidates_test.rst

$(BUILD)/host-nwk-candidates-tests: tests/test_nwk_candidates.c src/mac_frame.c src/nwk_beacon.c src/nwk_candidates.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_nwk_candidates.c src/mac_frame.c src/nwk_beacon.c src/nwk_candidates.c -o $@

test-nwk-candidates: $(BUILD)/host-nwk-candidates-tests $(BUILD)/nwk_candidates_test.ihx
	$(BUILD)/host-nwk-candidates-tests
	$(PYTHON) -B tests/boot_nwk_candidates.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/nwk_parent.rel: src/nwk_parent.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/nwk_parent_test.rel: tests/test_nwk_parent.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/nwk_parent_test.ihx: $(BUILD)/mac_frame.rel $(BUILD)/nwk_beacon.rel $(BUILD)/nwk_candidates.rel $(BUILD)/nwk_parent.rel $(BUILD)/nwk_parent_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_frame.rel $(BUILD)/nwk_beacon.rel $(BUILD)/nwk_candidates.rel $(BUILD)/nwk_parent.rel $(BUILD)/nwk_parent_test.rel
	cp $(BUILD)/mac_frame.rst $(BUILD)/nwk_parent_test.mac_frame.rst
	cp $(BUILD)/nwk_beacon.rst $(BUILD)/nwk_parent_test.nwk_beacon.rst
	cp $(BUILD)/nwk_candidates.rst $(BUILD)/nwk_parent_test.nwk_candidates.rst
	cp $(BUILD)/nwk_parent.rst $(BUILD)/nwk_parent_test.nwk_parent.rst
	cp $(BUILD)/nwk_parent_test.rst $(BUILD)/nwk_parent_test.nwk_parent_test.rst

$(BUILD)/host-nwk-parent-tests: tests/test_nwk_parent.c src/nwk_parent.c src/nwk_candidates.c src/nwk_beacon.c src/mac_frame.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_nwk_parent.c src/nwk_parent.c src/nwk_candidates.c src/nwk_beacon.c src/mac_frame.c -o $@

$(BUILD)/host-nwk-parent-sanitize: tests/test_nwk_parent.c src/nwk_parent.c src/nwk_candidates.c src/nwk_beacon.c src/mac_frame.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie tests/test_nwk_parent.c src/nwk_parent.c src/nwk_candidates.c src/nwk_beacon.c src/mac_frame.c -o $@

test-nwk-parent-sanitize: $(BUILD)/host-nwk-parent-sanitize
	$(BUILD)/host-nwk-parent-sanitize

test-nwk-parent: $(BUILD)/host-nwk-parent-tests $(BUILD)/nwk_parent_test.ihx
	$(BUILD)/host-nwk-parent-tests
	$(PYTHON) -B tests/boot_nwk_parent.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/mac_time.rel: src/mac_time.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

MAC_RADIO_MODULES := timebase clock mac_time radio_autoack mac_epoch mac_radio
MAC_RADIO_SRC := $(addprefix src/,$(addsuffix .c,$(MAC_RADIO_MODULES)))
MAC_RADIO_OBJECTS := $(addprefix $(BUILD)/mr_,$(addsuffix .rel,$(MAC_RADIO_MODULES)))
$(BUILD)/mr_%.rel: src/%.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -DCC2530_MAC_RADIO -c $< -o $@

$(BUILD)/mac_radio_test.rel: tests/test_mac_radio.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -DCC2530_MAC_RADIO -c $< -o $@

$(BUILD)/mac_radio_test.ihx: $(MAC_RADIO_OBJECTS) $(BUILD)/mac_radio_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(MAC_RADIO_OBJECTS) $(BUILD)/mac_radio_test.rel
	cp $(BUILD)/mr_timebase.rst $(BUILD)/mac_radio_test.timebase.rst
	cp $(BUILD)/mr_clock.rst $(BUILD)/mac_radio_test.clock.rst
	cp $(BUILD)/mr_mac_time.rst $(BUILD)/mac_radio_test.mac_time.rst
	cp $(BUILD)/mr_radio_autoack.rst $(BUILD)/mac_radio_test.radio_autoack.rst
	cp $(BUILD)/mr_mac_epoch.rst $(BUILD)/mac_radio_test.mac_epoch.rst
	cp $(BUILD)/mr_mac_radio.rst $(BUILD)/mac_radio_test.mac_radio.rst
	cp $(BUILD)/mac_radio_test.rst $(BUILD)/mac_radio_test.test_mac_radio.rst

$(BUILD)/host-mac-radio-tests: tests/test_mac_radio.c tests/test_radio_autoack.c $(MAC_RADIO_SRC) tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO tests/test_mac_radio.c $(MAC_RADIO_SRC) tests/host_mmio.c -o $@

$(BUILD)/host-mac-radio-tests-sanitize: tests/test_mac_radio.c tests/test_radio_autoack.c $(MAC_RADIO_SRC) tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_radio.c $(MAC_RADIO_SRC) tests/host_mmio.c -o $@

test-mac-radio: $(BUILD)/host-mac-radio-tests $(BUILD)/host-mac-radio-tests-sanitize $(BUILD)/mac_radio_test.ihx
	$(BUILD)/host-mac-radio-tests
	$(BUILD)/host-mac-radio-tests-sanitize
	$(PYTHON) -B tests/boot_mac_radio.py --output $(BUILD) --simulator "$(S51)"

MAC_ATTEMPT_MODULES := $(MAC_RADIO_MODULES) mac_attempt
MAC_ATTEMPT_SRC := $(addprefix src/,$(addsuffix .c,$(MAC_ATTEMPT_MODULES)))
MAC_ATTEMPT_OBJECTS := $(addprefix $(BUILD)/ma_,$(addsuffix .rel,$(MAC_ATTEMPT_MODULES)))
MAC_ATTEMPT_DEFINES := -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT
$(BUILD)/ma_%.rel: src/%.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) $(MAC_ATTEMPT_DEFINES) -c $< -o $@

$(BUILD)/ma_test_mac_attempt.rel: tests/test_mac_attempt.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) $(MAC_ATTEMPT_DEFINES) -c $< -o $@

$(BUILD)/mac_attempt_test.ihx: $(MAC_ATTEMPT_OBJECTS) $(BUILD)/ma_test_mac_attempt.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(MAC_ATTEMPT_OBJECTS) $(BUILD)/ma_test_mac_attempt.rel
	cp $(BUILD)/ma_timebase.rst $(BUILD)/mac_attempt_test.timebase.rst
	cp $(BUILD)/ma_clock.rst $(BUILD)/mac_attempt_test.clock.rst
	cp $(BUILD)/ma_mac_time.rst $(BUILD)/mac_attempt_test.mac_time.rst
	cp $(BUILD)/ma_radio_autoack.rst $(BUILD)/mac_attempt_test.radio_autoack.rst
	cp $(BUILD)/ma_mac_epoch.rst $(BUILD)/mac_attempt_test.mac_epoch.rst
	cp $(BUILD)/ma_mac_radio.rst $(BUILD)/mac_attempt_test.mac_radio.rst
	cp $(BUILD)/ma_mac_attempt.rst $(BUILD)/mac_attempt_test.mac_attempt.rst
	cp $(BUILD)/ma_test_mac_attempt.rst $(BUILD)/mac_attempt_test.test_mac_attempt.rst

$(BUILD)/host-mac-attempt-tests: tests/test_mac_attempt.c tests/test_radio_autoack.c $(MAC_ATTEMPT_SRC) tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) $(MAC_ATTEMPT_DEFINES) tests/test_mac_attempt.c $(MAC_ATTEMPT_SRC) tests/host_mmio.c -o $@

$(BUILD)/host-mac-attempt-tests-sanitize: tests/test_mac_attempt.c tests/test_radio_autoack.c $(MAC_ATTEMPT_SRC) tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) $(MAC_ATTEMPT_DEFINES) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_attempt.c $(MAC_ATTEMPT_SRC) tests/host_mmio.c -o $@

test-mac-attempt: $(BUILD)/host-mac-attempt-tests $(BUILD)/host-mac-attempt-tests-sanitize $(BUILD)/mac_attempt_test.ihx
	$(BUILD)/host-mac-attempt-tests
	$(BUILD)/host-mac-attempt-tests-sanitize
	$(PYTHON) -B tests/boot_mac_attempt.py --output $(BUILD) --simulator "$(S51)"

.PHONY: test-mac-epoch
$(BUILD)/mac_epoch.rel: src/mac_epoch.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_epoch_test.rel: tests/test_mac_epoch.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_epoch_test.ihx: $(BUILD)/mac_epoch.rel $(BUILD)/mac_epoch_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_epoch.rel $(BUILD)/mac_epoch_test.rel
	cp $(BUILD)/mac_epoch.rst $(BUILD)/mac_epoch_test.mac_epoch.rst
	cp $(BUILD)/mac_epoch_test.rst $(BUILD)/mac_epoch_test.mac_epoch_test.rst

$(BUILD)/host-mac-epoch-tests: tests/test_mac_epoch.c src/mac_epoch.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_epoch.c src/mac_epoch.c -o $@

$(BUILD)/host-mac-epoch-tests-sanitize: tests/test_mac_epoch.c src/mac_epoch.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_epoch.c src/mac_epoch.c -o $@

test-mac-epoch: $(BUILD)/host-mac-epoch-tests $(BUILD)/host-mac-epoch-tests-sanitize $(BUILD)/mac_epoch_test.ihx
	$(BUILD)/host-mac-epoch-tests
	$(BUILD)/host-mac-epoch-tests-sanitize
	$(PYTHON) -B tests/boot_mac_epoch.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/mac_stamp.rel: src/mac_stamp.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_stamp_test.rel: tests/test_mac_stamp.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_stamp_test.ihx: $(BUILD)/mac_epoch.rel $(BUILD)/mac_stamp.rel $(BUILD)/mac_stamp_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_epoch.rel $(BUILD)/mac_stamp.rel $(BUILD)/mac_stamp_test.rel
	cp $(BUILD)/mac_epoch.rst $(BUILD)/mac_stamp_test.mac_epoch.rst
	cp $(BUILD)/mac_stamp.rst $(BUILD)/mac_stamp_test.mac_stamp.rst
	cp $(BUILD)/mac_stamp_test.rst $(BUILD)/mac_stamp_test.mac_stamp_test.rst

MAC_STAMP_SRC := src/mac_epoch.c src/mac_stamp.c
$(BUILD)/host-mac-stamp-tests: tests/test_mac_stamp.c $(MAC_STAMP_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_stamp.c $(MAC_STAMP_SRC) -o $@

$(BUILD)/host-mac-stamp-tests-sanitize: tests/test_mac_stamp.c $(MAC_STAMP_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_stamp.c $(MAC_STAMP_SRC) -o $@

test-mac-stamp: $(BUILD)/host-mac-stamp-tests $(BUILD)/host-mac-stamp-tests-sanitize $(BUILD)/mac_stamp_test.ihx
	$(BUILD)/host-mac-stamp-tests
	$(BUILD)/host-mac-stamp-tests-sanitize
	$(PYTHON) -B tests/boot_mac_stamp.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/mac_time_test.rel: tests/test_mac_time.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_time_test.ihx: $(BUILD)/timebase.rel $(BUILD)/mac_time.rel $(BUILD)/mac_time_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/timebase.rel $(BUILD)/mac_time.rel $(BUILD)/mac_time_test.rel
	cp $(BUILD)/timebase.rst $(BUILD)/mac_time_test.timebase.rst
	cp $(BUILD)/mac_time.rst $(BUILD)/mac_time_test.mac_time.rst
	cp $(BUILD)/mac_time_test.rst $(BUILD)/mac_time_test.test_mac_time.rst

$(BUILD)/host-mac-time-tests: tests/test_mac_time.c src/mac_time.c src/timebase.c tests/host_mmio.c tests/host_mmio.h $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_time.c src/timebase.c src/mac_time.c tests/host_mmio.c -o $@

test-mac-time: $(BUILD)/host-mac-time-tests $(BUILD)/mac_time_test.ihx
	$(BUILD)/host-mac-time-tests
	$(PYTHON) -B tests/boot_mac_time.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/mac_scan.rel: src/mac_scan.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_scan_test.rel: tests/test_mac_scan.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_scan_test.ihx: $(BUILD)/mac_frame.rel $(BUILD)/mac_tx.rel $(BUILD)/nwk_beacon.rel $(BUILD)/nwk_candidates.rel $(BUILD)/mac_scan.rel $(BUILD)/mac_scan_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_frame.rel $(BUILD)/mac_tx.rel $(BUILD)/nwk_beacon.rel $(BUILD)/nwk_candidates.rel $(BUILD)/mac_scan.rel $(BUILD)/mac_scan_test.rel
	cp $(BUILD)/mac_frame.rst $(BUILD)/mac_scan_test.mac_frame.rst
	cp $(BUILD)/mac_tx.rst $(BUILD)/mac_scan_test.mac_tx.rst
	cp $(BUILD)/nwk_beacon.rst $(BUILD)/mac_scan_test.nwk_beacon.rst
	cp $(BUILD)/nwk_candidates.rst $(BUILD)/mac_scan_test.nwk_candidates.rst
	cp $(BUILD)/mac_scan.rst $(BUILD)/mac_scan_test.mac_scan.rst
	cp $(BUILD)/mac_scan_test.rst $(BUILD)/mac_scan_test.mac_scan_test.rst

$(BUILD)/host-mac-scan-tests: tests/test_mac_scan.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/mac_scan.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_scan.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/mac_scan.c -o $@

test-mac-scan: $(BUILD)/host-mac-scan-tests $(BUILD)/mac_scan_test.ihx
	$(BUILD)/host-mac-scan-tests
	$(PYTHON) -B tests/boot_mac_scan.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/mac_association.rel: src/mac_association.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_association_test.rel: tests/test_mac_association.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_association_test.ihx: $(BUILD)/mac_frame.rel $(BUILD)/mac_association.rel $(BUILD)/mac_association_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_frame.rel $(BUILD)/mac_association.rel $(BUILD)/mac_association_test.rel
	cp $(BUILD)/mac_frame.rst $(BUILD)/mac_association_test.mac_frame.rst
	cp $(BUILD)/mac_association.rst $(BUILD)/mac_association_test.mac_association.rst
	cp $(BUILD)/mac_association_test.rst $(BUILD)/mac_association_test.mac_association_test.rst

$(BUILD)/host-mac-association-tests: tests/test_mac_association.c src/mac_frame.c src/mac_association.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_association.c src/mac_frame.c src/mac_association.c -o $@

test-mac-association: $(BUILD)/host-mac-association-tests $(BUILD)/mac_association_test.ihx
	$(BUILD)/host-mac-association-tests
	$(PYTHON) -B tests/boot_mac_association.py --output $(BUILD) --simulator "$(S51)"

$(BUILD)/mac_poll.rel: src/mac_poll.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_poll_test.rel: tests/test_mac_poll.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(BUILD)/mac_poll_test.ihx: $(BUILD)/mac_frame.rel $(BUILD)/mac_tx.rel $(BUILD)/mac_association.rel $(BUILD)/mac_poll.rel $(BUILD)/mac_poll_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(BUILD)/mac_frame.rel $(BUILD)/mac_tx.rel $(BUILD)/mac_association.rel $(BUILD)/mac_poll.rel $(BUILD)/mac_poll_test.rel
	cp $(BUILD)/mac_frame.rst $(BUILD)/mac_poll_test.mac_frame.rst
	cp $(BUILD)/mac_tx.rst $(BUILD)/mac_poll_test.mac_tx.rst
	cp $(BUILD)/mac_association.rst $(BUILD)/mac_poll_test.mac_association.rst
	cp $(BUILD)/mac_poll.rst $(BUILD)/mac_poll_test.mac_poll.rst
	cp $(BUILD)/mac_poll_test.rst $(BUILD)/mac_poll_test.mac_poll_test.rst

$(BUILD)/host-mac-poll-tests: tests/test_mac_poll.c src/mac_frame.c src/mac_tx.c src/mac_association.c src/mac_poll.c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_poll.c src/mac_frame.c src/mac_tx.c src/mac_association.c src/mac_poll.c -o $@

test-mac-poll: $(BUILD)/host-mac-poll-tests $(BUILD)/mac_poll_test.ihx
	$(BUILD)/host-mac-poll-tests
	$(PYTHON) -B tests/boot_mac_poll.py --output $(BUILD) --simulator "$(S51)"

MAC_JOIN_MODULES := mac_frame mac_tx mac_association mac_poll mac_join
MAC_JOIN_SRC := $(addprefix src/,$(addsuffix .c,$(MAC_JOIN_MODULES)))
MAC_JOIN_OBJECTS := $(addprefix $(BUILD)/,$(addsuffix .rel,$(MAC_JOIN_MODULES)))
MAC_JOIN_CASES := 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21
MAC_JOIN_IMAGES := $(addprefix $(BUILD)/mac_join_,$(addsuffix _test.ihx,$(MAC_JOIN_CASES)))
$(BUILD)/mac_join.rel: src/mac_join.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -c $< -o $@

$(MAC_JOIN_IMAGES:.ihx=.rel): $(BUILD)/mac_join_%_test.rel: tests/test_mac_join.c $(HEADERS) Makefile | $(BUILD)
	$(SDCC) $(SDCC_FLAGS) -DMAC_JOIN_CASE=$* -c $< -o $@

$(MAC_JOIN_IMAGES): $(BUILD)/mac_join_%_test.ihx: $(MAC_JOIN_OBJECTS) $(BUILD)/mac_join_%_test.rel force-link
	$(SDCC) $(SDCC_FLAGS) $(LINK_FLAGS) -o $@ $(MAC_JOIN_OBJECTS) $(@:.ihx=.rel)
	cp $(BUILD)/mac_frame.rst $(@:.ihx=).mac_frame.rst
	cp $(BUILD)/mac_tx.rst $(@:.ihx=).mac_tx.rst
	cp $(BUILD)/mac_association.rst $(@:.ihx=).mac_association.rst
	cp $(BUILD)/mac_poll.rst $(@:.ihx=).mac_poll.rst
	cp $(BUILD)/mac_join.rst $(@:.ihx=).mac_join.rst
	cp $(@:.ihx=.rst) $(@:.ihx=).mac_join_$*_test.rst

$(BUILD)/host-mac-join-tests: tests/test_mac_join.c $(MAC_JOIN_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_join.c $(MAC_JOIN_SRC) -o $@

$(BUILD)/host-mac-join-tests-sanitize: tests/test_mac_join.c $(MAC_JOIN_SRC) $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_join.c $(MAC_JOIN_SRC) -o $@

test-mac-join: $(BUILD)/host-mac-join-tests $(BUILD)/host-mac-join-tests-sanitize $(MAC_JOIN_IMAGES)
	$(BUILD)/host-mac-join-tests
	$(BUILD)/host-mac-join-tests-sanitize
	$(PYTHON) -B tests/boot_mac_join.py --output $(BUILD) --simulator "$(S51)"

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

$(BUILD)/host-radio-tx-fixture-tests_$(BOARD): tests/test_radio_tx_fixture.c tests/test_radio_tx.c src/radio_tx_fixture_state.c src/radio_tx.c src/radio_fifo.c src/clock.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_tx_fixture.c src/radio_tx_fixture_state.c src/radio_tx.c src/radio_fifo.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-radio-link-fixture-tests_$(BOARD): tests/test_radio_link_fixture.c tests/test_radio_autoack.c src/radio_link_fixture_state.c src/radio_autoack.c src/clock.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_link_fixture.c src/radio_link_fixture_state.c src/radio_autoack.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

$(BUILD)/host-radio-link-fixture-sanitize_$(BOARD): tests/test_radio_link_fixture.c tests/test_radio_autoack.c src/radio_link_fixture_state.c src/radio_autoack.c src/clock.c src/timebase.c tests/host_mmio.c tests/host_mmio.h src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_radio_link_fixture.c src/radio_link_fixture_state.c src/radio_autoack.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $@

.PHONY: test-radio-rx-fixture
test-radio-rx-fixture: all $(BUILD)/host-radio-rx-fixture-tests_$(BOARD)
	$(BUILD)/host-radio-rx-fixture-tests_$(BOARD)
	$(PYTHON) -B tests/boot_image.py --board $(BOARD) --image radio_rx_fixture --output $(BUILD) --simulator "$(S51)"

# Component inputs vary with BOARD, not IMAGE. Keep both compiler definitions,
# but do not repeat the same corpus for every board fixture in test-local.
test-common: test-common-core test-mac-radio test-mac-stamp test-zcl-temperature test-mac-attempt test-mac-join test-zdo-node test-zdo-srv test-zigbee-security test-zigbee-mmo test-zigbee-key-hash test-security-counter
test-common-core: test-protocol-frame test-protocol-budget test-zcl-frame test-zcl-value test-zcl-attributes test-zcl-dispatch test-zcl-basic test-zcl-identify test-radio-rx test-radio-autoack test-radio-queue test-radio-tx
test-common-core: test-mac-tx test-nwk-candidates test-nwk-parent test-mac-time test-mac-epoch test-mac-scan test-mac-association test-mac-poll
test-common-core: test-noise-health test-radio-noise
test-common-core: test-timebase test-clock test-irq test-radio-fifo test-dma test-aes test-prng test-flash test-flash-exec test-flash-write test-nv-record test-nwk-beacon test-nwk-frame test-aps-frame $(BUILD)/host-mac-frame-tests $(BUILD)/mac_frame_test.ihx
	$(BUILD)/host-mac-frame-tests
	$(PYTHON) -B tests/boot_mac_frame.py --output $(BUILD) --simulator "$(S51)"

test-tools:
	$(PYTHON) -B -m unittest discover -s tools -p 'test_*.py' -v

test: test-common test-tools test-board

test-local:
	+$(MAKE) --no-print-directory -j1 test-tools
	+set -e; for board in generic lg_esl29_rev03; do \
		$(MAKE) --no-print-directory -j1 BOARD=$$board IMAGE=bringup BUILD="$(LOCAL_BUILD)/$$board/components" test-common; \
		for image in $(BOARD_IMAGES); do \
			$(MAKE) --no-print-directory -j1 BOARD=$$board IMAGE=$$image BUILD="$(LOCAL_BUILD)/$$board/$$image" test-board; \
		done; \
	done

test-board: $(if $(filter flash_fixture,$(IMAGE)),$(BUILD)/host-flash-fixture-tests_$(BOARD))
test-board: $(if $(filter radio_rx_fixture,$(IMAGE)),$(BUILD)/host-radio-rx-fixture-tests_$(BOARD))
test-board: $(if $(filter radio_tx_fixture,$(IMAGE)),$(BUILD)/host-radio-tx-fixture-tests_$(BOARD))
test-board: $(if $(filter radio_noise_fixture,$(IMAGE)),$(BUILD)/host-radio-noise-fixture-tests_$(BOARD) test-radio-noise-fixture-sanitize)
test-board: $(if $(filter radio_link_fixture,$(IMAGE)),$(BUILD)/host-radio-link-fixture-tests_$(BOARD) $(BUILD)/host-radio-link-fixture-sanitize_$(BOARD))
test-board: $(if $(filter aes_fixture,$(IMAGE)),$(BUILD)/host-aes-fixture-tests_$(BOARD) $(BUILD)/aes-reference)
test-board: $(if $(filter prng_fixture,$(IMAGE)),$(BUILD)/host-prng-fixture-tests_$(BOARD))
test-board: $(if $(filter radio_fifo_fixture,$(IMAGE)),$(BUILD)/host-radio-fifo-fixture-tests_$(BOARD))
test-board: $(if $(filter dma_fixture,$(IMAGE)),$(BUILD)/host-dma-fixture-tests_$(BOARD))
test-board: all $(BUILD)/host-tests_$(BOARD) $(if $(filter debug_fixture,$(IMAGE)),$(BUILD)/host-fixture-tests_$(BOARD)) $(if $(filter timebase_fixture,$(IMAGE)),$(BUILD)/host-timebase-fixture-tests_$(BOARD) $(BUILD)/host-timebase-failure-tests_$(BOARD)) $(if $(filter clock_fixture,$(IMAGE)),$(BUILD)/host-clock-fixture-tests_$(BOARD)) $(if $(filter irq_fixture,$(IMAGE)),$(BUILD)/host-irq-fixture-tests_$(BOARD))
	$(BUILD)/host-tests_$(BOARD)
ifeq ($(IMAGE),flash_fixture)
	$(BUILD)/host-flash-fixture-tests_$(BOARD)
endif
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
ifeq ($(IMAGE),radio_tx_fixture)
	$(BUILD)/host-radio-tx-fixture-tests_$(BOARD)
endif
ifeq ($(IMAGE),radio_noise_fixture)
	$(BUILD)/host-radio-noise-fixture-tests_$(BOARD)
endif
ifeq ($(IMAGE),radio_link_fixture)
	$(BUILD)/host-radio-link-fixture-tests_$(BOARD)
	$(BUILD)/host-radio-link-fixture-sanitize_$(BOARD)
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
	$(PYTHON) -B tests/boot_image.py --board $(BOARD) --image $(IMAGE) --output $(BUILD) --simulator "$(S51)"
