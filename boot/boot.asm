; BlueyOS Boot Entry Point
; "It's a big day!" - Bluey Heeler
; Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
; licensed by BBC Studios. BlueyOS is an unofficial fan/research project
; with no affiliation to Ludo Studio or the BBC.

MBOOT_MAGIC    equ 0x1BADB002
MBOOT_FLAGS    equ 0x00000007   ; page-align modules + memory map + video mode
MBOOT_CHECKSUM equ -(MBOOT_MAGIC + MBOOT_FLAGS)

section .multiboot
align 4
    dd MBOOT_MAGIC
    dd MBOOT_FLAGS
    dd MBOOT_CHECKSUM
    dd 0                    ; header_addr (unused for ELF)
    dd 0                    ; load_addr
    dd 0                    ; load_end_addr
    dd 0                    ; bss_end_addr
    dd 0                    ; entry_addr
    dd 0                    ; mode_type: linear graphics
    dd 1024                 ; preferred width
    dd 768                  ; preferred height
    dd 32                   ; preferred depth

section .bss
align 16
stack_bottom:
    resb 16384          ; 16 KiB kernel stack
stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
section .text
bits 32
global _start
extern kernel_main

_start:
    mov  esp, stack_top     ; set up stack (grows downward)
    push ebx                ; multiboot info struct pointer (arg2)
    push eax                ; multiboot magic number (arg1)
    call kernel_main        ; never returns
    cli
.hang:
    hlt
    jmp .hang
