ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5); /* PF_R | PF_X */
    rodata PT_LOAD FLAGS(4); /* PF_R */
    data   PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS
{
    . = 0x400000;

    .text ALIGN(0x1000) : {
        *(.text*)
    } :text

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata*)
    } :rodata

    . = ALIGN(0x1000);
    .data : {
        *(.data*)
    } :data

    .bss : {
        *(COMMON)
        *(.bss*)
    } :data

    /DISCARD/ : {
        *(.note.GNU-stack)
        *(.comment)
    }
}
