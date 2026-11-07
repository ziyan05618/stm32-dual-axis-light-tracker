#ifndef DISPLAY_H
#define DISPLAY_H
#include <stdint.h>
#include "portrait.h"
void display_init(void);
void display_clear(uint16_t color);
void display_restore(unsigned x,unsigned y,unsigned w,unsigned h);
void display_character_rows(const portrait_frame_t *frame,unsigned first,unsigned count);
void display_rect(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color);
void display_text(unsigned x,unsigned y,const char *text,uint16_t fg,uint16_t bg);
void display_meter(unsigned x,unsigned y,unsigned width,unsigned percent,uint32_t phase);
void display_frame_fx(uint32_t phase,uint8_t alert);
#endif
