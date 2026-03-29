/* BlueyOS Linker Script
 * Loads kernel at 1MB physical address
 * "Bandit knows where everything goes!" - Bluey
 */
OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)

SECTIONS {
    . = 0x100000;

    .text : {
        *(.multiboot)
        *(.text)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(COMMON)
        *(.bss)
    }

    kernel_end = .;
}
