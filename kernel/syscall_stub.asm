; BlueyOS Syscall ASM Stub - int 0x80 gateway
; "Daddy Daughter Day" - Bandit always answers when called!
; Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
; licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
bits 32

section .note.GNU-stack noalloc noexec nowrite progbits
section .text

global syscall_stub
global syscall_enter_user_frame
extern syscall_dispatch
extern syscall_saved_es
extern syscall_saved_fs
extern syscall_saved_gs

; Syscall entry point - registered as IDT gate 0x80 (DPL=3 so user can call)
; Calling convention: EAX=syscall number, EBX=arg1, ECX=arg2, EDX=arg3
; Returns result in EAX.
;
; NOTE: ES and FS are saved to per-entry globals (interrupts disabled) and
; restored individually.  GS is now saved and restored via the registers_t
; frame so that syscall handlers (e.g. set_thread_area) can update it by
; writing to regs->gs or setting syscall_saved_gs.  If SMP support is added,
; these globals must become per-CPU storage.
syscall_stub:
    cli
    push dword 0
    push dword 0x80
    pushad
    ; Save es/fs explicitly before overwriting them with the kernel selector.
    mov  ax, es
    mov  [syscall_saved_es], eax
    mov  ax, fs
    mov  [syscall_saved_fs], eax
    mov  ax, ds
    push eax            ; registers_t.ds
    mov  ax, gs
    push eax            ; registers_t.gs  (top of frame; may be modified by handler)
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
    mov  gs, ax         ; restore GS from registers_t.gs (handler may have changed it)
    mov  [syscall_saved_gs], eax
    mov  eax, [syscall_saved_es]
    mov  es, ax
    mov  eax, [syscall_saved_fs]
    mov  fs, ax
    mov  eax, [syscall_saved_gs]
    mov  gs, ax         ; ensure GS is correct after es/fs restore path
    pop  eax
    mov  ds, ax
    popad
    add  esp, 8
    sti
    iret

; Enter user mode directly from a saved registers_t frame.
; Argument: [esp+4] = const registers_t *
syscall_enter_user_frame:
    cli
    mov  edx, [esp+4]

    mov  ax, [edx+4]    ; ds
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  ax, [edx+0]    ; gs
    mov  gs, ax

    push dword [edx+64] ; ss
    push dword [edx+60] ; useresp
    push dword [edx+56] ; eflags
    push dword [edx+52] ; cs
    push dword [edx+48] ; eip

    mov  edi, [edx+8]
    mov  esi, [edx+12]
    mov  ebp, [edx+16]
    mov  ebx, [edx+24]
    mov  ecx, [edx+32]
    mov  eax, [edx+36]
    mov  edx, [edx+28]
    iret
