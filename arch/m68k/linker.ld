/* BlueyOS M68K Linker Script — Macintosh LC III
 * "It was the 80s!" — Bandit Heeler
 *
 * Memory layout for the Macintosh LC III (4 MB built-in RAM):
 *
 *   0x00000000 – 0x00000400   Mac low-memory globals (reserved by ROM)
 *   0x00000400 – 0x00007FFF   Interrupt vector table + ROM scratch area
 *   0x00008000 – 0x0007FFFF   BlueyOS kernel image  (max ~480 KB)
 *   0x00080000 – 0x0009FFFF   Kernel stack           (128 KB)
 *   0x000A0000 – 0x001FFFFF   Kernel heap            (~1.4 MB)
 *   0x00200000 – 0x003BFFFF   User process space     (~1.75 MB)
 *   0x003C0000 – 0x003FFFFF   Video RAM              (256 KB, 640×480×8bpp)
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
    /* Skip the first 32 KB reserved by the Mac ROM low-memory globals */
    RAM (rwx) : ORIGIN = 0x00008000, LENGTH = 0x00078000   /* ~480 KB kernel space */
}

SECTIONS
{
    . = 0x00008000;

    /* -----------------------------------------------------------------------
     * Interrupt vector table (68030: 256 × 4-byte vectors = 1 KB).
     * We position it at 0x8000 and set VBR accordingly in startup.S.
     * --------------------------------------------------------------------- */
    .vectors :
    {
        KEEP(*(.vectors))
    } > RAM

    /* -----------------------------------------------------------------------
     * Code
     * --------------------------------------------------------------------- */
    .text :
    {
        *(.text._start)     /* startup stub must be first */
        *(.text)
        *(.text.*)
    } > RAM

    /* -----------------------------------------------------------------------
     * Read-only data
     * --------------------------------------------------------------------- */
    .rodata :
    {
        *(.rodata)
        *(.rodata.*)
    } > RAM

    /* -----------------------------------------------------------------------
     * Initialised data (loaded from image, run from RAM — same region here)
     * --------------------------------------------------------------------- */
    .data :
    {
        *(.data)
        *(.data.*)
    } > RAM

    /* -----------------------------------------------------------------------
     * BSS — zeroed by startup.S before kernel_main_m68k()
     * --------------------------------------------------------------------- */
    .bss :
    {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    /* Expose end of kernel image for heap initialisation */
    kernel_end = .;

    /* -----------------------------------------------------------------------
     * Discard DWARF debug info from the final image (saves space on floppy)
     * --------------------------------------------------------------------- */
    /DISCARD/ :
    {
        *(.debug*)
        *(.comment)
        *(.note*)
    }
}
