#include "control.h"
#include "tracker_config.h"
static int32_t clamp(int32_t v,int32_t limit) { return v>limit?limit:(v < -limit?-limit:v); }
void axis_init(volatile axis_t *a,int32_t limit,int8_t phase_sign)
{
    a->position=0; a->target=0; a->last_step=0; a->limit=limit;
    a->jog=0; a->last_dir=0; a->phase_sign=phase_sign;
    a->phase=0; a->active=0; a->ramp=STEP_START_MS;
}
void axis_stop(volatile axis_t *a) { a->jog=0; a->target=a->position; a->last_dir=0; a->ramp=STEP_START_MS; }
void axis_jog(volatile axis_t *a,int direction)
{
    int8_t d=(int8_t)((direction>0)-(direction<0));
    if(a->jog!=d) { a->target=a->position; a->jog=d; }
}
void axis_move(volatile axis_t *a,int32_t steps)
{
    a->jog=0;
    if(steps>a->limit) steps=a->limit;
    if(steps < -a->limit) steps=-a->limit;
    a->target=clamp(a->target+steps,a->limit);
}
uint8_t axis_tick(volatile axis_t *a,uint32_t now,int enabled)
{
    static const uint8_t sequence[8]={1u,3u,2u,6u,4u,12u,8u,9u};
    if(!enabled) { axis_stop(a); a->active=0; a->last_step=now; return 0; }
    int dir=a->jog;
    if(!dir) dir=(a->target>a->position)-(a->target<a->position);
    if((dir>0 && a->position>=a->limit)||(dir<0 && a->position<=-a->limit)) dir=0;
    if(!dir) { a->last_dir=0; a->ramp=STEP_START_MS; return a->active?sequence[a->phase]:0u; }
    if(dir!=a->last_dir) { a->ramp=STEP_START_MS; a->last_dir=(int8_t)dir; }
    if((uint32_t)(now-a->last_step)>=a->ramp) {
        a->last_step=now; a->position+=dir;
        if(a->jog) a->target=a->position;
        int electrical=dir*a->phase_sign;
        a->phase=(uint8_t)((a->phase+(electrical>0?1u:7u))&7u); a->active=1u;
        if(a->ramp>STEP_FAST_MS) --a->ramp;
    }
    return a->active?sequence[a->phase]:0u;
}
int track_direction(uint16_t positive,uint16_t negative,int previous)
{
    if(positive<TRACK_MIN_LIGHT && negative<TRACK_MIN_LIGHT) return 0;
    int delta=(int)positive-(int)negative;
    if(delta>=TRACK_START_ERROR) return 1;
    if(delta<=-TRACK_START_ERROR) return -1;
    if(previous>0 && delta>TRACK_STOP_ERROR) return 1;
    if(previous<0 && delta< -TRACK_STOP_ERROR) return -1;
    return 0;
}
uint16_t light_from_adc(uint16_t raw)
{
    if(raw>4095u) raw=4095u;
    return (uint16_t)(((4095u-raw)*1000u+2047u)/4095u);
}
uint16_t battery_from_adc(uint16_t raw)
{
    if(raw>4095u) raw=4095u;
    return (uint16_t)(((uint32_t)raw*ADC_REFERENCE_MV*2u+2047u)/4095u);
}
uint8_t voltage_percent(uint16_t mv)
{
    if(mv<=BATTERY_EMPTY_MV) return 0u;
    if(mv>=BATTERY_FULL_MV) return 100u;
    return (uint8_t)(((uint32_t)(mv-BATTERY_EMPTY_MV)*100u)/(BATTERY_FULL_MV-BATTERY_EMPTY_MV));
}

