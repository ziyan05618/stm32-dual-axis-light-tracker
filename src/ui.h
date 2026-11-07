#ifndef UI_H
#define UI_H
#include <stdint.h>
enum { UI_LIMIT_LEFT=1u, UI_LIMIT_RIGHT=2u, UI_LIMIT_UP=4u, UI_LIMIT_DOWN=8u };
typedef struct {
    uint16_t light[4]; /* left, right, up, down; relative light 0..1000 */
    uint16_t battery_mv;
    int32_t pan, tilt;
    uint8_t automatic, enabled, adc_fault;
    int8_t pan_request, tilt_request;
    uint8_t limit_attempts; /* one-shot presses rejected at a software boundary */
} ui_state_t;
void ui_render(const ui_state_t *state, uint32_t now);
void ui_animate(void);
#endif
