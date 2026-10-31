# SPDX-License-Identifier: BSD-3-Clause
.DEFAULT_GOAL := all
PYTHON ?= python3
HOST_CC ?= cc
BOARD ?= lg_esl29_rev03
IMAGE ?= join
BUILD ?= build/$(BOARD)/$(IMAGE)
LLVM_TOOLS ?= ../llvm-mcs51-build/bin
LLVM_SRC ?= ../llvm-project
LLVM_REVISION ?=
LLVM_TREE ?=
LLVM_LIT_EVIDENCE ?=
# The join image defaults to known-router parent recovery; other images are production-only.
LLVM_VARIANT ?= $(if $(filter join,$(IMAGE)),parent-recovery,production)
LLVM_TIER_OUT ?= build/llvm-tiers
LLVM_TIER_ARGS ?=
LLVM_SIM_ARCHIVE ?= build/sdcc_4.2.0+dfsg.orig.tar.xz
LLVM_SIM_PROFILE ?= reference
LLVM_SIM_DIR ?= build/llvm-sim/$(LLVM_SIM_PROFILE)
LLVM_S51 ?= $(LLVM_SIM_DIR)/sdcc-4.2.0+dfsg/sim/ucsim/s51.src/s51
JOIN_SMOKE_KEY_MODE ?= default-tc
ifneq ($(filter $(JOIN_SMOKE_KEY_MODE),install-code default-tc),$(JOIN_SMOKE_KEY_MODE))
$(error JOIN_SMOKE_KEY_MODE must be install-code or default-tc)
endif
JOIN_SMOKE_PARENT_RECOVERY ?= 1
JOIN_SMOKE_REPLAY_POLICY_C ?= 0
ifneq ($(filter $(JOIN_SMOKE_PARENT_RECOVERY),0 1),$(JOIN_SMOKE_PARENT_RECOVERY))
$(error JOIN_SMOKE_PARENT_RECOVERY must be 0 or 1)
endif
ifneq ($(filter $(JOIN_SMOKE_REPLAY_POLICY_C),0 1),$(JOIN_SMOKE_REPLAY_POLICY_C))
$(error JOIN_SMOKE_REPLAY_POLICY_C must be 0 or 1)
endif
ifeq ($(JOIN_SMOKE_REPLAY_POLICY_C),1)
ifneq ($(JOIN_SMOKE_PARENT_RECOVERY),1)
$(error JOIN_SMOKE_REPLAY_POLICY_C requires JOIN_SMOKE_PARENT_RECOVERY=1)
endif
endif
JOIN_SMOKE_RECOVERY_DEFINES := $(if $(filter 1,$(JOIN_SMOKE_PARENT_RECOVERY)),-DCC2530_PARENT_RECOVERY) $(if $(filter 1,$(JOIN_SMOKE_REPLAY_POLICY_C)),-DCC2530_REPLAY_POLICY_C)

ifeq ($(BOARD),generic)
BOARD_NUMBER := 0
else ifeq ($(BOARD),lg_esl29_rev03)
BOARD_NUMBER := 1
else
$(error BOARD must be generic or lg_esl29_rev03)
endif
HEADERS := $(wildcard include/*.h src/*_internal.h tests/*.h)
HOST_FLAGS := -std=c99 -O2 -Wall -Wextra -Werror -pedantic -DCC2530_HOST_TEST -Iinclude -DCC2530_BOARD=$(BOARD_NUMBER) -Itests
LLVM_TIER = $(PYTHON) -B tools/llvm/tiers.py --tools "$(abspath $(LLVM_TOOLS))" --llvm-src "$(abspath $(LLVM_SRC))" \
	--variant "$(LLVM_VARIANT)" --simulator "$(abspath $(LLVM_S51))" \
	$(if $(LLVM_TREE),--llvm-build "$(abspath $(LLVM_TREE))") \
	$(if $(LLVM_LIT_EVIDENCE),--lit-evidence "$(abspath $(LLVM_LIT_EVIDENCE))") $(LLVM_TIER_ARGS)

.PHONY: all firmware llvm test test-fast test-smoke test-full test-llvm-fast test-llvm-smoke test-llvm-full test-host test-m5-m6-host test-join-smoke-host llvm-simulator FORCE
all: firmware
firmware llvm:
	$(PYTHON) -B tools/llvm/firmware.py --tools "$(abspath $(LLVM_TOOLS))" --llvm-src "$(abspath $(LLVM_SRC))" \
		--board "$(BOARD)" --image "$(IMAGE)" --variant "$(LLVM_VARIANT)" --output "$(BUILD)" \
		$(if $(LLVM_REVISION),--llvm-revision "$(LLVM_REVISION)")
test: test-host test-smoke
test-fast test-llvm-fast:
	$(LLVM_TIER) --tier fast --output "$(LLVM_TIER_OUT)/fast"
test-smoke test-llvm-smoke:
	$(LLVM_TIER) --tier smoke --output "$(LLVM_TIER_OUT)/smoke"
test-full test-llvm-full:
	$(LLVM_TIER) --tier full --output "$(LLVM_TIER_OUT)/full"
llvm-simulator: $(LLVM_S51)
$(LLVM_S51):
	$(PYTHON) -B tools/llvm/prepare_simulator.py --archive "$(LLVM_SIM_ARCHIVE)" --output "$(LLVM_SIM_DIR)" --profile "$(LLVM_SIM_PROFILE)"

$(BUILD):
	mkdir -p "$@"
$(BUILD)/join-smoke-layout/join_smoke_layout.h: tools/llvm/ref/join_smoke_layout.h
	mkdir -p "$(@D)"
	cp "$<" "$@"

include mk/host-tests.mk
# Some native tests include C helper implementations, not only headers.
$(HOST_PROGRAMS): mk/host-tests.mk $(wildcard tests/*.c) $(BUILD)/host-profile.txt
$(BUILD)/host-profile.txt: FORCE | $(BUILD)
	printf '%s\n' '$(BOARD)' '$(JOIN_SMOKE_KEY_MODE)' '$(JOIN_SMOKE_RECOVERY_DEFINES)' '$(HOST_CC)' '$(HOST_FLAGS)' > "$@.tmp"
	cmp -s "$@.tmp" "$@" || mv "$@.tmp" "$@"
	rm -f "$@.tmp"
FORCE:

HOST_CHECKS := host-nwk-frame-tests host-aps-frame-tests host-zcl-frame-tests host-nv-record-tests host-security-counter-tests
test-host: $(addprefix $(BUILD)/,$(HOST_CHECKS)) $(BUILD)/host-join-smoke
	set -e; for name in $(HOST_CHECKS); do "$(BUILD)/$$name"; done
	set -e; for n in 0 2 4 9 25 26; do "$(BUILD)/host-join-smoke" $$n; done

M5_M6_CHECKS := host-security-keys-tests host-bdb-join-tests host-parent-storage-tests host-parent-storage-audit-tests host-parent-recovery-tests host-parent-recovery-policy-c-tests host-parent-candidates-tests host-replay-policy-c-tests host-replay-log-reference-tests host-security-receive-completion-tests
M5_M6_PROGRAMS := $(M5_M6_CHECKS) $(addsuffix -sanitize,$(M5_M6_CHECKS)) host-nwk-parent-tests host-nwk-parent-sanitize host-zcl-sensor host-zcl-sensor-sanitize host-report-completion host-report-completion-sanitize host-mac-link-e2e-tests
test-m5-m6-host: $(addprefix $(BUILD)/,$(M5_M6_PROGRAMS))
	set -e; for name in $(M5_M6_PROGRAMS); do "$(BUILD)/$$name"; done

JOIN_SMOKE_HOST_CASES := 0 1 2 3 4 5 6 8 9 10 16 17 25 26 27 $(if $(filter default-tc,$(JOIN_SMOKE_KEY_MODE)),11 12 13 14 15) $(if $(filter 1,$(JOIN_SMOKE_PARENT_RECOVERY)),18 19 20) $(if $(filter 1,$(JOIN_SMOKE_REPLAY_POLICY_C)),21 22 23 24)
test-join-smoke-host: $(addprefix $(BUILD)/,host-join-smoke host-join-smoke-sanitize host-join-smoke-deep host-join-smoke-deep-sanitize host-zcl-sensor host-zcl-sensor-sanitize)
	"$(BUILD)/host-zcl-sensor"
	"$(BUILD)/host-zcl-sensor-sanitize"
	set -e; for n in $(JOIN_SMOKE_HOST_CASES); do \
		"$(BUILD)/host-join-smoke" $$n > "$(BUILD)/join-smoke-$$n.txt"; \
		"$(BUILD)/host-join-smoke-sanitize" $$n > "$(BUILD)/join-smoke-$$n-sanitize.txt"; \
		"$(BUILD)/host-join-smoke-deep" $$n > "$(BUILD)/join-smoke-$$n-deep.txt"; \
		"$(BUILD)/host-join-smoke-deep-sanitize" $$n > "$(BUILD)/join-smoke-$$n-deep-sanitize.txt"; \
		cmp "$(BUILD)/join-smoke-$$n.txt" "$(BUILD)/join-smoke-$$n-deep.txt"; \
		cmp "$(BUILD)/join-smoke-$$n-sanitize.txt" "$(BUILD)/join-smoke-$$n-deep-sanitize.txt"; \
		cat "$(BUILD)/join-smoke-$$n.txt" "$(BUILD)/join-smoke-$$n-sanitize.txt"; done
