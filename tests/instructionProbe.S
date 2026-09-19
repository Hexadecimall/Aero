.text
#ifdef __APPLE__
.global _instructionProbe
_instructionProbe:
#else
.global instructionProbe
instructionProbe:
#endif
    mov x9, x0
    mov x1, #-1
    adds x2, x1, #1
    cset x3, cs
    stp x2, x3, [x9], #16
    mov x1, #0x7fffffffffffffff
    adds x2, x1, #1
    cset x3, vs
    stp x2, x3, [x9], #16
    mov w1, #0x80000000
    subs w2, w1, #1
    cset x3, vs
    stp x2, x3, [x9], #16
    mov x1, #-1
    mov x2, #0xdef0
    movk x2, #0x9abc, lsl #16
    movk x2, #0x5678, lsl #32
    movk x2, #0x1234, lsl #48
    umulh x3, x1, x2
    smulh x4, x1, x2
    stp x3, x4, [x9], #16
    mov x1, #-123
    mov x2, #7
    sdiv x3, x1, x2
    udiv x4, x1, x2
    stp x3, x4, [x9], #16
    mov x1, #0x8000000000000000
    mov x2, #-1
    sdiv x3, x1, x2
    udiv x4, x1, xzr
    stp x3, x4, [x9], #16
    mov x1, #0xdef0
    movk x1, #0x9abc, lsl #16
    movk x1, #0x5678, lsl #32
    movk x1, #0x1234, lsl #48
    asr x3, x1, #17
    eor x4, x1, x1, ror #13
    stp x3, x4, [x9], #16
    mov x1, #-123
    asr x3, x1, #5
    ubfx x4, x1, #7, #19
    stp x3, x4, [x9], #16
    mov x1, #0xdef0
    movk x1, #0x9abc, lsl #16
    movk x1, #0x5678, lsl #32
    movk x1, #0x1234, lsl #48
    mov x2, #0xabcd
    bfi x1, x2, #9, #16
    sbfx x3, x2, #8, #8
    stp x1, x3, [x9], #16
    mov w1, #0x4321
    movk w1, #0x8765, lsl #16
    str w1, [x9]
    ldrsw x3, [x9]
    ldrsb x4, [x9, #3]
    stp x3, x4, [x9], #16
    mov x1, #-2
    mov x2, #0xaaaa
    and x3, x1, #0x00ff00ff00ff00ff
    orr x4, x2, #0xcccccccccccccccc
    stp x3, x4, [x9], #16
    cmp x1, #1
    csel x3, x1, x2, lt
    csneg x4, x1, x2, eq
    stp x3, x4, [x9], #16
    ret
