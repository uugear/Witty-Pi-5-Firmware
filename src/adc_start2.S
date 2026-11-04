/*
 * ADC conversion routine with controlled instruction timing.
 *
 * On some RP2350 boards, consecutive ADC conversions may exhibit
 * alternating high and low readings, as well as occasional outliers.
 * Experiments showed that inserting two NOP instructions immediately
 * after starting each ADC conversion significantly reduces these
 * alternating errors.
 *
 * This routine uses hand-written assembly to preserve the exact
 * instruction sequence and timing around the ADC START operation.
 * It performs five conversions per call and executes directly from
 * Flash (XIP). The caller performs 15 conversions in total and
 * applies a trimmed mean to further suppress remaining outliers.
 *
 * Arguments:
 *   r0 = CS.ERR output bytes
 *   r1 = ADC CS SET alias
 *   r2 = ADC CS register
 *   r3 = output samples (five 16-bit words)
 */

.syntax unified
.cpu cortex-m33
.thumb

.section .text.wp5_adc_start2, "ax", %progbits
.p2align 3
// Preserve the phase-0 loop placement used by R3 path A.
.hword 0xbf00
.global wp5_adc_convert5_start2
.type wp5_adc_convert5_start2, %function
.thumb_func
wp5_adc_convert5_start2:
    push.w {r4, r10, r11, lr}
    mov.w r11, #4
    mov r12, r3
    add.w r10, r3, #10
1:
    str.w r11, [r1]
    nop
    nop
2:
    ldr r3, [r2]
    lsls r3, r3, #23
    bpl 2b
    ldr r3, [r2, #4]
    strh r3, [r12], #2
    ldr r3, [r2]
    cmp r10, r12
    ubfx r3, r3, #9, #1
    strb r3, [r0], #1
    bne 1b
    pop.w {r4, r10, r11, pc}
.size wp5_adc_convert5_start2, . - wp5_adc_convert5_start2
