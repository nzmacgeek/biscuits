| BlueyOS M68K Startup — Macintosh LC III
| "It was the 80s!" — Bandit Heeler
|
| Target: Motorola MC68030 @ 25 MHz, Apple Macintosh LC III
|
| This stub is assembled by m68k-linux-gnu-as (GNU assembler for M68K).
| It runs immediately after the Macintosh ROM hands control to our boot
| sector / ROM image.  The ROM has already:
|   - Performed a basic RAM test
|   - Initialised VIA1 and the ADB controller
|   - Set up the interrupt priority level to 7 (all masked)
|   - Placed SP at the top of available RAM
|
| Our job:
|   1.  Set SR to supervisor mode, IPL=7 (no interrupts yet)
|   2.  Establish a known stack pointer (top of dedicated 128 KB stack region)
|   3.  Zero the BSS segment
|   4.  Call kernel_main_m68k()  (C entry point, never returns)
|
| ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
|
| Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
| licensed by BBC Studios. BlueyOS is an unofficial fan/research project
| with no affiliation to Ludo Studio or the BBC.

        .section .text
        .global _start
        .extern kernel_main_m68k
        .extern __bss_start
        .extern __bss_end

| ---------------------------------------------------------------------------
| _start — very first instruction executed on the LC III
| ---------------------------------------------------------------------------
_start:
        | Mask all interrupts (IPL = 7), stay in supervisor mode.
        move.w  #0x2700, %sr

        | Set up our own stack: top of dedicated 128 KB stack region
        | (0x00080000 – 0x0009FFFF per linker.ld; stack grows downward,
        |  so we start at the top 0x000A0000).
        move.l  #0x000A0000, %sp

        | Zero BSS.
        lea     __bss_start, %a0
        lea     __bss_end,   %a1
_bss_zero_loop:
        cmp.l   %a1, %a0
        bge     _bss_zero_done
        clr.b   (%a0)+
        bra     _bss_zero_loop
_bss_zero_done:

        | Lower IPL to 6 (allow VBL but not NMI) — we will re-raise once
        | our own interrupt handlers are in place.
        move.w  #0x2600, %sr

        | Jump to the C kernel entry point.  This must never return.
        jsr     kernel_main_m68k

        | Safety net — halt the processor if kernel_main returns.
_halt:
        stop    #0x2700        | supervisor + IPL=7 + STOP
        bra     _halt


| ---------------------------------------------------------------------------
| _nmi_handler — NMI (level-7 auto-vector, programmer's button)
| Prints a minimal crash notice via the SCC and halts.
| ---------------------------------------------------------------------------
        .global _nmi_handler
_nmi_handler:
        | TODO: print "BlueyOS NMI - Bandit's Finger of Doom" via SCC
        stop    #0x2700
        rte
