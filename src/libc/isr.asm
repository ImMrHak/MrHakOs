[bits 32]
[global isr_keyboard]
[extern isr_keyboard_handler]

isr_keyboard:
    pusha
    call isr_keyboard_handler
    popa
    iret