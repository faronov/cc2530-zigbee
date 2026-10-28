/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2026, cc2530-zigbee contributors. See LICENSE.
 * Test-only format experiment. All media I/O uses the existing flash services.
 * The large host parser buffer is NOT a proposed target RAM allocation.
 */
#if defined(__SDCC) || !defined(CC2530_HOST_TEST)
#error Replay format reference is host-only and must not enter firmware.
#endif
#include "nv_record.h"
#include <string.h>

enum { REPLAY_REF_SIZE=160, REPLAY_REF_START=192, REPLAY_REF_SLOT=48,
       REPLAY_REF_SLOTS=38 };
typedef struct {
    uint32_t base, last, checksum;
    uint8_t version, length, used, state, data[REPLAY_REF_SIZE];
} replay_ref_page_t;
static replay_ref_page_t replay_ref_pages[2];
static uint8_t replay_ref_media[2048], replay_ref_word[4], replay_ref_delta[48];
static uint8_t replay_ref_selected, replay_ref_fresh, replay_ref_attempts[2];
static nv_record_result_t replay_ref_fault;
static const uint8_t replay_ref_offsets[5]={96,100,104,138,142};

static uint32_t replay_ref_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void replay_ref_put(uint8_t *p, uint32_t value)
{
    unsigned i;
    for(i=0;i<4;i++) { p[i]=(uint8_t)value; value>>=8; }
}
static uint32_t replay_ref_crc(const uint8_t *p, unsigned length)
{
    uint32_t value=0xffffffffUL;
    unsigned i;
    while(length--) {
        value^=*p++;
        for(i=0;i<8;i++) value=(value>>1)^((value&1) ? 0xedb88320UL : 0);
    }
    return value^0xffffffffUL;
}
static uint8_t replay_ref_ff(const uint8_t *p, unsigned length)
{
    while(length--) if(*p++!=255) return 0;
    return 1;
}
static uint8_t replay_ref_domain(const uint8_t *data, unsigned field)
{
    if(field==2) return 1;
    if(!(data[124]&(1u<<((field==0 || field==3) ? 0 : 1)))) return 0;
    return field<3 || (data[146]&1);
}
static uint8_t replay_ref_k3(const replay_ref_page_t *p)
{
    return p->length==REPLAY_REF_SIZE && !memcmp(p->data,"CTR1",4) &&
        p->data[4]==1 && p->data[5]==144 && !p->data[6] && !p->data[7] &&
        p->data[126]=='K' && p->data[127]==3;
}
static nv_record_result_t replay_ref_fail(nv_record_result_t result)
{
    replay_ref_fault=result;
    return result;
}
static void replay_ref_reset(void)
{
    memset(replay_ref_pages,0,sizeof(replay_ref_pages));
    memset(replay_ref_attempts,0,sizeof(replay_ref_attempts));
    replay_ref_selected=replay_ref_fresh=255;
    replay_ref_fault=NV_RECORD_OK;
}
static nv_record_result_t replay_ref_scan(uint8_t page)
{
    replay_ref_page_t *p=&replay_ref_pages[page];
    uint8_t *raw=replay_ref_media, *d;
    unsigned pos, slot, field;
    uint32_t value, prior;
    p->state=NV_PAGE_UNCHECKED; p->used=0;
    for(pos=0;pos<2048;pos+=FLASH_READ_MAX)
        if(flash_nv_read(page,(uint16_t)pos,raw+pos,FLASH_READ_MAX)!=FLASH_OK)
            return replay_ref_fail(NV_RECORD_READ_FAILED);
    if(replay_ref_ff(raw,2048)) { p->state=NV_PAGE_EMPTY; return NV_RECORD_OK; }
    p->state=NV_PAGE_FORMAT;
    if(memcmp(raw,"NVR1",4)) return NV_RECORD_OK;
    p->version=raw[4];
    if(p->version!=1 && p->version!=2) {
        p->state=NV_PAGE_UNSUPPORTED; return NV_RECORD_OK;
    }
    p->length=raw[6];
    if(raw[5] || !p->length || p->length>REPLAY_REF_SIZE || raw[7]) return NV_RECORD_OK;
    p->base=p->last=replay_ref_u32(raw+8);
    if(!p->base) return NV_RECORD_OK;
    memset(p->data,255,sizeof(p->data));
    memcpy(p->data,raw+12,p->length);
    if(p->version==2 && !replay_ref_k3(p)) return NV_RECORD_OK;
    pos=p->version==1 ? 2040u : 176u;
    if(memcmp(raw+pos+4,p->version==1 ? "CMT1" : "CMT2",4)) {
        p->state=NV_PAGE_INCOMPLETE; return NV_RECORD_OK;
    }
    p->checksum=replay_ref_u32(raw+pos);
    if(!replay_ref_ff(raw+12+p->length,pos-12-p->length)) return NV_RECORD_OK;
    if(p->checksum!=replay_ref_crc(raw,pos)) {
        p->state=NV_PAGE_INTEGRITY; return NV_RECORD_OK;
    }
    if(p->version==1) { p->state=NV_PAGE_VALID; return NV_RECORD_OK; }
    if(!replay_ref_ff(raw+184,8) || !replay_ref_ff(raw+2016,32)) return NV_RECORD_OK;
    for(slot=0;slot<REPLAY_REF_SLOTS;slot++) {
        d=raw+REPLAY_REF_START+slot*REPLAY_REF_SLOT;
        if(replay_ref_ff(d,REPLAY_REF_SLOT)) {
            if(!replay_ref_ff(d,2016u-(REPLAY_REF_START+slot*REPLAY_REF_SLOT)))
                return NV_RECORD_OK;
            break;
        }
        if(memcmp(d+44,"DMT2",4)) { p->state=NV_PAGE_INCOMPLETE; return NV_RECORD_OK; }
        if(replay_ref_u32(d+40)!=replay_ref_crc(d,40)) {
            p->state=NV_PAGE_INTEGRITY; return NV_RECORD_OK;
        }
        if(p->last==0xffffffffUL || replay_ref_u32(d)!=p->last+1 ||
           replay_ref_u32(d+4)!=p->base || !d[8] || (d[8]&0xe0) ||
           d[9]!=1 || d[10] || d[11] || replay_ref_u32(d+32)!=p->checksum ||
           replay_ref_u32(d+36)!=p->last) return NV_RECORD_OK;
        for(field=0;field<5;field++) {
            if((d[8]&(1u<<field)) && !replay_ref_domain(p->data,field)) return NV_RECORD_OK;
            value=replay_ref_u32(d+12+4*field);
            prior=replay_ref_u32(p->data+replay_ref_offsets[field]);
            if((d[8]&(1u<<field)) ? value<=prior : value!=prior) return NV_RECORD_OK;
            replay_ref_put(p->data+replay_ref_offsets[field],value);
        }
        p->last++; p->used++;
    }
    p->state=NV_PAGE_VALID;
    return NV_RECORD_OK;
}
static nv_record_result_t replay_ref_open(uint8_t *out)
{
    uint8_t i, selected, valid=0;
    nv_record_result_t result;
    if(replay_ref_fault!=NV_RECORD_OK) return replay_ref_fault;
    for(i=0;i<2;i++) {
        result=replay_ref_scan(i);
        if(result!=NV_RECORD_OK) return result;
        if(replay_ref_pages[i].state==NV_PAGE_UNSUPPORTED)
            return replay_ref_fail(NV_RECORD_UNSUPPORTED);
        if(replay_ref_pages[i].state==NV_PAGE_VALID) valid|=(uint8_t)(1u<<i);
    }
    if(!valid) return replay_ref_fail(NV_RECORD_CORRUPT);
    if(valid==3) {
        selected=replay_ref_pages[1].base>replay_ref_pages[0].base ? 1 : 0;
        if(replay_ref_pages[selected^1].last==0xffffffffUL ||
           replay_ref_pages[selected].base!=replay_ref_pages[selected^1].last+1)
            return replay_ref_fail(NV_RECORD_CONFLICT);
    } else {
        selected=valid==1 ? 0 : 1;
        if(replay_ref_pages[selected^1].state!=NV_PAGE_EMPTY)
            return replay_ref_fail(NV_RECORD_RECOVERY_REQUIRED);
    }
    replay_ref_selected=selected;
    if(!replay_ref_k3(&replay_ref_pages[selected]))
        return replay_ref_fail(NV_RECORD_UNSUPPORTED);
    if(out) memcpy(out,replay_ref_pages[selected].data,REPLAY_REF_SIZE);
    return NV_RECORD_OK;
}
static nv_record_result_t replay_ref_program(uint8_t page, unsigned offset,
                                           const uint8_t *data)
{
    memcpy(replay_ref_word,data,4);
    if(flash_nv_program(page,(uint16_t)offset,replay_ref_word,3)!=FLASH_WRITE_OK)
        return replay_ref_fail(NV_RECORD_WRITE_FAILED);
    return NV_RECORD_OK;
}
static nv_record_result_t replay_ref_verify(uint32_t generation, const uint8_t *data)
{
    nv_record_result_t result=replay_ref_open(NULL);
    if(result!=NV_RECORD_OK) return result;
    if(replay_ref_pages[replay_ref_selected].last!=generation ||
       memcmp(replay_ref_pages[replay_ref_selected].data,data,REPLAY_REF_SIZE))
        return replay_ref_fail(NV_RECORD_CORRUPT);
    return NV_RECORD_OK;
}
static nv_record_result_t replay_ref_checkpoint(const uint8_t *data)
{
    uint8_t page;
    unsigned pos;
    uint32_t generation;
    nv_record_result_t result=replay_ref_open(NULL);
    if(result!=NV_RECORD_OK) return result;
    generation=replay_ref_pages[replay_ref_selected].last;
    if(generation==0xffffffffUL) return NV_RECORD_GENERATION_EXHAUSTED;
    if(memcmp(data,"CTR1",4) || data[4]!=1 || data[5]!=144 || data[6] || data[7] ||
       data[126]!='K' || data[127]!=3) return NV_RECORD_INVALID_ARGUMENT;
    for(pos=8;pos<=12;pos+=4)
        if(replay_ref_u32(data+pos)<
           replay_ref_u32(replay_ref_pages[replay_ref_selected].data+pos))
            return NV_RECORD_INVALID_ARGUMENT;
    page=replay_ref_selected^1;
    if(replay_ref_attempts[page]>=NV_RECORD_ERASE_LIMIT) return NV_RECORD_ERASE_LIMIT_REACHED;
    memset(replay_ref_media,255,192);
    memcpy(replay_ref_media,"NVR1",4);
    replay_ref_media[4]=2; replay_ref_media[5]=replay_ref_media[7]=0;
    replay_ref_media[6]=REPLAY_REF_SIZE;
    replay_ref_put(replay_ref_media+8,++generation);
    memcpy(replay_ref_media+12,data,REPLAY_REF_SIZE);
    replay_ref_put(replay_ref_media+176,replay_ref_crc(replay_ref_media,176));
    memcpy(replay_ref_media+180,"CMT2",4);
    replay_ref_attempts[page]++;
    if(flash_nv_erase(page,3)!=FLASH_WRITE_OK) return replay_ref_fail(NV_RECORD_WRITE_FAILED);
    for(pos=0;pos<172;pos+=4) {
        result=replay_ref_program(page,pos,replay_ref_media+pos);
        if(result!=NV_RECORD_OK) return result;
    }
    result=replay_ref_program(page,176,replay_ref_media+176);
    if(result!=NV_RECORD_OK) return result;
    result=replay_ref_program(page,180,replay_ref_media+180);
    if(result!=NV_RECORD_OK) return result;
    result=replay_ref_verify(generation,data);
    if(result==NV_RECORD_OK) replay_ref_fresh=page;
    return result;
}
static nv_record_result_t replay_ref_admit(const uint8_t *data)
{
    replay_ref_page_t *p;
    nv_record_result_t result=replay_ref_open(NULL);
    unsigned field, byte, offset;
    uint8_t mask=0, is_floor;
    uint32_t value, prior;
    if(result!=NV_RECORD_OK) return result;
    p=&replay_ref_pages[replay_ref_selected];
    if(p->last==0xffffffffUL) return NV_RECORD_GENERATION_EXHAUSTED;
    for(byte=0;byte<REPLAY_REF_SIZE;byte++) {
        is_floor=0;
        for(field=0;field<5;field++)
            if(byte>=replay_ref_offsets[field] && byte<replay_ref_offsets[field]+4u)
                is_floor=1;
        if(!is_floor && data[byte]!=p->data[byte]) return NV_RECORD_INVALID_ARGUMENT;
    }
    for(field=0;field<5;field++) {
        value=replay_ref_u32(data+replay_ref_offsets[field]);
        prior=replay_ref_u32(p->data+replay_ref_offsets[field]);
        if(value<prior) return NV_RECORD_INVALID_ARGUMENT;
        if(value>prior) {
            if(!replay_ref_domain(p->data,field)) return NV_RECORD_INVALID_ARGUMENT;
            mask|=(uint8_t)(1u<<field);
        }
    }
    if(!mask) return NV_RECORD_INVALID_ARGUMENT;
    if(p->version!=2 || replay_ref_fresh!=replay_ref_selected || p->used==REPLAY_REF_SLOTS)
        return replay_ref_checkpoint(data);
    memset(replay_ref_delta,0,sizeof(replay_ref_delta));
    replay_ref_put(replay_ref_delta,p->last+1);
    replay_ref_put(replay_ref_delta+4,p->base);
    replay_ref_delta[8]=mask; replay_ref_delta[9]=1;
    for(field=0;field<5;field++)
        memcpy(replay_ref_delta+12+4*field,data+replay_ref_offsets[field],4);
    replay_ref_put(replay_ref_delta+32,p->checksum);
    replay_ref_put(replay_ref_delta+36,p->last);
    replay_ref_put(replay_ref_delta+40,replay_ref_crc(replay_ref_delta,40));
    memcpy(replay_ref_delta+44,"DMT2",4);
    offset=REPLAY_REF_START+p->used*REPLAY_REF_SLOT;
    for(byte=0;byte<REPLAY_REF_SLOT;byte+=4) {
        result=replay_ref_program(replay_ref_selected,offset+byte,replay_ref_delta+byte);
        if(result!=NV_RECORD_OK) return result;
    }
    return replay_ref_verify(replay_ref_u32(replay_ref_delta),data);
}
