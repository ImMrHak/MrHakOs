[bits 64]

global isr_timer
global isr_keyboard
extern isr_timer_handler
extern isr_keyboard_handler

%macro PUSH_REGS 0
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POP_REGS 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
%endmacro

isr_timer:
    PUSH_REGS
    call isr_timer_handler
    POP_REGS
    iretq

isr_keyboard:
    PUSH_REGS
    call isr_keyboard_handler
    POP_REGS
    iretq
