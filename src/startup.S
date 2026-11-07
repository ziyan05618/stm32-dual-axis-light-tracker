.syntax unified
.cpu cortex-m3
.thumb

.global g_pfnVectors
.global Reset_Handler
.extern main
.extern SysTick_Handler

.section .isr_vector,"a",%progbits
.type g_pfnVectors,%object
g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word Default_Handler /* NMI */
    .word Default_Handler /* HardFault */
    .word Default_Handler /* MemManage */
    .word Default_Handler /* BusFault */
    .word Default_Handler /* UsageFault */
    .word 0,0,0,0
    .word Default_Handler /* SVC */
    .word Default_Handler /* DebugMon */
    .word 0
    .word Default_Handler /* PendSV */
    .word SysTick_Handler
    /* STM32F103 medium-density IRQ 0..42 */
    .rept 43
    .word Default_Handler
    .endr
.size g_pfnVectors, .-g_pfnVectors

.section .text.Reset_Handler,"ax",%progbits
.thumb_func
Reset_Handler:
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
1:  cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:  ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0
3:  cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b
4:  bl main
5:  b 5b

.section .text.Default_Handler,"ax",%progbits
.thumb_func
Default_Handler:
    b board_fault

