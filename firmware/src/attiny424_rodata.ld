ENTRY(_start)
SECTIONS {
    .text 0 : { *(.text) }
    /* GNU AVR encodes a data-space address with the 0x800000 offset. */
    .rodata 0x808100 : AT(0x100) { *(.rodata) }
}
