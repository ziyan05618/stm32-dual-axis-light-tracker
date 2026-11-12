#ifndef POSITION_STORE_H
#define POSITION_STORE_H
#include <stdint.h>

/* Last two 1 KiB pages are excluded from the application by the linker. */
#define POSITION_FLASH_BASE 0x0800F800u
#define POSITION_FLASH_END 0x08010000u
#define POSITION_PAGE_BYTES 1024u
typedef struct {
    int32_t pan,tilt;
    uint8_t pan_phase,tilt_phase;
    int8_t pan_sign,tilt_sign;
} saved_position_t;
enum { POSITION_MISSING=0, POSITION_CLEAN=1, POSITION_INTERRUPTED=2 };
int position_store_load(saved_position_t *position);
int position_store_save(const saved_position_t *position,int clean);

/* Board backend. Only the reserved pages may be changed. */
uint16_t position_flash_read(uint32_t address);
int position_flash_program(uint32_t address,uint16_t value);
int position_flash_erase(uint32_t address);
#endif
