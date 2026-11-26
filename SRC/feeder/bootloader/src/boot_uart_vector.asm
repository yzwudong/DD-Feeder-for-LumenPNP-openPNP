; UART1 shared vector at 0x0023; common handler at 0x1700
        .area CSEG (ABS)
        .org 0x0023
        ljmp    _boot_uart_common

        .org 0x17C0
_boot_uart_common:
        mov     a, _boot_mode
        jz      00002$
        push    acc
        push    psw
        push    dpl
        push    dph
        push    b
        lcall   _boot_uart_isr
        pop     b
        pop     dph
        pop     dpl
        pop     psw
        pop     acc
        reti
00002$:
        ljmp    0x1823


