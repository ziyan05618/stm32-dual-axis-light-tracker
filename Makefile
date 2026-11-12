CC := tools/arm-gnu/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-gcc.exe
OBJCOPY := tools/arm-gnu/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-objcopy.exe
SIZE := tools/arm-gnu/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi/bin/arm-none-eabi-size.exe
CFLAGS := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -Os -ffunction-sections -fdata-sections -Wall -Wextra -Werror -std=c11 -ffreestanding -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables
LDFLAGS := -mcpu=cortex-m3 -mthumb -mfloat-abi=soft -nostdlib -Wl,--gc-sections -Wl,-Map=build/tracker.map -T src/stm32f103c8.ld

all: build/tracker.elf build/tracker.hex build/tracker.bin

SOURCES := src/startup.S src/main.c src/board.c src/control.c src/position_store.c src/display.c src/ui.c
HEADERS := $(wildcard src/*.h)

build/tracker.elf: $(SOURCES) $(HEADERS) src/stm32f103c8.ld
	@if not exist build mkdir build
	$(CC) $(CFLAGS) -Isrc $(SOURCES) $(LDFLAGS) -lgcc -o $@

build/tracker.hex: build/tracker.elf
	$(OBJCOPY) -O ihex $< $@

build/tracker.bin: build/tracker.elf
	$(OBJCOPY) -O binary $< $@

size: build/tracker.elf
	$(SIZE) $<

