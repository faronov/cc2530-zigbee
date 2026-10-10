#!/bin/bash
# usage: TOOLS=bin OUT=dir MDIR=objdir PROJECT_LD=script link.sh TAG [extra driver flags...]
# Production SDCC object order; only $TOOLS/clang and $TOOLS/ld.lld are used.
set -u
TAG=$1; shift
L=$OUT/link; M=$MDIR
FIXED=("$L/fixed_xdata_mcs51.o")
[ "${FIRMWARE_VARIANT:-production}" = rx-diag ] && FIXED+=("$L/rx_diag_mcs51.o")
ORDER="mac_link_workspace mac_link_child_workspace flash_exec timebase clock flash flash_write nv_record security_counter aes ccm_star zigbee_mmo zigbee_key_hash mac_time radio_autoack mac_epoch mac_radio mac_attempt mac_frame mac_tx mac_adapter nwk_frame aps_frame ed_wire security_keys mac_association mac_poll mac_join zdo_node zdo_srv nwk_beacon nwk_candidates nwk_parent mac_scan nwk_aps nwk_aps_direct zdo_runtime zcl_sensor bdb_join bdb_join_init mac_link_driver startup lg_esl29_rev03 join_smoke join_smoke_main"
OBJS=""; for m in $ORDER; do [ -f $M/$m.o ] || { echo "missing $m"; exit 2; }; OBJS="$OBJS $M/$m.o"; done
n=$(ls $M/*.o | wc -l); [ $n = 45 ] || { echo "object count $n != 45"; exit 2; }
CMD=("$TOOLS/clang" -target mcs51 -mcpu=cc2530 -Oz -nostartfiles "$OUT/rt/mcs51/cc2530_startup.o"
     -resource-dir "$OUT/rt" --ld-path="$TOOLS/ld.lld" -Wl,-T,"$PROJECT_LD"
     -Wl,-Map,"$L/$TAG.map" "$@"
     $OBJS "$L/flash_exec_mcs51.o" "${FIXED[@]}" -o "$L/$TAG.elf")
printf '%q ' "${CMD[@]}" > "$L/$TAG.cmd"; echo >> "$L/$TAG.cmd"
"${CMD[@]}" -### > "$L/$TAG.driver" 2>&1
"${CMD[@]}" > "$L/$TAG.log" 2>&1
rc=$?; echo "rc=$rc" >> "$L/$TAG.log"; echo "$TAG rc=$rc"
