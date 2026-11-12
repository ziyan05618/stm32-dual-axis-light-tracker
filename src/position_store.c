#include "position_store.h"
#include "tracker_config.h"
#include <stddef.h>

typedef struct {
    uint32_t magic,sequence;
    int16_t pan,tilt;
    uint16_t motion,config;
    uint32_t checksum;
    uint16_t commit,reserved;
} record_t;
_Static_assert(sizeof(record_t)==24u,"Position journal record layout");
#define RECORD_MAGIC 0x31534F50u
#define RECORD_COMMIT 0xA55Au
#define CONFIG_TAG ((uint16_t)(PAN_SOFT_LIMIT_STEPS*33u ^ TILT_SOFT_LIMIT_STEPS*17u ^ 0x31A7u))
#define SLOTS (POSITION_PAGE_BYTES/sizeof(record_t))

static uint32_t checksum(const record_t *r)
{
    const uint8_t *bytes=(const uint8_t *)r;
    uint32_t hash=2166136261u;
    for(unsigned i=0;i<offsetof(record_t,checksum);++i)hash=(hash^bytes[i])*16777619u;
    return hash;
}
static void read_record(uint32_t address,record_t *r)
{
    uint8_t *bytes=(uint8_t *)r;
    for(unsigned i=0;i<sizeof(*r);i+=2u){uint16_t v=position_flash_read(address+i);bytes[i]=(uint8_t)v;bytes[i+1u]=(uint8_t)(v>>8);}
}
static int valid(const record_t *r)
{
    return r->magic==RECORD_MAGIC&&r->commit==RECORD_COMMIT&&r->config==CONFIG_TAG
        &&r->checksum==checksum(r)&&r->reserved==0xFFFFu&&(r->motion&~0x1FFu)==0u
        &&r->pan>=-PAN_SOFT_LIMIT_STEPS&&r->pan<=PAN_SOFT_LIMIT_STEPS
        &&r->tilt>=-TILT_SOFT_LIMIT_STEPS&&r->tilt<=TILT_SOFT_LIMIT_STEPS;
}
static uint32_t latest(record_t *result)
{
    uint32_t address=0u;
    for(unsigned page=0;page<2u;++page)for(unsigned i=0;i<SLOTS;++i){
        uint32_t at=POSITION_FLASH_BASE+page*POSITION_PAGE_BYTES+i*sizeof(record_t);
        record_t r;read_record(at,&r);
        if(valid(&r)&&(!address||(int32_t)(r.sequence-result->sequence)>0)){*result=r;address=at;}
    }
    return address;
}
static int erased(uint32_t address)
{
    for(unsigned i=0;i<sizeof(record_t);i+=2u)if(position_flash_read(address+i)!=0xFFFFu)return 0;
    return 1;
}
int position_store_load(saved_position_t *position)
{
    record_t r;
    if(!latest(&r))return POSITION_MISSING;
    position->pan=r.pan;position->tilt=r.tilt;
    position->pan_phase=(uint8_t)((r.motion>>1)&7u);position->tilt_phase=(uint8_t)((r.motion>>4)&7u);
    position->pan_sign=(r.motion&(1u<<7))?-1:1;position->tilt_sign=(r.motion&(1u<<8))?-1:1;
    return (r.motion&1u)?POSITION_CLEAN:POSITION_INTERRUPTED;
}
int position_store_save(const saved_position_t *position,int clean)
{
    if(position->pan < -PAN_SOFT_LIMIT_STEPS||position->pan>PAN_SOFT_LIMIT_STEPS
       ||position->tilt < -TILT_SOFT_LIMIT_STEPS||position->tilt>TILT_SOFT_LIMIT_STEPS)return 0;
    record_t previous,r;uint32_t last=latest(&previous);
    uint32_t page=last?last&~(POSITION_PAGE_BYTES-1u):POSITION_FLASH_BASE;
    uint32_t destination=0u;
    for(unsigned i=0;i<SLOTS;++i){uint32_t at=page+i*sizeof(record_t);if(erased(at)){destination=at;break;}}
    if(!destination){
        page=page==POSITION_FLASH_BASE?POSITION_FLASH_BASE+POSITION_PAGE_BYTES:POSITION_FLASH_BASE;
        if(!position_flash_erase(page))return 0;
        destination=page;
    }
    r.magic=RECORD_MAGIC;r.sequence=last?previous.sequence+1u:1u;
    r.pan=(int16_t)position->pan;r.tilt=(int16_t)position->tilt;
    r.motion=(uint16_t)((clean?1u:0u)|((position->pan_phase&7u)<<1)|((position->tilt_phase&7u)<<4)
        |(position->pan_sign<0?1u<<7:0u)|(position->tilt_sign<0?1u<<8:0u));
    r.config=CONFIG_TAG;r.checksum=checksum(&r);r.commit=RECORD_COMMIT;r.reserved=0xFFFFu;
    const uint8_t *bytes=(const uint8_t *)&r;
    /* The commit word is last: a torn record never replaces the last valid one. */
    for(unsigned i=0;i<=offsetof(record_t,commit);i+=2u){
        uint16_t value=(uint16_t)(bytes[i]|((uint16_t)bytes[i+1u]<<8));
        if(!position_flash_program(destination+i,value))return 0;
    }
    record_t checked;read_record(destination,&checked);
    return valid(&checked)&&checked.sequence==r.sequence;
}
