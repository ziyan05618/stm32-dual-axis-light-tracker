#ifndef CONTROL_H
#define CONTROL_H
#include <stdint.h>
typedef struct {
    int32_t position, target;
    uint32_t last_step;
    int32_t limit;
    int8_t jog, last_dir, phase_sign;
    uint8_t phase, active, ramp, min_interval;
} axis_t;
void axis_init(volatile axis_t *a,int32_t limit,int8_t phase_sign);
void axis_stop(volatile axis_t *a);
void axis_jog(volatile axis_t *a,int direction);
void axis_move(volatile axis_t *a,int32_t steps);
void axis_home(volatile axis_t *a);
uint8_t axis_tick(volatile axis_t *a,uint32_t now,int enabled);
int track_direction(uint16_t positive,uint16_t negative,int previous);
uint16_t light_from_adc(uint16_t raw);
uint16_t battery_from_adc(uint16_t raw);
uint8_t voltage_percent(uint16_t millivolts);
#endif

