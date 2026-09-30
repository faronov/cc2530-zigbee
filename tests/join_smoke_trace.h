/* SPDX-License-Identifier: BSD-3-Clause
 * Native peripheral transcript; only external admission enters MCU memory.
 */
#include "join_smoke_layout.h"
extern volatile uint8_t aes_dma0[8],aes_dma1[32],aes_key[16],aes_iv[16],aes_input[16],aes_output[16];
extern uint8_t flash_exec_work[9];
static unsigned join_trace_steps,join_trace_events,join_trace_active,join_trace_flash;
static uint8_t join_trace_sfr[256];

static void join_hex(const volatile uint8_t *p,unsigned n)
{
    unsigned i;
    for(i=0;i<n;i++) printf("%02x",p[i]);
}
static void join_registers(uint8_t *out)
{
#define REG(name,address) out[address]=name;
    CC2530_REGISTER_LIST(REG)
#undef REG
}
static void join_event(char kind,unsigned address,unsigned value,uint8_t dma)
{
    uint8_t after[256]={0};
    unsigned i,comma=0;
    if(!join_trace_active) return;
    join_registers(after);
    printf("%s[\"%c\",%u,%u,[",join_trace_events++?",":"",kind,address,value);
    for(i=0;i<256;i++) if(after[i]!=join_trace_sfr[i]) {
        printf("%s[%u,%u]",comma++?",":"",i,after[i]);
    }
    printf("],");
    if(dma) {
        printf("[%u,\"",dma);
        join_hex(dma==1?aes_key:dma==2?aes_iv:aes_input,16);
        printf("\",\""); join_hex(aes_output,16); printf("\"]");
    } else printf("null");
    printf(",");
    if(kind=='w' && address==0x6270) join_trace_flash=1;
    if(kind=='r' && address==0x6270 && join_trace_flash && !(value&0x83)) {
        printf("[%u,\"",flash_exec_work[0]&3);
        join_hex(flash_exec_work+1,4); printf("\"]"); join_trace_flash=0;
    } else printf("null");
    printf("]");
}
static uint8_t join_read(uint8_t a,uint8_t value)
{
    uint8_t arm=SOC_DMAARM, control=SOC_ENCCS, dma=0, result;
    join_registers(join_trace_sfr);
    result=joint_load(a,value);
    if(a==SOC_IEN0_ADDRESS && (arm&1) && !(SOC_DMAARM&1))
        dma=(control&7)==4?1:(control&7)==6?2:3;
    join_event('r',a,result,dma); return result;
}
static void join_write(uint8_t a,uint8_t before,uint8_t value)
{
    join_registers(join_trace_sfr);
    joint_store(a,before,value);
    join_event('w',a,value,0);
}
static uint8_t join_xread(uint16_t a)
{
    uint8_t value;
    join_registers(join_trace_sfr);
    value=joint_xload(a); join_event('r',a,value,0); return value;
}
static void join_xwrite(uint16_t a,uint8_t value)
{
    join_registers(join_trace_sfr);
    joint_xstore(a,value); join_event('w',a,value,0);
}
static void join_cycles(uint8_t n)
{
    join_registers(join_trace_sfr);
    joint_cycles(n); join_event('c',0,n,0);
}
static void join_trace_start(unsigned selected)
{
    uint8_t admission[JOIN_ADMISSION_SIZE];
    unsigned i;
    join_pack_admission(admission,&join_smoke_phase.admission);
    printf("{\"case\":%u,\"admission\":\"",selected); join_hex(admission,sizeof(admission));
    printf("\",\"draws\":\""); join_hex(join_smoke_draws,JOIN_SMOKE_DRAWS);
    printf("\",\"nv_initial\":\""); join_hex(security_joint_nv(),4096);
    printf("\",\"initial\":{");
#define REG(name,address) printf("\"%u\":%u,",address,name);
    CC2530_REGISTER_LIST(REG)
#undef REG
    for(i=0;i<sizeof(xregs);i++)
        printf("\"%u\":%u%s",0x6100u+i,xregs[i],i+1==sizeof(xregs)?"":",");
    printf("},\"steps\":[");
    host_mmio_read_hook=join_read; host_mmio_write_hook=join_write;
    host_mmio_xread_hook=join_xread; host_mmio_xwrite_hook=join_xwrite;
    host_mmio_cycles_hook=join_cycles;
}
static void join_trace_before(void)
{
    printf("%s{\"packet\":\"",join_trace_steps++?",":"");
    join_hex(join_smoke_mailbox,8); printf("\",\"events\":[");
    join_trace_events=0; join_trace_active=1;
}
static void join_trace_after(uint8_t returned)
{
    join_trace_active=0;
    printf("],\"returned\":%s,\"status\":\"",returned?"true":"false");
    join_hex((const volatile uint8_t *)&join_smoke_status,48);
    printf("\",\"nv\":\""); join_hex(security_joint_nv(),4096); printf("\"}");
}
static void join_trace_finish(void)
{
    printf("],\"peer_tx\":%u,\"aes_blocks\":%u,\"flash_commands\":%u}\n",
        link_peer_tx,security_joint_aes_blocks(),security_joint_flash_commands());
}
