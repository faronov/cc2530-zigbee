/* SPDX-License-Identifier: BSD-3-Clause
 * Offline-only synthetic controller. No dummy service, private-state progress
 * or hardware access. Each scenario begins in a fresh host process/CRT epoch.
 */
#define MAC_ADAPTER_MAIN adapter_component_main
#include "test_mac_adapter.c"
#include "mac_smoke.h"

static unsigned starting, exporting, serial, selected;
static uint16_t smoke_address(const volatile void *p)
{
    /* Synthetic host mapping only. Actual target placement is separately checked. */
    if(p==&mac_smoke_config) return 0x1600;
    if(p==&mac_smoke_tx) return 0x1680;
    if(p==&mac_smoke_action) return 0x1800;
    if(p==&mac_smoke_clock) return 0x1840;
    return adapter_address(p);
}
static void smoke_store(uint8_t a,uint8_t before,uint8_t value)
{
    if(starting) { assert(write_count==1); write_count=0; logs(); return; }
    adapter_store(a,before,value);
}
static uint8_t smoke_load(uint8_t a,uint8_t value)
{
    if(selected==3 && mac_smoke_status.stage==4 && a==SOC_ST0_ADDRESS) ticks+=65536;
    return adapter_load(a,value);
}
static void packet(unsigned run)
{
    const uint8_t arm[8]={0xa9,0x56,26,0xe5,0x36,0xc9,0x4d,0xb2};
    const uint8_t go[8]={0x56,0xa9,26,0xe5,0xc9,0x36,0x4d,0xb2};
    unsigned j;
    for(j=0;j<8;j++) mac_smoke_mailbox[j]=run?go[j]:arm[j];
}
static void cycle(void)
{
    unsigned before=accesses;
    if(exporting) {
        printf("%s{\"packet\":\"",serial++?",":""); hex((const void *)mac_smoke_mailbox,8);
        printf("\",\"events\":["); trace_count=0;
    }
    printing=exporting;
    mac_smoke_poll();
    printing=0; logs();
    assert(tx_attempts<=5 && mac_smoke_status.transmissions<=1);
    if(mac_smoke_status.phase<=MS_ADMITTED) assert(before==accesses);
    if(exporting) { printf("],\"status\":\""); hex((const void *)&mac_smoke_status,64); printf("\"}"); }
}
int main(int argc,char **argv)
{
    unsigned j,before;
    mac_smoke_status_t saved;
    assert(argc==2 || argc==3);
    selected=(unsigned)strtoul(argv[1],NULL,10); assert(selected<10);
    exporting=argc==3; adapter_reset(); replies=0; reply_after_tx=0;
    host_mmio_xaddress_hook=smoke_address; host_mmio_write_hook=smoke_store; host_mmio_read_hook=smoke_load;
    body_length=MAC_SMOKE_LENGTH; SOC_P0=SOC_P1=SOC_P2=255;
    starting=1; assert(_sdcc_external_startup()==0); starting=0;
    mac_smoke_initialize(); assert(mac_smoke_status.phase==MS_DISARMED);
    config.value=mac_smoke_config;
    if(selected==1) cca_clear=0;
    if(selected==2) clock_failure=1;
    if(selected==9) hold_tx=1;
    if(exporting) {
        printf("{\"case\":%u,\"initial\":{",selected);
#define REG(name,address) printf("\"%u\":%u,",address,name);
        CC2530_REGISTER_LIST(REG)
#undef REG
        for(j=0;j<sizeof(xregs);j++) printf("\"%u\":%u%s",0x6100u+j,xregs[j],j+1==sizeof(xregs)?"":",");
        printf("},\"steps\":[");
    }
    cycle();
    if(selected==5) { for(j=1;j<256;j++) cycle(); }
    else if(selected==6) { packet(1); cycle(); }
    else {
        packet(0); if(selected==4) mac_smoke_mailbox[7]^=1;
        if(selected==8) mac_smoke_status.guards[1]^=1;
        cycle();
        if(mac_smoke_status.phase==MS_ARMED) {
            packet(1); cycle(); assert(mac_smoke_status.phase==MS_ADMITTED);
            if(selected==7) mac_smoke_mailbox[0]=1;
            cycle();
        }
    }
    if(exporting) puts("]}");
    else fprintf(stderr,"case%u phase%u reason%u outcome%u stage%u adapter%u mac%u radio%u txphase%u draws%u attempts%u busy%u sent%u retired%u transmissions%u steps%u\n",
        selected,mac_smoke_status.phase,mac_smoke_status.reason,mac_smoke_status.outcome,
        mac_smoke_status.stage,mac_smoke_status.adapter_result,mac_smoke_status.mac_result,
        mac_smoke_status.radio_result,mac_smoke_status.mac_phase,mac_smoke_status.draws,
        mac_smoke_status.attempts,mac_smoke_status.busy,mac_smoke_status.sent,
        mac_smoke_status.retired,mac_smoke_status.transmissions,
        mac_smoke_status.steps[0]+256u*mac_smoke_status.steps[1]);
    if(selected<2) {
        assert(mac_smoke_status.phase==MS_END && mac_smoke_status.completed && mac_smoke_status.released);
        assert(mac_smoke_status.outcome==(selected?MS_CCA_BUSY:MS_SENT));
        assert(!XR(0x618b) && !count && !mac_adapter_diagnostic()->held && !mac_adapter_diagnostic()->ready);
        assert(mac_smoke_tx.engine.phase==MAC_TX_IDLE);
        assert(mac_smoke_status.draws==(selected?5:1));
        assert(mac_smoke_status.transmissions==(selected?0:1));
        assert(tx_count==14);
        assert(!memcmp(tx_fifo+1,"\x41\x88\x5a\x34\x12\xff\xff\x78\x56""MAC1",13));
    } else assert(mac_smoke_status.phase==MS_FAULT && !mac_smoke_status.completed);
    before=accesses; memcpy(&saved,(const void *)&mac_smoke_status,sizeof(saved));
    packet(0);
    for(j=0;j<3;j++) { mac_smoke_initialize(); mac_smoke_poll(); }
    assert(accesses==before && !memcmp(&saved,(const void *)&mac_smoke_status,sizeof(saved)));
    return 0;
}
