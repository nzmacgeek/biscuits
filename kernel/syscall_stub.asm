; BlueyOS Syscall ASM Stub - int 0x80 gateway
; "Daddy Daughter Day" - Bandit always answers when called!
; Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
; licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
bits 32

section .note.GNU-stack noalloc noexec nowrite progbits

global syscall_stub
extern syscall_dispatch

; Syscall entry point - registered as IDT gate 0x80 (DPL=3 so user can call)
; Calling convention: EAX=syscall number, EBX=arg1, ECX=arg2, EDX=arg3
; Returns result in EAX.
syscall_stub:
    cli
    push ds
    push es
    push fs
    push gs
    pushad              ; push EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
    mov  ax, 0x10       ; kernel data segment
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    push esp            ; pass pointer to registers_t as argument
    call syscall_dispatch
    add  esp, 4
    popad
    pop  gs
    pop  fs
    pop  es
    pop  ds
    sti
    iret
