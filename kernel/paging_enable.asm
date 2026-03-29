; BlueyOS Paging Enable - ASM stub to activate virtual memory
; "Bandit flips the big switch!" - Episode ref: "Hammerbarn"
; Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
; licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
bits 32

section .note.GNU-stack noalloc noexec nowrite progbits

global paging_enable

; void paging_enable(uint32_t page_dir_phys)
; Loads CR3 with the page directory physical address, then enables paging
; by setting bit 31 (PG) of CR0.
paging_enable:
    mov  eax, [esp+4]    ; first argument: physical address of page directory
    mov  cr3, eax        ; load page directory base register
    mov  eax, cr0
    or   eax, 0x80000000 ; set PG bit (bit 31)
    mov  cr0, eax
    ret
