/* BlueyOS Linker Script
 * Loads kernel at 1MB physical address
 * "Bandit knows where everything goes!" - Bluey
 */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0x100000;

    .text : {
        *(.multiboot)
        *(.text)
    } :text

    .rodata : {
        *(.rodata*)
    } :text

    .data : {
        *(.data)
    } :data

    .bss : {
        *(COMMON)
        *(.bss)
    } :data

    kernel_end = .;
}
