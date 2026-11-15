//! Tiny flash-only bootstrap. It copies the runtime before touching Rust data.
core::arch::global_asm!(
    r#"
    .section .flash_boot,"ax"
    .thumb
    .thumb_func
    .global PumperBoot
PumperBoot:
    cpsid i
    ldr r0, =__ram_load
    ldr r1, =__ram_start
    ldr r2, =__ram_copy_end
2:
    cmp r1, r2
    bhs 3f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 2b
3:
    .if {diagnostics}
    ldr r0, =_stack_start
    sub r1, r0, #131072
    ldr r2, =0xa55aa55a
4:
    cmp r1, r0
    bhs 5f
    str r2, [r1], #4
    b 4b
5:
    .endif
    ldr r0, =0xe000ed08
    ldr r1, =__vector_table
    str r1, [r0]
    dsb
    isb
    ldr r0, =Reset
    bx r0
    .ltorg
"#, diagnostics = const cfg!(feature = "diagnostics") as u8
);
