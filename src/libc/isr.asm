[bits 32]
[global isr_timer]
[global isr_keyboard]
[extern isr_timer_handler]
[extern isr_keyboard_handler]

isr_timer:
    pusha
    call isr_timer_handler
    popa
    iret

isr_keyboard:
    pusha
    call isr_keyboard_handler
    popa
    iret