# SPDX-License-Identifier: BSD-3-Clause
# Explicit OFFLINE-only build; never a default image, upload or hardware action.
MAC_SMOKE_DIR := $(BUILD)/mac-smoke
MAC_SMOKE_FLAGS := $(MAC_ADAPTER_BANK_FLAGS)
MAC_SMOKE_MODULES := mac_smoke_iram_low banked mac_smoke_iram_high $(MAC_ADAPTER_MODULES) startup status $(BOARD) mac_smoke mac_smoke_main
MAC_SMOKE_OBJECTS := $(addprefix $(MAC_SMOKE_DIR)/,$(addsuffix .rel,$(MAC_SMOKE_MODULES)))
MAC_SMOKE_LINK := $(filter-out -Wl-bMA_CALLER=0x08,$(MAC_ADAPTER_BANK_LINK)) -Wl-bMS_START=0x08 -Wl-bMS_STATUS=0x08 -Wl-bMS_BOARD=0x08 -Wl-bMS_CALLER=0x08 -Wl-bMS_MAIN=0x08
$(MAC_SMOKE_DIR):
	mkdir -p $@
define MAC_SMOKE_MODULE
$(MAC_SMOKE_DIR)/$(1).rel: src/$(1).c $(HEADERS) mac-smoke.mk | $(MAC_SMOKE_DIR)
	$$(SDCC) $$(MAC_SMOKE_FLAGS) --dataseg $(if $(filter banked,$(1)),DSEG,MA_$(1)) \
		$(if $(filter mac_tx mac_frame,$(1)),--codeseg MA_BANK1) $(if $(filter mac_adapter,$(1)),--codeseg MA_BANK2) -c $$< -o $$@
endef
$(foreach m,$(MAC_ADAPTER_BANK_MODULES),$(eval $(call MAC_SMOKE_MODULE,$(m))))
$(MAC_SMOKE_DIR)/mac_smoke_iram_%.rel: src/mac_smoke_iram_%.c mac-smoke.mk | $(MAC_SMOKE_DIR)
	$(SDCC) $(MAC_SMOKE_FLAGS) -c $< -o $@
$(MAC_SMOKE_DIR)/startup.rel: src/startup.c $(HEADERS) mac-smoke.mk | $(MAC_SMOKE_DIR)
	$(SDCC) $(MAC_SMOKE_FLAGS) --dataseg MS_START -c $< -o $@
$(MAC_SMOKE_DIR)/status.rel: src/status.c $(HEADERS) mac-smoke.mk | $(MAC_SMOKE_DIR)
	$(SDCC) $(MAC_SMOKE_FLAGS) --dataseg MS_STATUS -c $< -o $@
$(MAC_SMOKE_DIR)/$(BOARD).rel: boards/$(BOARD).c $(HEADERS) mac-smoke.mk | $(MAC_SMOKE_DIR)
	$(SDCC) $(MAC_SMOKE_FLAGS) --dataseg MS_BOARD -c $< -o $@
$(MAC_SMOKE_DIR)/mac_smoke.rel: src/mac_smoke.c $(HEADERS) mac-smoke.mk | $(MAC_SMOKE_DIR)
	$(SDCC) $(MAC_SMOKE_FLAGS) --dataseg MS_CALLER -c $< -o $@
$(MAC_SMOKE_DIR)/mac_smoke_main.rel: examples/mac_smoke_main.c $(HEADERS) mac-smoke.mk | $(MAC_SMOKE_DIR)
	$(SDCC) $(MAC_SMOKE_FLAGS) --dataseg MS_MAIN -c $< -o $@
$(MAC_SMOKE_DIR)/mac_smoke.ihx: $(MAC_SMOKE_OBJECTS)
	$(SDCC) $(SDCC_FLAGS) $(MAC_SMOKE_LINK) -o $@ $(MAC_SMOKE_OBJECTS)
	set -e; $(foreach m,$(MAC_SMOKE_MODULES),cp $(MAC_SMOKE_DIR)/$(m).rst $(MAC_SMOKE_DIR)/mac_smoke.$(m).rst;)
.PHONY: mac-smoke
mac-smoke: $(MAC_SMOKE_DIR)/mac_smoke.physical.hex
$(MAC_SMOKE_DIR)/mac_smoke.physical.hex: $(MAC_SMOKE_DIR)/mac_smoke.ihx tests/verify_mac_smoke.py tests/mac_smoke_pins.json
	$(PYTHON) -B tests/verify_mac_smoke.py --board $(BOARD) --output $(MAC_SMOKE_DIR) --pack $@

# The shared host model's lexical include closure contains its generated trace layout.
MAC_SMOKE_HOST_SRC := tests/test_mac_smoke.c src/mac_smoke.c $(MAC_ADAPTER_SRC) tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c
$(MAC_SMOKE_DIR)/host-smoke: $(MAC_SMOKE_HOST_SRC) $(HEADERS) $(MAC_ADAPTER_BANK_DIR)/mac_adapter_layout.h mac-smoke.mk | $(MAC_SMOKE_DIR)
	$(HOST_CC) $(HOST_FLAGS) $(MAC_ADAPTER_DEFINES) -I$(MAC_ADAPTER_BANK_DIR) $(MAC_SMOKE_HOST_SRC) -o $@
$(MAC_SMOKE_DIR)/host-smoke-sanitize: $(MAC_SMOKE_HOST_SRC) $(HEADERS) $(MAC_ADAPTER_BANK_DIR)/mac_adapter_layout.h mac-smoke.mk | $(MAC_SMOKE_DIR)
	$(HOST_CC) $(HOST_FLAGS) $(MAC_ADAPTER_DEFINES) -I$(MAC_ADAPTER_BANK_DIR) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie $(MAC_SMOKE_HOST_SRC) -o $@
.PHONY: test-mac-smoke-native
test-mac-smoke-native: $(MAC_SMOKE_DIR)/host-smoke $(MAC_SMOKE_DIR)/host-smoke-sanitize
	set -e; for n in 0 1 2 3 4 5 6 7 8 9; do $(MAC_SMOKE_DIR)/host-smoke $$n; $(MAC_SMOKE_DIR)/host-smoke-sanitize $$n; done
.PHONY: test-mac-smoke
test-mac-smoke: mac-smoke test-mac-smoke-native
	$(PYTHON) -B tools/test_mac_smoke.py --board $(BOARD) --output $(MAC_SMOKE_DIR)
	$(PYTHON) -B tests/boot_mac_smoke.py --board $(BOARD) --output $(MAC_SMOKE_DIR) --simulator "$(S51)"
