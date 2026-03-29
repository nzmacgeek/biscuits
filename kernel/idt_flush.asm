; BlueyOS IDT flush - load the IDTR register
; Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
; licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
bits 32
section .note.GNU-stack noalloc noexec nowrite progbits

global idt_flush
idt_flush:
    mov eax, [esp+4]
    lidt [eax]
    ret
