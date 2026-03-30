; BlueyOS Syscall ASM Stub - int 0x80 gateway
; "Daddy Daughter Day" - Bandit always answers when called!
; Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
; licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
bits 32

section .note.GNU-stack noalloc noexec nowrite progbits
section .text

global syscall_stub
extern syscall_dispatch
extern syscall_saved_es
extern syscall_saved_fs
extern syscall_saved_gs

; Syscall entry point - registered as IDT gate 0x80 (DPL=3 so user can call)
; Calling convention: EAX=syscall number, EBX=arg1, ECX=arg2, EDX=arg3
; Returns result in EAX.
;
; NOTE: es, fs, gs are saved to per-entry globals (interrupts disabled) and
; restored individually, preserving any values the user had set.  If SMP
; support is added, these globals must become per-CPU storage.
syscall_stub:
    cli
    push dword 0
    push dword 0x80
    pushad
    ; Save es/fs/gs explicitly before overwriting them with the kernel selector.
    mov  ax, es
    mov  [syscall_saved_es], eax
    mov  ax, fs
    mov  [syscall_saved_fs], eax
    mov  ax, gs
    mov  [syscall_saved_gs], eax
    mov  ax, ds
    push eax
    mov  ax, 0x10       ; kernel data segment
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    push esp            ; pass pointer to registers_t as argument
    call syscall_dispatch
    add  esp, 4
    ; syscall_dispatch sets regs->eax to the return value before returning.
    ; Do NOT write eax here: if the scheduler switched context, regs no longer
    ; refers to the current process's frame and the write would corrupt it.
    pop  eax
    mov  ds, ax
    mov  eax, [syscall_saved_es]
    mov  es, ax
    mov  eax, [syscall_saved_fs]
    mov  fs, ax
    mov  eax, [syscall_saved_gs]
    mov  gs, ax
    popad
    add  esp, 8
    sti
    iret
