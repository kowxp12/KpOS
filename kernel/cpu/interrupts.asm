BITS 32

global irq_stub_table
extern irq_handler

%macro IRQ 2
global irq%1
irq%1:
    push dword %2          ; push the interrupt number
    jmp irq_common
%endmacro

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

irq_common:
    pusha                  ; save all general registers
    mov eax, [esp + 32]    ; the interrupt number we pushed
    push eax
    call irq_handler
    add esp, 4
    popa                   ; restore registers
    add esp, 4             ; remove the interrupt number
    iret                   ; return to the interrupted code

section .data
irq_stub_table:
    dd irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
    dd irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

section .note.GNU-stack noalloc noexec nowrite progbits