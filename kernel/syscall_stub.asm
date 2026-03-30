; BlueyOS Syscall ASM Stub - int 0x80 gateway
; "Daddy Daughter Day" - Bandit always answers when called!
; Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
; licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
bits 32

section .note.GNU-stack noalloc noexec nowrite progbits
section .text

global syscall_stub
extern syscall_dispatch

; Syscall entry point - registered as IDT gate 0x80 (DPL=3 so user can call)
; Calling convention: EAX=syscall number, EBX=arg1, ECX=arg2, EDX=arg3
; Returns result in EAX.
syscall_stub:
    cli
    push dword 0
    push dword 0x80
    pushad
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
    mov  [esp + 32], eax ; saved eax in registers_t
    pop  eax
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    popad
    add  esp, 8
    sti
    iret
