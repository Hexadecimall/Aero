.global _start
.text
_start:
    ldr x0, [sp]
    add x1, sp, #8
    bl programMain
    mov x8, #93
    svc #0
