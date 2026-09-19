// Linux AArch64 executable: print arguments separated by spaces.
.global _start
.text
_start:
    ldr x19, [sp]
    sub x19, x19, #1
    add x20, sp, #16
    cbz x19, newline
next:
    ldr x1, [x20]
    mov x2, #0
length:
    add x3, x1, x2
    ldrb w4, [x3]
    cbz w4, output
    add x2, x2, #1
    b length
output:
    mov x0, #1
    mov x8, #64
    svc #0
    sub x19, x19, #1
    cbz x19, newline
    adr x1, space
    mov x2, #1
    mov x0, #1
    mov x8, #64
    svc #0
    add x20, x20, #8
    b next
newline:
    adr x1, linefeed
    mov x2, #1
    mov x0, #1
    mov x8, #64
    svc #0
    mov x0, #0
    mov x8, #93
    svc #0
space: .ascii " "
linefeed: .ascii "\n"
