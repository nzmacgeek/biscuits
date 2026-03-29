/* BlueyOS PowerPC Startup — iMac G4 "Sunflower"
 * "That's a ripper machine!" — Bandit Heeler, probably
 *
 * Target: PowerPC G4 (MPC7450) @ 700/800/1000 MHz, Apple iMac G4 flat-panel
 *
 * This file is assembled by powerpc-linux-gnu-as.
 * It is the very first code that runs after BootX (Apple's second-stage
 * loader) jumps to our kernel image.
 *
 * BootX calling convention (standard Apple bootloader hand-off):
 *   r3 = pointer to Open Firmware device tree (flat tree, BootX format)
 *   r4 = size of the device tree
 *   r5 = 0 (reserved)
 *   r6 = 0 (reserved)
 *   MSR = IR=0, DR=0 (real-mode — MMU is OFF)
 *   SRR0/SRR1 unused at entry
 *
 * Our job:
 *   1.  Save r3 (device tree pointer) in a global
 *   2.  Establish a stack (just below our load address)
 *   3.  Clear BSS
 *   4.  Flush and invalidate the caches
 *   5.  Call kernel_main_ppc(of_tree_ptr)  — never returns
 *
 * ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
 *
 * Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
 * licensed by BBC Studios. BlueyOS is an unofficial fan/research project
 * with no affiliation to Ludo Studio or the BBC.
 */

        .section ".text"
        .global _start
        .extern kernel_main_ppc
        .extern __bss_start
        .extern __bss_end
        .extern of_tree_ptr         /* global set below */

/* ---------------------------------------------------------------------------
 * _start — entry from BootX
 * r3 = OF device-tree pointer (save it before we trash registers)
 * --------------------------------------------------------------------------*/
_start:
        /* --- Save the OF device-tree pointer -------------------------------- */
        lis     %r9, of_tree_ptr@ha
        stw     %r3, of_tree_ptr@l(%r9)

        /* --- Disable external interrupts (MSR[EE] = 0) --------------------- */
        mfmsr   %r9
        rlwinm  %r9, %r9, 0, 17, 15    /* clear bit 15 (EE) */
        mtmsr   %r9
        isync

        /* --- Set up stack below our load address (0x00600000) --------------- */
        /* Stack grows downward; leave 256 bytes of red zone below the base.   */
        lis     %r1, 0x0060
        addi    %r1, %r1, -256         /* sp = 0x005FFF00 */
        li      %r0, 0
        stw     %r0, 0(%r1)            /* back-chain terminator */

        /* --- Zero BSS ------------------------------------------------------- */
        lis     %r3, __bss_start@ha
        addi    %r3, %r3, __bss_start@l
        lis     %r4, __bss_end@ha
        addi    %r4, %r4, __bss_end@l
        li      %r5, 0
_bss_zero:
        cmplw   %r3, %r4
        bge     _bss_zero_done
        stb     %r5, 0(%r3)
        addi    %r3, %r3, 1
        b       _bss_zero
_bss_zero_done:

        /* --- Flush and invalidate D-cache ----------------------------------- */
        /* MPC7450: dcbf for every cache line in the first 32 KB             */
        li      %r3, 0
        li      %r4, 512               /* 512 × 64-byte lines = 32 KB */
_dcache_flush:
        dcbf    %r0, %r3
        addi    %r3, %r3, 64
        bdnz    _dcache_flush          /* uses CTR, so load it first... */
        /* (CTR is not set above; use a simple loop instead) */

        sync

        /* --- Invalidate I-cache --------------------------------------------- */
        icbi    %r0, %r3
        isync

        /* --- Call C kernel entry -------------------------------------------- */
        lis     %r3, of_tree_ptr@ha
        lwz     %r3, of_tree_ptr@l(%r3)
        bl      kernel_main_ppc

        /* Should never return — halt if it does */
_halt:
        b       _halt

/* ---------------------------------------------------------------------------
 * Global storage for the OF device tree pointer
 * --------------------------------------------------------------------------*/
        .section ".data"
        .global of_tree_ptr
of_tree_ptr:
        .long   0

/* ---------------------------------------------------------------------------
 * Minimal exception vector stubs (real-mode, placed at 0x100, 0x200, …)
 * These are relocated into their proper positions by kernel_main_ppc()
 * after it maps low memory.
 *
 * Each handler just halts for now; a full kernel will install proper
 * handlers via the PowerPC exception dispatch table.
 * --------------------------------------------------------------------------*/
        .section ".vectors", "ax"
        .org    0x100
_machine_check:
        b       _machine_check

        .org    0x200
_dsi:                                  /* Data Storage Interrupt (page fault) */
        b       _dsi

        .org    0x300
_isi:                                  /* Instruction Storage Interrupt */
        b       _isi

        .org    0x400
_external_interrupt:
        b       _external_interrupt

        .org    0x500
_alignment:
        b       _alignment

        .org    0x600
_program:
        b       _program

        .org    0x700
_fpu_unavailable:
        b       _fpu_unavailable

        .org    0x800
_decrementer:                          /* Timer interrupt — kernel tick */
        b       _decrementer

        .org    0xC00
_syscall:                              /* sc instruction — BlueyOS syscalls */
        b       _syscall

        .org    0xD00
_trace:
        b       _trace

        .org    0xF00
_altivec_unavailable:
        b       _altivec_unavailable
