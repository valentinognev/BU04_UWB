/*
 * ARM microlib printf glue symbols required by Keil-compiled .lib objects.
 * Each symbol is a unique address token used by the microlib printf engine.
 */
.syntax unified
.thumb
.text

.macro microlib_stub name
    .global \name
    .hidden \name
    .type \name, %function
\name:
    bx lr
    .size \name, . - \name
.endm

microlib_stub _printf_a
microlib_stub _printf_c
microlib_stub _printf_charcount
microlib_stub _printf_d
microlib_stub _printf_e
microlib_stub _printf_f
microlib_stub _printf_flags
microlib_stub _printf_fp_dec
microlib_stub _printf_fp_hex
microlib_stub _printf_g
/* _printf_i is provided by newlib-nano; do not stub it here. */
microlib_stub _printf_int_dec
microlib_stub _printf_l
microlib_stub _printf_lc
microlib_stub _printf_ll
microlib_stub _printf_lld
microlib_stub _printf_lli
microlib_stub _printf_llo
microlib_stub _printf_llu
microlib_stub _printf_llx
microlib_stub _printf_longlong_dec
microlib_stub _printf_longlong_hex
microlib_stub _printf_longlong_oct
microlib_stub _printf_ls
microlib_stub _printf_mbtowc
microlib_stub _printf_n
microlib_stub _printf_o
microlib_stub _printf_p
microlib_stub _printf_percent
microlib_stub _printf_pre_padding
microlib_stub _printf_return_value
microlib_stub _printf_s
microlib_stub _printf_sizespec
microlib_stub _printf_str
microlib_stub _printf_truncate_signed
microlib_stub _printf_truncate_unsigned
microlib_stub _printf_u
microlib_stub _printf_wc
microlib_stub _printf_wctomb
microlib_stub _printf_widthprec
microlib_stub _printf_x
