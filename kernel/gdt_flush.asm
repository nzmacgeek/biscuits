; BlueyOS GDT flush - loads the new GDT and reloads segments
; "Bandit sorted all the segments!" - Bluey
; Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
; licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
bits 32
section .note.GNU-stack noalloc noexec nowrite progbits
section .text

global gdt_flush
global tss_flush
gdt_flush:
    mov eax, [esp+4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret
tss_flush:
    ; Load the TSS descriptor at GDT index 5 ((5 << 3) == selector 0x28, RPL 0).
    ; The previous 0x2B value kept index 5 but set the low RPL bits to 3; using
    ; that user-RPL selector here can fault before the IDT is installed and
    ; reset the kernel.
    mov ax, 0x28
    ltr ax
    ret
