ENTRY(Reset_Handler)
MEMORY
{
 FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 62K
 RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS
{
 .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
 .text : { . = ALIGN(4); *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } > FLASH
 .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
 .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > FLASH
 .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT> FLASH
 _sidata = LOADADDR(.data);
 .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
 . = ALIGN(8); _end = .;
 ASSERT(_end + 2048 <= _estack, "Insufficient stack space")
 /DISCARD/ : { *(.comment*) *(.note*) }
}
