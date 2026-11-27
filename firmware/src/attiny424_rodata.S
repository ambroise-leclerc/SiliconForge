.global _start
.section .text
_start:
    ldi r30, lo8(values)
    ldi r31, hi8(values)
    ld r24, Z
    cpi r24, 0x42
    brne failure
    break
failure:
    rjmp failure

.section .rodata,"a",@progbits
values:
    .byte 0x42
