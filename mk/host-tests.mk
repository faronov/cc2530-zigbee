# SPDX-License-Identifier: BSD-3-Clause
# Native recipes extracted unchanged from the pre-migration GNU Make expansion.
# Compiler-assisted SDCC trace generators are archived; LLVM join vectors remain.
HOST_PROGRAMS := $(BUILD)/aes-reference \
	$(BUILD)/host-aes-fixture-tests_$(BOARD) \
	$(BUILD)/host-aes-tests \
	$(BUILD)/host-aps-frame-tests \
	$(BUILD)/host-bdb-join-tests \
	$(BUILD)/host-bdb-join-tests-sanitize \
	$(BUILD)/host-clock-failure-tests \
	$(BUILD)/host-clock-fixture-tests_$(BOARD) \
	$(BUILD)/host-clock-tests \
	$(BUILD)/host-dma-fixture-tests_$(BOARD) \
	$(BUILD)/host-dma-tests \
	$(BUILD)/host-ed-wire-tests \
	$(BUILD)/host-ed-wire-tests-sanitize \
	$(BUILD)/host-fixture-tests_$(BOARD) \
	$(BUILD)/host-flash-exec-tests \
	$(BUILD)/host-flash-fixture-tests_$(BOARD) \
	$(BUILD)/host-flash-tests \
	$(BUILD)/host-flash-write-tests \
	$(BUILD)/host-irq-fixture-tests_$(BOARD) \
	$(BUILD)/host-irq-tests \
	$(BUILD)/host-join-smoke \
	$(BUILD)/host-join-smoke-deep \
	$(BUILD)/host-join-smoke-deep-sanitize \
	$(BUILD)/host-join-smoke-sanitize \
	$(BUILD)/host-join-smoke-vectors \
	$(BUILD)/host-join-smoke-vectors-sanitize \
	$(BUILD)/host-mac-adapter-tests \
	$(BUILD)/host-mac-adapter-tests-sanitize \
	$(BUILD)/host-mac-association-tests \
	$(BUILD)/host-mac-attempt-rx-diag-tests \
	$(BUILD)/host-mac-attempt-tests \
	$(BUILD)/host-mac-attempt-tests-sanitize \
	$(BUILD)/host-mac-epoch-tests \
	$(BUILD)/host-mac-epoch-tests-sanitize \
	$(BUILD)/host-mac-frame-tests \
	$(BUILD)/host-mac-handoff-mac-tests \
	$(BUILD)/host-mac-handoff-mac-tests-sanitize \
	$(BUILD)/host-mac-handoff-tests \
	$(BUILD)/host-mac-handoff-tests-sanitize \
	$(BUILD)/host-mac-interval-radio-tests \
	$(BUILD)/host-mac-interval-radio-tests-sanitize \
	$(BUILD)/host-mac-join-tests \
	$(BUILD)/host-mac-join-tests-sanitize \
	$(BUILD)/host-mac-link-child-crypto-tests \
	$(BUILD)/host-mac-link-child-crypto-tests-sanitize \
	$(BUILD)/host-mac-link-child-join-tests \
	$(BUILD)/host-mac-link-child-join-tests-sanitize \
	$(BUILD)/host-mac-link-child-nv-tests \
	$(BUILD)/host-mac-link-child-nv-tests-sanitize \
	$(BUILD)/host-mac-link-child-upper-tests \
	$(BUILD)/host-mac-link-child-upper-tests-sanitize \
	$(BUILD)/host-mac-link-deep \
	$(BUILD)/host-mac-link-deep-sanitize \
	$(BUILD)/host-mac-link-direct-join-tests \
	$(BUILD)/host-mac-link-direct-join-tests-sanitize \
	$(BUILD)/host-mac-link-direct-nv-tests \
	$(BUILD)/host-mac-link-direct-nv-tests-sanitize \
	$(BUILD)/host-mac-link-direct-poison-tests \
	$(BUILD)/host-mac-link-direct-tests \
	$(BUILD)/host-mac-link-direct-tests-sanitize \
	$(BUILD)/host-mac-link-direct-upper-tests \
	$(BUILD)/host-mac-link-direct-upper-tests-sanitize \
	$(BUILD)/host-mac-link-e2e-tests \
	$(BUILD)/host-mac-link-e2e-tests-sanitize \
	$(BUILD)/host-mac-link-join-tests \
	$(BUILD)/host-mac-link-join-tests-sanitize \
	$(BUILD)/host-mac-link-projection-tests \
	$(BUILD)/host-mac-link-projection-tests-sanitize \
	$(BUILD)/host-mac-link-ram-join-tests \
	$(BUILD)/host-mac-link-ram-join-tests-sanitize \
	$(BUILD)/host-mac-link-ram-tests \
	$(BUILD)/host-mac-link-ram-tests-sanitize \
	$(BUILD)/host-mac-link-scan-tests \
	$(BUILD)/host-mac-link-scan-tests-sanitize \
	$(BUILD)/host-mac-link-shallow \
	$(BUILD)/host-mac-link-shallow-sanitize \
	$(BUILD)/host-mac-link-workspace-join-tests \
	$(BUILD)/host-mac-link-workspace-join-tests-sanitize \
	$(BUILD)/host-mac-link-workspace-tests \
	$(BUILD)/host-mac-link-workspace-tests-sanitize \
	$(BUILD)/host-mac-observed-tests \
	$(BUILD)/host-mac-observed-tests-sanitize \
	$(BUILD)/host-mac-poll-tests \
	$(BUILD)/host-mac-radio-tests \
	$(BUILD)/host-mac-radio-tests-sanitize \
	$(BUILD)/host-mac-reconfig-tests \
	$(BUILD)/host-mac-reconfig-tests-sanitize \
	$(BUILD)/host-mac-scan-tests \
	$(BUILD)/host-mac-stamp-tests \
	$(BUILD)/host-mac-stamp-tests-sanitize \
	$(BUILD)/host-mac-time-tests \
	$(BUILD)/host-mac-tx-interval-tests \
	$(BUILD)/host-mac-tx-interval-tests-sanitize \
	$(BUILD)/host-mac-tx-tests \
	$(BUILD)/host-noise-health-tests \
	$(BUILD)/host-nv-record-tests \
	$(BUILD)/host-nwk-beacon-tests \
	$(BUILD)/host-nwk-candidates-tests \
	$(BUILD)/host-nwk-frame-tests \
	$(BUILD)/host-nwk-parent-sanitize \
	$(BUILD)/host-nwk-parent-tests \
	$(BUILD)/host-prng-fixture-tests_$(BOARD) \
	$(BUILD)/host-prng-tests \
	$(BUILD)/host-protocol-budget-tests \
	$(BUILD)/host-protocol-frame-tests \
	$(BUILD)/host-radio-autoack-link-loss-tests \
	$(BUILD)/host-radio-autoack-loss-tests \
	$(BUILD)/host-radio-autoack-rx-diag-link-loss-tests \
	$(BUILD)/host-radio-autoack-rx-diag-loss-tests \
	$(BUILD)/host-radio-autoack-rx-diag-tests \
	$(BUILD)/host-radio-autoack-tests \
	$(BUILD)/host-radio-autoack-tests-sanitize \
	$(BUILD)/host-radio-fifo-fixture-tests_$(BOARD) \
	$(BUILD)/host-radio-fifo-tests \
	$(BUILD)/host-radio-link-fixture-sanitize_$(BOARD) \
	$(BUILD)/host-radio-link-fixture-tests_$(BOARD) \
	$(BUILD)/host-radio-noise-fixture-sanitize_$(BOARD) \
	$(BUILD)/host-radio-noise-fixture-tests_$(BOARD) \
	$(BUILD)/host-radio-noise-tests \
	$(BUILD)/host-radio-queue-tests \
	$(BUILD)/host-radio-rx-fixture-tests_$(BOARD) \
	$(BUILD)/host-radio-rx-tests \
	$(BUILD)/host-radio-tx-fixture-tests_$(BOARD) \
	$(BUILD)/host-radio-tx-tests \
	$(BUILD)/host-security-counter-tests \
	$(BUILD)/host-security-counter-tests-sanitize \
	$(BUILD)/host-security-keys-tests \
	$(BUILD)/host-security-keys-tests-sanitize \
	$(BUILD)/host-tests_$(BOARD) \
	$(BUILD)/host-timebase-failure-tests_$(BOARD) \
	$(BUILD)/host-timebase-fixture-tests_$(BOARD) \
	$(BUILD)/host-timebase-tests \
	$(BUILD)/host-zcl-attributes-tests \
	$(BUILD)/host-zcl-basic-tests \
	$(BUILD)/host-zcl-basic-tests-sanitize \
	$(BUILD)/host-zcl-dispatch-tests \
	$(BUILD)/host-zcl-frame-tests \
	$(BUILD)/host-zcl-identify-tests \
	$(BUILD)/host-zcl-identify-tests-sanitize \
	$(BUILD)/host-zcl-sensor \
	$(BUILD)/host-zcl-sensor-sanitize \
	$(BUILD)/host-zcl-temperature-tests \
	$(BUILD)/host-zcl-temperature-tests-sanitize \
	$(BUILD)/host-zcl-value-tests \
	$(BUILD)/host-zdo-node-tests \
	$(BUILD)/host-zdo-node-tests-sanitize \
	$(BUILD)/host-zdo-srv-tests \
	$(BUILD)/host-zdo-srv-tests-sanitize \
	$(BUILD)/host-zigbee-key-hash-tests \
	$(BUILD)/host-zigbee-key-hash-tests-sanitize \
	$(BUILD)/host-zigbee-mmo-tests \
	$(BUILD)/host-zigbee-mmo-tests-sanitize \
	$(BUILD)/host-zigbee-security-tests \
	$(BUILD)/host-zigbee-security-tests-sanitize

$(BUILD)/aes-reference: tests/aes_reference.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DAES_REFERENCE_MAIN tests/aes_reference.c -o $(BUILD)/aes-reference

$(BUILD)/host-aes-fixture-tests_$(BOARD): tests/test_aes_fixture.c tests/aes_reference.c src/aes_fixture_state.c src/aes.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DAES_FIXTURE_HOST_TEST tests/test_aes_fixture.c tests/aes_reference.c src/aes_fixture_state.c src/aes.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-aes-fixture-tests_$(BOARD)

$(BUILD)/host-aes-tests: tests/test_aes.c tests/aes_reference.c tests/host_mmio.c src/aes.c src/timebase.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_aes.c tests/aes_reference.c tests/host_mmio.c src/aes.c src/timebase.c -o $(BUILD)/host-aes-tests

$(BUILD)/host-aps-frame-tests: tests/test_aps_frame.c src/aps_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_aps_frame.c src/aps_frame.c -o $(BUILD)/host-aps-frame-tests

$(BUILD)/host-bdb-join-tests: tests/test_bdb_join.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_bdb_join.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c -o $(BUILD)/host-bdb-join-tests

$(BUILD)/host-bdb-join-tests-sanitize: tests/test_bdb_join.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_bdb_join.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c -o $(BUILD)/host-bdb-join-tests-sanitize

$(BUILD)/host-clock-failure-tests: tests/test_clock.c tests/host_mmio.c src/clock.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCLOCK_FAILURE_TEST tests/test_clock.c tests/host_mmio.c src/clock.c -o $(BUILD)/host-clock-failure-tests

$(BUILD)/host-clock-fixture-tests_$(BOARD): tests/test_clock_fixture.c src/clock_fixture_state.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_clock_fixture.c src/clock_fixture_state.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-clock-fixture-tests_$(BOARD)

$(BUILD)/host-clock-tests: tests/test_clock.c tests/host_mmio.c src/clock.c src/timebase.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_clock.c tests/host_mmio.c src/clock.c src/timebase.c -o $(BUILD)/host-clock-tests

$(BUILD)/host-dma-fixture-tests_$(BOARD): tests/test_dma_fixture.c src/dma_fixture_state.c src/dma.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DDMA_FIXTURE_HOST_TEST tests/test_dma_fixture.c src/dma_fixture_state.c src/dma.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-dma-fixture-tests_$(BOARD)

$(BUILD)/host-dma-tests: tests/test_dma.c tests/host_mmio.c src/dma.c src/timebase.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_dma.c tests/host_mmio.c src/dma.c src/timebase.c -o $(BUILD)/host-dma-tests

$(BUILD)/host-ed-wire-tests: tests/test_ed_wire.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/zigbee_security.c tests/security_aes_model.c tests/host_mmio.c tests/aes_reference.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_ed_wire.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/zigbee_security.c tests/security_aes_model.c tests/host_mmio.c tests/aes_reference.c -o $(BUILD)/host-ed-wire-tests

$(BUILD)/host-ed-wire-tests-sanitize: tests/test_ed_wire.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/zigbee_security.c tests/security_aes_model.c tests/host_mmio.c tests/aes_reference.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_ed_wire.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/zigbee_security.c tests/security_aes_model.c tests/host_mmio.c tests/aes_reference.c -o $(BUILD)/host-ed-wire-tests-sanitize

$(BUILD)/host-fixture-tests_$(BOARD): tests/test_debug_fixture.c tests/host_mmio.c src/startup.c src/status.c src/debug_pattern.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_debug_fixture.c tests/host_mmio.c src/startup.c src/status.c src/debug_pattern.c boards/$(BOARD).c -o $(BUILD)/host-fixture-tests_$(BOARD)

$(BUILD)/host-flash-exec-tests: tests/test_flash_exec.c tests/host_flash_engine.c src/flash_exec.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_flash_exec.c tests/host_flash_engine.c src/flash_exec.c tests/host_mmio.c -o $(BUILD)/host-flash-exec-tests

$(BUILD)/host-flash-fixture-tests_$(BOARD): tests/test_flash_fixture.c tests/host_flash_engine.c src/flash_fixture_state.c src/flash_exec.c src/flash.c src/flash_write.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_flash_fixture.c tests/host_flash_engine.c src/flash_fixture_state.c src/flash_exec.c src/flash.c src/flash_write.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-flash-fixture-tests_$(BOARD)

$(BUILD)/host-flash-tests: tests/test_flash.c src/flash.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_flash.c src/flash.c tests/host_mmio.c -o $(BUILD)/host-flash-tests

$(BUILD)/host-flash-write-tests: tests/test_flash_write.c tests/host_flash_engine.c src/flash_exec.c src/flash.c src/flash_write.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_flash_write.c tests/host_flash_engine.c src/flash_exec.c src/flash.c src/flash_write.c tests/host_mmio.c -o $(BUILD)/host-flash-write-tests

$(BUILD)/host-irq-fixture-tests_$(BOARD): tests/test_irq_fixture.c src/irq_fixture_state.c src/irq.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DIRQ_FIXTURE_HOST_TEST tests/test_irq_fixture.c src/irq_fixture_state.c src/irq.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-irq-fixture-tests_$(BOARD)

$(BUILD)/host-irq-tests: tests/test_irq.c tests/host_mmio.c src/irq.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_irq.c tests/host_mmio.c src/irq.c -o $(BUILD)/host-irq-tests

$(BUILD)/host-join-smoke: tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT $(if $(filter default-tc,$(JOIN_SMOKE_KEY_MODE)),-DCC2530_DEFAULT_TC_KEY) -I. -Isrc -I$(BUILD)/mac-adapter tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-join-smoke

$(BUILD)/host-join-smoke-deep: tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT $(if $(filter default-tc,$(JOIN_SMOKE_KEY_MODE)),-DCC2530_DEFAULT_TC_KEY) -DCC2530_MAC_LINK_DEEP_REFERENCE  	-I. -Isrc -I$(BUILD)/mac-adapter tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-join-smoke-deep

$(BUILD)/host-join-smoke-deep-sanitize: tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT $(if $(filter default-tc,$(JOIN_SMOKE_KEY_MODE)),-DCC2530_DEFAULT_TC_KEY) -DCC2530_MAC_LINK_DEEP_REFERENCE  	-I. -Isrc -I$(BUILD)/mac-adapter -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-join-smoke-deep-sanitize

$(BUILD)/host-join-smoke-sanitize: tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT $(if $(filter default-tc,$(JOIN_SMOKE_KEY_MODE)),-DCC2530_DEFAULT_TC_KEY) -I. -Isrc -I$(BUILD)/mac-adapter  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-join-smoke-sanitize

$(BUILD)/host-join-smoke-vectors: tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk $(BUILD)/join-smoke-layout/join_smoke_layout.h | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT $(if $(filter default-tc,$(JOIN_SMOKE_KEY_MODE)),-DCC2530_DEFAULT_TC_KEY) -DJOIN_SMOKE_TRACE  	-I. -Isrc -I$(BUILD)/mac-adapter -I$(BUILD)/join-smoke-layout tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-join-smoke-vectors

$(BUILD)/host-join-smoke-vectors-sanitize: tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk $(BUILD)/join-smoke-layout/join_smoke_layout.h | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT $(if $(filter default-tc,$(JOIN_SMOKE_KEY_MODE)),-DCC2530_DEFAULT_TC_KEY) -DJOIN_SMOKE_TRACE  	-I. -Isrc -I$(BUILD)/mac-adapter -I$(BUILD)/join-smoke-layout -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_join_smoke.c src/join_smoke.c src/zcl_sensor.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-join-smoke-vectors-sanitize

$(BUILD)/host-mac-adapter-tests: tests/test_mac_adapter.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_frame.c src/mac_tx.c src/mac_adapter.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER tests/test_mac_adapter.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_frame.c src/mac_tx.c src/mac_adapter.c tests/host_mmio.c -o $(BUILD)/host-mac-adapter-tests

$(BUILD)/host-mac-adapter-tests-sanitize: tests/test_mac_adapter.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_frame.c src/mac_tx.c src/mac_adapter.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_adapter.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_frame.c src/mac_tx.c src/mac_adapter.c tests/host_mmio.c -o $(BUILD)/host-mac-adapter-tests-sanitize

$(BUILD)/host-mac-association-tests: tests/test_mac_association.c src/mac_frame.c src/mac_association.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_association.c src/mac_frame.c src/mac_association.c -o $(BUILD)/host-mac-association-tests

$(BUILD)/host-mac-attempt-rx-diag-tests: tests/test_mac_attempt.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_RX_DIAG -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT tests/test_mac_attempt.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c -o $(BUILD)/host-mac-attempt-rx-diag-tests

$(BUILD)/host-mac-attempt-tests: tests/test_mac_attempt.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT tests/test_mac_attempt.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c -o $(BUILD)/host-mac-attempt-tests

$(BUILD)/host-mac-attempt-tests-sanitize: tests/test_mac_attempt.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_attempt.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c -o $(BUILD)/host-mac-attempt-tests-sanitize

$(BUILD)/host-mac-epoch-tests: tests/test_mac_epoch.c src/mac_epoch.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_epoch.c src/mac_epoch.c -o $(BUILD)/host-mac-epoch-tests

$(BUILD)/host-mac-epoch-tests-sanitize: tests/test_mac_epoch.c src/mac_epoch.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_epoch.c src/mac_epoch.c -o $(BUILD)/host-mac-epoch-tests-sanitize

$(BUILD)/host-mac-frame-tests: tests/test_mac_frame.c src/mac_frame.c src/nwk_beacon.c src/nwk_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_frame.c src/mac_frame.c src/nwk_beacon.c src/nwk_frame.c -o $(BUILD)/host-mac-frame-tests

$(BUILD)/host-mac-handoff-mac-tests: tests/test_mac_handoff.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_tx.c src/mac_frame.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL tests/test_mac_handoff.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_tx.c src/mac_frame.c tests/host_mmio.c -o $(BUILD)/host-mac-handoff-mac-tests

$(BUILD)/host-mac-handoff-mac-tests-sanitize: tests/test_mac_handoff.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_tx.c src/mac_frame.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_handoff.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_tx.c src/mac_frame.c tests/host_mmio.c -o $(BUILD)/host-mac-handoff-mac-tests-sanitize

$(BUILD)/host-mac-handoff-tests: tests/test_mac_handoff.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF tests/test_mac_handoff.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c -o $(BUILD)/host-mac-handoff-tests

$(BUILD)/host-mac-handoff-tests-sanitize: tests/test_mac_handoff.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_handoff.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c -o $(BUILD)/host-mac-handoff-tests-sanitize

$(BUILD)/host-mac-interval-radio-tests: tests/test_mac_attempt.c src/mac_tx.c src/mac_frame.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_INTERVAL tests/test_mac_attempt.c src/mac_tx.c src/mac_frame.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c -o $(BUILD)/host-mac-interval-radio-tests

$(BUILD)/host-mac-interval-radio-tests-sanitize: tests/test_mac_attempt.c src/mac_tx.c src/mac_frame.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_INTERVAL -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_attempt.c src/mac_tx.c src/mac_frame.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c tests/host_mmio.c -o $(BUILD)/host-mac-interval-radio-tests-sanitize

$(BUILD)/host-mac-join-tests: tests/test_mac_join.c src/mac_frame.c src/mac_tx.c src/mac_association.c src/mac_poll.c src/mac_join.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_join.c src/mac_frame.c src/mac_tx.c src/mac_association.c src/mac_poll.c src/mac_join.c -o $(BUILD)/host-mac-join-tests

$(BUILD)/host-mac-join-tests-sanitize: tests/test_mac_join.c src/mac_frame.c src/mac_tx.c src/mac_association.c src/mac_poll.c src/mac_join.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_join.c src/mac_frame.c src/mac_tx.c src/mac_association.c src/mac_poll.c src/mac_join.c -o $(BUILD)/host-mac-join-tests-sanitize

$(BUILD)/host-mac-link-child-crypto-tests: tests/test_mac_link_child_workspace.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/ed_wire.c src/nwk_frame.c src/aps_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -Isrc tests/test_mac_link_child_workspace.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/ed_wire.c src/nwk_frame.c src/aps_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c -o $(BUILD)/host-mac-link-child-crypto-tests

$(BUILD)/host-mac-link-child-crypto-tests-sanitize: tests/test_mac_link_child_workspace.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/ed_wire.c src/nwk_frame.c src/aps_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -Isrc  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie  	tests/test_mac_link_child_workspace.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/ed_wire.c src/nwk_frame.c src/aps_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c -o $(BUILD)/host-mac-link-child-crypto-tests-sanitize

$(BUILD)/host-mac-link-child-join-tests: tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -Isrc tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-child-join-tests

$(BUILD)/host-mac-link-child-join-tests-sanitize: tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -Isrc  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie  	tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-child-join-tests-sanitize

$(BUILD)/host-mac-link-child-nv-tests: tests/test_mac_link_child_nv.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -I. -Isrc -I$(BUILD)/mac-adapter  	tests/test_mac_link_child_nv.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-child-nv-tests

$(BUILD)/host-mac-link-child-nv-tests-sanitize: tests/test_mac_link_child_nv.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -I. -Isrc -I$(BUILD)/mac-adapter  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie  	tests/test_mac_link_child_nv.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-child-nv-tests-sanitize

$(BUILD)/host-mac-link-child-upper-tests: tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -I. -Isrc -I$(BUILD)/mac-adapter  	tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-child-upper-tests

$(BUILD)/host-mac-link-child-upper-tests-sanitize: tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -I. -Isrc -I$(BUILD)/mac-adapter  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie  	tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-child-upper-tests-sanitize

$(BUILD)/host-mac-link-deep: tests/test_mac_link_shallow.c src/mac_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -Isrc -DCC2530_MAC_LINK_DEEP_REFERENCE  tests/test_mac_link_shallow.c src/mac_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-deep

$(BUILD)/host-mac-link-deep-sanitize: tests/test_mac_link_shallow.c src/mac_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -Isrc -DCC2530_MAC_LINK_DEEP_REFERENCE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_link_shallow.c src/mac_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-deep-sanitize

$(BUILD)/host-mac-link-direct-join-tests: tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -Isrc tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-direct-join-tests

$(BUILD)/host-mac-link-direct-join-tests-sanitize: tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -Isrc  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie  	tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-direct-join-tests-sanitize

$(BUILD)/host-mac-link-direct-nv-tests: tests/test_mac_link_child_nv.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -I. -Isrc -I$(BUILD)/mac-adapter  	tests/test_mac_link_child_nv.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-direct-nv-tests

$(BUILD)/host-mac-link-direct-nv-tests-sanitize: tests/test_mac_link_child_nv.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -I. -Isrc -I$(BUILD)/mac-adapter  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie  	tests/test_mac_link_child_nv.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-direct-nv-tests-sanitize

$(BUILD)/host-mac-link-direct-poison-tests: tests/mac_link_direct_poison.c tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -I. -Isrc -I$(BUILD)/mac-adapter  	-Wl,--wrap=security_keys_status -Wl,--wrap=bdb_join_start -Wl,--wrap=bdb_join_step -Wl,--wrap=bdb_join_receive -Wl,--wrap=mac_link_driver_step -Wl,--wrap=nwk_aps_step -Wl,--wrap=nwk_aps_transmit -Wl,--wrap=nwk_aps_complete -Wl,--wrap=zdo_runtime_step -Wl,--wrap=zdo_runtime_request -Wl,--wrap=zdo_runtime_broadcast tests/mac_link_direct_poison.c  	tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-direct-poison-tests

$(BUILD)/host-mac-link-direct-tests: tests/test_mac_link_direct.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -I. -Isrc -I$(BUILD)/mac-adapter  	tests/test_mac_link_direct.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-direct-tests

$(BUILD)/host-mac-link-direct-tests-sanitize: tests/test_mac_link_direct.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -I. -Isrc -I$(BUILD)/mac-adapter  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie  	tests/test_mac_link_direct.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-direct-tests-sanitize

$(BUILD)/host-mac-link-direct-upper-tests: tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -I. -Isrc -I$(BUILD)/mac-adapter  	tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-direct-upper-tests

$(BUILD)/host-mac-link-direct-upper-tests-sanitize: tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -I. -Isrc -I$(BUILD)/mac-adapter  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie  	tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_direct.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-direct-upper-tests-sanitize

$(BUILD)/host-mac-link-e2e-tests: tests/test_mac_link_e2e.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -I$(BUILD)/mac-adapter tests/test_mac_link_e2e.c tests/mac_link_peer.c  	tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c -o $(BUILD)/host-mac-link-e2e-tests

$(BUILD)/host-mac-link-e2e-tests-sanitize: tests/test_mac_link_e2e.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -I$(BUILD)/mac-adapter -fsanitize=address,undefined -fno-sanitize-recover=all  	-fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_link_e2e.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c -o $(BUILD)/host-mac-link-e2e-tests-sanitize

$(BUILD)/host-mac-link-join-tests: tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_LINK tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c -o $(BUILD)/host-mac-link-join-tests

$(BUILD)/host-mac-link-join-tests-sanitize: tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_LINK -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c -o $(BUILD)/host-mac-link-join-tests-sanitize

$(BUILD)/host-mac-link-projection-tests: tests/test_mac_link_projection.c tests/mac_attempt_projection.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -Isrc tests/test_mac_link_projection.c tests/mac_attempt_projection.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c tests/host_mmio.c -o $(BUILD)/host-mac-link-projection-tests

$(BUILD)/host-mac-link-projection-tests-sanitize: tests/test_mac_link_projection.c tests/mac_attempt_projection.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -Isrc -fsanitize=address,undefined -fno-sanitize-recover=all  	-fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_link_projection.c tests/mac_attempt_projection.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c tests/host_mmio.c -o $(BUILD)/host-mac-link-projection-tests-sanitize

$(BUILD)/host-mac-link-ram-join-tests: tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c -o $(BUILD)/host-mac-link-ram-join-tests

$(BUILD)/host-mac-link-ram-join-tests-sanitize: tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -fsanitize=address,undefined -fno-sanitize-recover=all  	-fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c -o $(BUILD)/host-mac-link-ram-join-tests-sanitize

$(BUILD)/host-mac-link-ram-tests: tests/test_mac_link_ram.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -Isrc -I$(BUILD)/mac-adapter tests/test_mac_link_ram.c tests/mac_link_peer.c  	tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c -o $(BUILD)/host-mac-link-ram-tests

$(BUILD)/host-mac-link-ram-tests-sanitize: tests/test_mac_link_ram.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -Isrc -I$(BUILD)/mac-adapter -fsanitize=address,undefined -fno-sanitize-recover=all  	-fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_link_ram.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c -o $(BUILD)/host-mac-link-ram-tests-sanitize

$(BUILD)/host-mac-link-scan-tests: tests/test_mac_link_scan.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/mac_scan.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_LINK tests/test_mac_link_scan.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/mac_scan.c -o $(BUILD)/host-mac-link-scan-tests

$(BUILD)/host-mac-link-scan-tests-sanitize: tests/test_mac_link_scan.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/mac_scan.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_LINK -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_link_scan.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/mac_scan.c -o $(BUILD)/host-mac-link-scan-tests-sanitize

$(BUILD)/host-mac-link-shallow: tests/test_mac_link_shallow.c src/mac_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -Isrc   tests/test_mac_link_shallow.c src/mac_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-shallow

$(BUILD)/host-mac-link-shallow-sanitize: tests/test_mac_link_shallow.c src/mac_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT -Isrc  -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_link_shallow.c src/mac_frame.c src/mac_link_workspace.c src/mac_link_child_workspace.c -o $(BUILD)/host-mac-link-shallow-sanitize

$(BUILD)/host-mac-link-workspace-join-tests: tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -Isrc tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c -o $(BUILD)/host-mac-link-workspace-join-tests

$(BUILD)/host-mac-link-workspace-join-tests-sanitize: tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -Isrc  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie  	tests/test_mac_link_join.c src/mac_frame.c src/mac_tx.c src/mac_poll.c src/mac_association.c src/mac_join.c src/mac_link_workspace.c -o $(BUILD)/host-mac-link-workspace-join-tests-sanitize

$(BUILD)/host-mac-link-workspace-tests: tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -Isrc -I$(BUILD)/mac-adapter  	tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c -o $(BUILD)/host-mac-link-workspace-tests

$(BUILD)/host-mac-link-workspace-tests-sanitize: tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -Isrc -I$(BUILD)/mac-adapter  	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie  	tests/test_mac_link_workspace.c tests/mac_link_peer.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/nwk_parent.c src/mac_scan.c src/mac_poll.c src/mac_association.c src/mac_join.c src/nwk_aps.c src/nwk_aps_transmit.c src/zdo_node.c src/zdo_srv.c src/zdo_runtime.c src/bdb_join.c src/bdb_join_init.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_adapter.c src/mac_link_driver.c src/mac_link_workspace.c -o $(BUILD)/host-mac-link-workspace-tests-sanitize

$(BUILD)/host-mac-observed-tests: tests/test_mac_observed.c src/mac_tx.c src/mac_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED tests/test_mac_observed.c src/mac_tx.c src/mac_frame.c -o $(BUILD)/host-mac-observed-tests

$(BUILD)/host-mac-observed-tests-sanitize: tests/test_mac_observed.c src/mac_tx.c src/mac_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_observed.c src/mac_tx.c src/mac_frame.c -o $(BUILD)/host-mac-observed-tests-sanitize

$(BUILD)/host-mac-poll-tests: tests/test_mac_poll.c src/mac_frame.c src/mac_tx.c src/mac_association.c src/mac_poll.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_poll.c src/mac_frame.c src/mac_tx.c src/mac_association.c src/mac_poll.c -o $(BUILD)/host-mac-poll-tests

$(BUILD)/host-mac-radio-tests: tests/test_mac_radio.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO tests/test_mac_radio.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c tests/host_mmio.c -o $(BUILD)/host-mac-radio-tests

$(BUILD)/host-mac-radio-tests-sanitize: tests/test_mac_radio.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_radio.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c tests/host_mmio.c -o $(BUILD)/host-mac-radio-tests-sanitize

$(BUILD)/host-mac-reconfig-tests: tests/test_mac_reconfig.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_frame.c src/mac_tx.c src/mac_adapter.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -I$(BUILD)/mac-adapter tests/test_mac_reconfig.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_frame.c src/mac_tx.c src/mac_adapter.c tests/host_mmio.c -o $(BUILD)/host-mac-reconfig-tests

$(BUILD)/host-mac-reconfig-tests-sanitize: tests/test_mac_reconfig.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_frame.c src/mac_tx.c src/mac_adapter.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -I$(BUILD)/mac-adapter -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_reconfig.c src/timebase.c src/clock.c src/mac_time.c src/radio_autoack.c src/mac_epoch.c src/mac_radio.c src/mac_attempt.c src/mac_frame.c src/mac_tx.c src/mac_adapter.c tests/host_mmio.c -o $(BUILD)/host-mac-reconfig-tests-sanitize

$(BUILD)/host-mac-scan-tests: tests/test_mac_scan.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/mac_scan.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_scan.c src/mac_frame.c src/mac_tx.c src/nwk_beacon.c src/nwk_candidates.c src/mac_scan.c -o $(BUILD)/host-mac-scan-tests

$(BUILD)/host-mac-stamp-tests: tests/test_mac_stamp.c src/mac_epoch.c src/mac_stamp.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_stamp.c src/mac_epoch.c src/mac_stamp.c -o $(BUILD)/host-mac-stamp-tests

$(BUILD)/host-mac-stamp-tests-sanitize: tests/test_mac_stamp.c src/mac_epoch.c src/mac_stamp.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_stamp.c src/mac_epoch.c src/mac_stamp.c -o $(BUILD)/host-mac-stamp-tests-sanitize

$(BUILD)/host-mac-time-tests: tests/test_mac_time.c src/timebase.c src/mac_time.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_time.c src/timebase.c src/mac_time.c tests/host_mmio.c -o $(BUILD)/host-mac-time-tests

$(BUILD)/host-mac-tx-interval-tests: tests/test_mac_tx_interval.c src/mac_tx.c src/mac_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_INTERVAL tests/test_mac_tx_interval.c src/mac_tx.c src/mac_frame.c -o $(BUILD)/host-mac-tx-interval-tests

$(BUILD)/host-mac-tx-interval-tests-sanitize: tests/test_mac_tx_interval.c src/mac_tx.c src/mac_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_INTERVAL -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_mac_tx_interval.c src/mac_tx.c src/mac_frame.c -o $(BUILD)/host-mac-tx-interval-tests-sanitize

$(BUILD)/host-mac-tx-tests: tests/test_mac_tx.c src/mac_tx.c src/mac_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_mac_tx.c src/mac_tx.c src/mac_frame.c -o $(BUILD)/host-mac-tx-tests

$(BUILD)/host-noise-health-tests: tests/test_noise_health.c src/noise_health.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_noise_health.c src/noise_health.c -o $(BUILD)/host-noise-health-tests

$(BUILD)/host-nv-record-tests: tests/test_nv_record.c tests/host_flash_engine.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_nv_record.c tests/host_flash_engine.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c tests/host_mmio.c -o $(BUILD)/host-nv-record-tests

$(BUILD)/host-nwk-beacon-tests: tests/test_nwk_beacon.c src/nwk_beacon.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_nwk_beacon.c src/nwk_beacon.c -o $(BUILD)/host-nwk-beacon-tests

$(BUILD)/host-nwk-candidates-tests: tests/test_nwk_candidates.c src/mac_frame.c src/nwk_beacon.c src/nwk_candidates.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_nwk_candidates.c src/mac_frame.c src/nwk_beacon.c src/nwk_candidates.c -o $(BUILD)/host-nwk-candidates-tests

$(BUILD)/host-nwk-frame-tests: tests/test_nwk_frame.c src/nwk_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_nwk_frame.c src/nwk_frame.c -o $(BUILD)/host-nwk-frame-tests

$(BUILD)/host-nwk-parent-sanitize: tests/test_nwk_parent.c src/nwk_parent.c src/nwk_candidates.c src/nwk_beacon.c src/mac_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie tests/test_nwk_parent.c src/nwk_parent.c src/nwk_candidates.c src/nwk_beacon.c src/mac_frame.c -o $(BUILD)/host-nwk-parent-sanitize

$(BUILD)/host-nwk-parent-tests: tests/test_nwk_parent.c src/nwk_parent.c src/nwk_candidates.c src/nwk_beacon.c src/mac_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_nwk_parent.c src/nwk_parent.c src/nwk_candidates.c src/nwk_beacon.c src/mac_frame.c -o $(BUILD)/host-nwk-parent-tests

$(BUILD)/host-prng-fixture-tests_$(BOARD): tests/test_prng_fixture.c src/prng_fixture_state.c src/prng.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DPRNG_FIXTURE_HOST_TEST tests/test_prng_fixture.c src/prng_fixture_state.c src/prng.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-prng-fixture-tests_$(BOARD)

$(BUILD)/host-prng-tests: tests/test_prng.c src/prng.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_prng.c src/prng.c tests/host_mmio.c -o $(BUILD)/host-prng-tests

$(BUILD)/host-protocol-budget-tests: tests/test_protocol_budget.c src/mac_frame.c src/nwk_frame.c src/aps_frame.c src/zcl_frame.c src/zcl_value.c src/zcl_attributes.c src/zcl_dispatch.c src/zcl_write.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_protocol_budget.c src/mac_frame.c src/nwk_frame.c src/aps_frame.c src/zcl_frame.c src/zcl_value.c src/zcl_attributes.c src/zcl_dispatch.c src/zcl_write.c -o $(BUILD)/host-protocol-budget-tests

$(BUILD)/host-protocol-frame-tests: tests/test_protocol_frame.c src/mac_frame.c src/nwk_frame.c src/aps_frame.c src/zcl_frame.c src/zcl_value.c src/zcl_attributes.c src/zcl_dispatch.c src/zcl_write.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_protocol_frame.c src/mac_frame.c src/nwk_frame.c src/aps_frame.c src/zcl_frame.c src/zcl_value.c src/zcl_attributes.c src/zcl_dispatch.c src/zcl_write.c -o $(BUILD)/host-protocol-frame-tests

$(BUILD)/host-radio-autoack-link-loss-tests: tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_ADAPTER -DCC2530_MAC_LINK -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c -o $(BUILD)/host-radio-autoack-link-loss-tests

$(BUILD)/host-radio-autoack-loss-tests: tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_ADAPTER -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c -o $(BUILD)/host-radio-autoack-loss-tests

$(BUILD)/host-radio-autoack-rx-diag-link-loss-tests: tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_RX_DIAG -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -DCC2530_MAC_ADAPTER -DCC2530_MAC_LINK tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c -o $(BUILD)/host-radio-autoack-rx-diag-link-loss-tests

$(BUILD)/host-radio-autoack-rx-diag-loss-tests: tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_RX_DIAG -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie -DCC2530_MAC_ADAPTER tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c -o $(BUILD)/host-radio-autoack-rx-diag-loss-tests

$(BUILD)/host-radio-autoack-rx-diag-tests: tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_RX_DIAG -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c -o $(BUILD)/host-radio-autoack-rx-diag-tests

$(BUILD)/host-radio-autoack-tests: tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c -o $(BUILD)/host-radio-autoack-tests

$(BUILD)/host-radio-autoack-tests-sanitize: tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_radio_autoack.c src/timebase.c src/radio_autoack.c tests/host_mmio.c -o $(BUILD)/host-radio-autoack-tests-sanitize

$(BUILD)/host-radio-fifo-fixture-tests_$(BOARD): tests/test_radio_fifo_fixture.c src/radio_fifo_fixture_state.c src/radio_fifo.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DRADIO_FIFO_FIXTURE_HOST_TEST tests/test_radio_fifo_fixture.c src/radio_fifo_fixture_state.c src/radio_fifo.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-radio-fifo-fixture-tests_$(BOARD)

$(BUILD)/host-radio-fifo-tests: tests/test_radio_fifo.c tests/host_mmio.c src/radio_fifo.c src/timebase.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_fifo.c tests/host_mmio.c src/radio_fifo.c src/timebase.c -o $(BUILD)/host-radio-fifo-tests

$(BUILD)/host-radio-link-fixture-sanitize_$(BOARD): tests/test_radio_link_fixture.c src/radio_link_fixture_state.c src/radio_autoack.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_radio_link_fixture.c src/radio_link_fixture_state.c src/radio_autoack.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-radio-link-fixture-sanitize_$(BOARD)

$(BUILD)/host-radio-link-fixture-tests_$(BOARD): tests/test_radio_link_fixture.c src/radio_link_fixture_state.c src/radio_autoack.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_link_fixture.c src/radio_link_fixture_state.c src/radio_autoack.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-radio-link-fixture-tests_$(BOARD)

$(BUILD)/host-radio-noise-fixture-sanitize_$(BOARD): tests/test_radio_noise_fixture.c tests/host_mmio.c src/radio_noise_fixture_state.c src/radio_noise.c src/timebase.c src/clock.c src/noise_health.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie tests/test_radio_noise_fixture.c tests/host_mmio.c src/radio_noise_fixture_state.c src/radio_noise.c src/timebase.c src/clock.c src/noise_health.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-radio-noise-fixture-sanitize_$(BOARD)

$(BUILD)/host-radio-noise-fixture-tests_$(BOARD): tests/test_radio_noise_fixture.c tests/host_mmio.c src/radio_noise_fixture_state.c src/radio_noise.c src/timebase.c src/clock.c src/noise_health.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_noise_fixture.c tests/host_mmio.c src/radio_noise_fixture_state.c src/radio_noise.c src/timebase.c src/clock.c src/noise_health.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-radio-noise-fixture-tests_$(BOARD)

$(BUILD)/host-radio-noise-tests: tests/test_radio_noise.c tests/host_mmio.c src/radio_noise.c src/timebase.c src/noise_health.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_noise.c tests/host_mmio.c src/radio_noise.c src/timebase.c src/noise_health.c -o $(BUILD)/host-radio-noise-tests

$(BUILD)/host-radio-queue-tests: tests/test_radio_queue.c src/radio_queue.c src/radio_rx.c src/timebase.c src/irq.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_queue.c src/radio_queue.c src/radio_rx.c src/timebase.c src/irq.c tests/host_mmio.c -o $(BUILD)/host-radio-queue-tests

$(BUILD)/host-radio-rx-fixture-tests_$(BOARD): tests/test_radio_rx_fixture.c src/radio_rx_fixture_state.c src/radio_rx.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_rx_fixture.c src/radio_rx_fixture_state.c src/radio_rx.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-radio-rx-fixture-tests_$(BOARD)

$(BUILD)/host-radio-rx-tests: tests/test_radio_rx.c src/radio_rx.c src/timebase.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_rx.c src/radio_rx.c src/timebase.c tests/host_mmio.c -o $(BUILD)/host-radio-rx-tests

$(BUILD)/host-radio-tx-fixture-tests_$(BOARD): tests/test_radio_tx_fixture.c src/radio_tx_fixture_state.c src/radio_tx.c src/radio_fifo.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_tx_fixture.c src/radio_tx_fixture_state.c src/radio_tx.c src/radio_fifo.c src/clock.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-radio-tx-fixture-tests_$(BOARD)

$(BUILD)/host-radio-tx-tests: tests/test_radio_tx.c src/timebase.c src/radio_fifo.c src/radio_tx.c tests/host_mmio.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_radio_tx.c src/timebase.c src/radio_fifo.c src/radio_tx.c tests/host_mmio.c -o $(BUILD)/host-radio-tx-tests

$(BUILD)/host-security-counter-tests: tests/test_security_counter.c tests/host_flash_engine.c tests/host_mmio.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_security_counter.c tests/host_flash_engine.c tests/host_mmio.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c -o $(BUILD)/host-security-counter-tests

$(BUILD)/host-security-counter-tests-sanitize: tests/test_security_counter.c tests/host_flash_engine.c tests/host_mmio.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_security_counter.c tests/host_flash_engine.c tests/host_mmio.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c -o $(BUILD)/host-security-counter-tests-sanitize

$(BUILD)/host-security-keys-tests: tests/test_security_keys.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_security_keys.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c -o $(BUILD)/host-security-keys-tests

$(BUILD)/host-security-keys-tests-sanitize: tests/test_security_keys.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_security_keys.c tests/security_joint_model.c tests/security_aes_model.c tests/host_flash_engine.c tests/host_mmio.c tests/aes_reference.c src/flash_exec.c src/flash.c src/flash_write.c src/nv_record.c src/security_counter.c src/timebase.c src/aes.c src/ccm_star.c src/zigbee_mmo.c src/zigbee_key_hash.c src/nwk_frame.c src/aps_frame.c src/ed_wire.c src/security_keys.c -o $(BUILD)/host-security-keys-tests-sanitize

$(BUILD)/host-tests_$(BOARD): tests/test_bootstrap.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_bootstrap.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-tests_$(BOARD)

$(BUILD)/host-timebase-failure-tests_$(BOARD): tests/test_timebase_fixture.c src/timebase_fixture_state.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DTIMEBASE_FIXTURE_FAILURE_TEST tests/test_timebase_fixture.c src/timebase_fixture_state.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-timebase-failure-tests_$(BOARD)

$(BUILD)/host-timebase-fixture-tests_$(BOARD): tests/test_timebase_fixture.c src/timebase_fixture_state.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_timebase_fixture.c src/timebase_fixture_state.c src/timebase.c tests/host_mmio.c src/startup.c src/status.c boards/$(BOARD).c -o $(BUILD)/host-timebase-fixture-tests_$(BOARD)

$(BUILD)/host-timebase-tests: tests/test_timebase.c tests/host_mmio.c src/timebase.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_timebase.c tests/host_mmio.c src/timebase.c -o $(BUILD)/host-timebase-tests

$(BUILD)/host-zcl-attributes-tests: tests/test_zcl_attributes.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_attributes.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $(BUILD)/host-zcl-attributes-tests

$(BUILD)/host-zcl-basic-tests: tests/test_zcl_basic.c src/zcl_basic.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_basic.c src/zcl_basic.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $(BUILD)/host-zcl-basic-tests

$(BUILD)/host-zcl-basic-tests-sanitize: tests/test_zcl_basic.c src/zcl_basic.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zcl_basic.c src/zcl_basic.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $(BUILD)/host-zcl-basic-tests-sanitize

$(BUILD)/host-zcl-dispatch-tests: tests/test_zcl_dispatch.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_dispatch.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $(BUILD)/host-zcl-dispatch-tests

$(BUILD)/host-zcl-frame-tests: tests/test_zcl_frame.c src/zcl_frame.c src/aps_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_frame.c src/zcl_frame.c src/aps_frame.c -o $(BUILD)/host-zcl-frame-tests

$(BUILD)/host-zcl-identify-tests: tests/test_zcl_identify.c src/zcl_identify.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_identify.c src/zcl_identify.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $(BUILD)/host-zcl-identify-tests

$(BUILD)/host-zcl-identify-tests-sanitize: tests/test_zcl_identify.c src/zcl_identify.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zcl_identify.c src/zcl_identify.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $(BUILD)/host-zcl-identify-tests-sanitize

$(BUILD)/host-zcl-sensor: tests/test_zcl_sensor.c src/zcl_sensor.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT $(if $(filter default-tc,$(JOIN_SMOKE_KEY_MODE)),-DCC2530_DEFAULT_TC_KEY) -I. -Isrc tests/test_zcl_sensor.c src/zcl_sensor.c -o $(BUILD)/host-zcl-sensor

$(BUILD)/host-zcl-sensor-sanitize: tests/test_zcl_sensor.c src/zcl_sensor.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -DCC2530_MAC_RADIO -DCC2530_MAC_ATTEMPT -DCC2530_MAC_HANDOFF -DCC2530_MAC_INTERVAL -DCC2530_MAC_OBSERVED -DCC2530_MAC_ADAPTER -DCC2530_MAC_RECONFIG -DCC2530_MAC_LINK -DCC2530_MAC_LINK_RAM -DCC2530_MAC_LINK_WORKSPACE -DCC2530_MAC_LINK_CHILD_WORKSPACE -DCC2530_MAC_LINK_DIRECT $(if $(filter default-tc,$(JOIN_SMOKE_KEY_MODE)),-DCC2530_DEFAULT_TC_KEY) -I. -Isrc -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zcl_sensor.c src/zcl_sensor.c -o $(BUILD)/host-zcl-sensor-sanitize

$(BUILD)/host-zcl-temperature-tests: tests/test_zcl_temperature.c src/zcl_temperature.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_temperature.c src/zcl_temperature.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $(BUILD)/host-zcl-temperature-tests

$(BUILD)/host-zcl-temperature-tests-sanitize: tests/test_zcl_temperature.c src/zcl_temperature.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zcl_temperature.c src/zcl_temperature.c src/zcl_dispatch.c src/zcl_write.c src/zcl_attributes.c src/zcl_frame.c src/zcl_value.c -o $(BUILD)/host-zcl-temperature-tests-sanitize

$(BUILD)/host-zcl-value-tests: tests/test_zcl_value.c src/zcl_value.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zcl_value.c src/zcl_value.c -o $(BUILD)/host-zcl-value-tests

$(BUILD)/host-zdo-node-tests: tests/test_zdo_node.c src/zdo_node.c src/aps_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zdo_node.c src/zdo_node.c src/aps_frame.c -o $(BUILD)/host-zdo-node-tests

$(BUILD)/host-zdo-node-tests-sanitize: tests/test_zdo_node.c src/zdo_node.c src/aps_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zdo_node.c src/zdo_node.c src/aps_frame.c -o $(BUILD)/host-zdo-node-tests-sanitize

$(BUILD)/host-zdo-srv-tests: tests/test_zdo_srv.c src/zdo_srv.c src/zdo_node.c src/aps_frame.c src/nwk_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zdo_srv.c src/zdo_srv.c src/zdo_node.c src/aps_frame.c src/nwk_frame.c -o $(BUILD)/host-zdo-srv-tests

$(BUILD)/host-zdo-srv-tests-sanitize: tests/test_zdo_srv.c src/zdo_srv.c src/zdo_node.c src/aps_frame.c src/nwk_frame.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zdo_srv.c src/zdo_srv.c src/zdo_node.c src/aps_frame.c src/nwk_frame.c -o $(BUILD)/host-zdo-srv-tests-sanitize

$(BUILD)/host-zigbee-key-hash-tests: tests/test_zigbee_key_hash.c src/timebase.c src/aes.c src/zigbee_mmo.c src/zigbee_key_hash.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zigbee_key_hash.c src/timebase.c src/aes.c src/zigbee_mmo.c src/zigbee_key_hash.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c -o $(BUILD)/host-zigbee-key-hash-tests

$(BUILD)/host-zigbee-key-hash-tests-sanitize: tests/test_zigbee_key_hash.c src/timebase.c src/aes.c src/zigbee_mmo.c src/zigbee_key_hash.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zigbee_key_hash.c src/timebase.c src/aes.c src/zigbee_mmo.c src/zigbee_key_hash.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c -o $(BUILD)/host-zigbee-key-hash-tests-sanitize

$(BUILD)/host-zigbee-mmo-tests: tests/test_zigbee_mmo.c src/timebase.c src/aes.c src/zigbee_mmo.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zigbee_mmo.c src/timebase.c src/aes.c src/zigbee_mmo.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c -o $(BUILD)/host-zigbee-mmo-tests

$(BUILD)/host-zigbee-mmo-tests-sanitize: tests/test_zigbee_mmo.c src/timebase.c src/aes.c src/zigbee_mmo.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zigbee_mmo.c src/timebase.c src/aes.c src/zigbee_mmo.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c -o $(BUILD)/host-zigbee-mmo-tests-sanitize

$(BUILD)/host-zigbee-security-tests: tests/test_zigbee_security.c src/timebase.c src/aes.c src/ccm_star.c src/nwk_frame.c src/aps_frame.c src/zigbee_security.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) tests/test_zigbee_security.c src/timebase.c src/aes.c src/ccm_star.c src/nwk_frame.c src/aps_frame.c src/zigbee_security.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c -o $(BUILD)/host-zigbee-security-tests

$(BUILD)/host-zigbee-security-tests-sanitize: tests/test_zigbee_security.c src/timebase.c src/aes.c src/ccm_star.c src/nwk_frame.c src/aps_frame.c src/zigbee_security.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c $(HEADERS) Makefile mk/host-tests.mk | $(BUILD)
	$(HOST_CC) $(HOST_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-pie -no-pie tests/test_zigbee_security.c src/timebase.c src/aes.c src/ccm_star.c src/nwk_frame.c src/aps_frame.c src/zigbee_security.c tests/host_mmio.c tests/aes_reference.c tests/security_aes_model.c -o $(BUILD)/host-zigbee-security-tests-sanitize
