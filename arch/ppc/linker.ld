/* BlueyOS PowerPC Linker Script — iMac G4 "Sunflower"
 * "That's a ripper machine!" — Bandit Heeler
 *
 * Memory layout for the iMac G4 with 128 MB RAM.
 * BootX loads our kernel at 0x00600000 and jumps to _start.
 *
 *   0x00000000 – 0x000000FF   Exception vector table (relocated here by kernel)
 *   0x00001000 – 0x005FFFFF   Open Firmware + BootX scratch area (do not use)
 *   0x00600000 – 0x00DFFFFF   BlueyOS kernel image   (max 8 MB)
 *   0x00E00000 – 0x00FFFFFF   Kernel stack            (2 MB)
 *   0x01000000 – 0x06FFFFFF   Kernel heap             (~96 MB with 128 MB RAM)
 *   0x07000000 – 0x07FFFFFF   User process space      (16 MB)
 *
 * The frame buffer (ATI Radeon) is mapped at 0x90000000 by Open Firmware.
 *
 * ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
 *
 * Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
 * licensed by BBC Studios. BlueyOS is an unofficial fan/research project
 * with no affiliation to Ludo Studio or the BBC.
 */

ENTRY(_start)

MEMORY
{
    /* Exception vectors relocated to physical 0x0 — 4 KB */
    VECTORS (rwx) : ORIGIN = 0x00000000, LENGTH = 0x00001000

    /* Kernel image loaded by BootX at 0x600000 — 8 MB max */
    RAM     (rwx) : ORIGIN = 0x00600000, LENGTH = 0x00800000
}

SECTIONS
{
    /* -----------------------------------------------------------------------
     * Exception vector stubs — copied to 0x00000000 at runtime by kernel_main
     * --------------------------------------------------------------------- */
    .vectors 0x00000000 (NOLOAD) :
    {
        KEEP(*(.vectors))
    }

    /* -----------------------------------------------------------------------
     * Kernel image starts at BootX load address
     * --------------------------------------------------------------------- */
    . = 0x00600000;

    .text :
    {
        *(.text._start)     /* _start must be at the very beginning */
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    } > RAM

    .data :
    {
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
    } > RAM

    /* Small BSS sections (PowerPC EABI uses .sbss for small globals) */
    .sbss :
    {
        __sbss_start = .;
        *(.sbss)
        *(.sbss.*)
        __sbss_end = .;
    } > RAM

    .bss :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    kernel_end = .;

    /DISCARD/ :
    {
        *(.debug*)
        *(.comment)
        *(.note*)
    }
}
