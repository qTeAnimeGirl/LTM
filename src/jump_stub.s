.text
.align 2
.global _ltm_jump_to_entry

_ltm_jump_to_entry:
    // x0 = entry_point
    // x1 = guest_sp
    mov sp, x1
    mov x16, x0

    // Clear all registers as expected by Linux ARM64 ABI
    mov x0, #0
    mov x1, #0
    mov x2, #0
    mov x3, #0
    mov x4, #0
    mov x5, #0
    mov x6, #0
    mov x7, #0
    mov x8, #0
    mov x9, #0
    mov x10, #0
    mov x11, #0
    mov x12, #0
    mov x13, #0
    mov x14, #0
    mov x15, #0
    mov x17, #0
    mov x18, #0
    mov x19, #0
    mov x20, #0
    mov x21, #0
    mov x22, #0
    mov x23, #0
    mov x24, #0
    mov x25, #0
    mov x26, #0
    mov x27, #0
    mov x28, #0
    mov x29, #0
    mov x30, #0

    // Reset Linux Thread-Local Storage pointer to 0
    msr tpidr_el0, xzr

    br x16

.global _ltm_resume_context_asm
_ltm_resume_context_asm:
    // x0 = pointer to arm_thread_state64_t
    // x1 = optional pointer to arm_neon_state64_t (or NULL)
    // x2 = use_x18_as_trampoline (1 for inflate_table w18 trap, 0 for syscalls/standard)
    cmp x1, #0x1000
    b.lo 1f
    ldp q0, q1, [x1, #0]
    ldp q2, q3, [x1, #32]
    ldp q4, q5, [x1, #64]
    ldp q6, q7, [x1, #96]
    ldp q8, q9, [x1, #128]
    ldp q10, q11, [x1, #160]
    ldp q12, q13, [x1, #192]
    ldp q14, q15, [x1, #224]
    ldp q16, q17, [x1, #256]
    ldp q18, q19, [x1, #288]
    ldp q20, q21, [x1, #320]
    ldp q22, q23, [x1, #352]
    ldp q24, q25, [x1, #384]
    ldp q26, q27, [x1, #416]
    ldp q28, q29, [x1, #448]
    ldp q30, q31, [x1, #480]
    ldr w3, [x1, #512]
    msr fpsr, x3
    ldr w3, [x1, #516]
    msr fpcr, x3
1:
    ldr w1, [x0, #264]      // Load __cpsr
    msr nzcv, x1            // Restore NZCV condition flags
    ldr x1, [x0, #248]      // Load guest __sp
    mov sp, x1

    cbnz w2, 2f

    // Standard mode (syscalls, clone, fork, execve): jump via x16, restore x18
    ldr x16, [x0, #256]     // Load target __pc into x16
    ldr x17, [x0, #136]     // Restore guest x17 from offset 136
    ldp x29, x30, [x0, #232]
    ldr x28, [x0, #224]
    ldp x26, x27, [x0, #208]
    ldp x24, x25, [x0, #192]
    ldp x22, x23, [x0, #176]
    ldp x20, x21, [x0, #160]
    ldp x18, x19, [x0, #144] // Restore guest x18 AND x19 from offset 144
    ldp x14, x15, [x0, #112]
    ldp x12, x13, [x0, #96]
    ldp x10, x11, [x0, #80]
    ldp x8, x9, [x0, #64]
    ldp x6, x7, [x0, #48]
    ldp x4, x5, [x0, #32]
    ldp x2, x3, [x0, #16]
    ldp x0, x1, [x0, #0]
    br x16

2:
    // Virtualized x18 mode (inflate_table brk #0x1801..0x1804): jump via x18, restore x16 & x17
    ldr x18, [x0, #256]     // Load target __pc into physical x18
    ldp x16, x17, [x0, #128] // Restore guest x16 AND x17 from offset 128
    ldp x29, x30, [x0, #232]
    ldr x28, [x0, #224]
    ldp x26, x27, [x0, #208]
    ldp x24, x25, [x0, #192]
    ldp x22, x23, [x0, #176]
    ldp x20, x21, [x0, #160]
    ldr x19, [x0, #152]      // Restore guest x19 from offset 152
    ldp x14, x15, [x0, #112]
    ldp x12, x13, [x0, #96]
    ldp x10, x11, [x0, #80]
    ldp x8, x9, [x0, #64]
    ldp x6, x7, [x0, #48]
    ldp x4, x5, [x0, #32]
    ldp x2, x3, [x0, #16]
    ldp x0, x1, [x0, #0]
    br x18

.global _ltm_sigreturn_stub
_ltm_sigreturn_stub:
    mov x8, #139
    brk #0

.global _ltm_sig_handler_on_stack
_ltm_sig_handler_on_stack:
    // x0 = sig, x1 = si, x2 = ctx, x3 = new_sp
    mov sp, x3
    b _ltm_sig_handler_impl

.global _ltm_call_on_stack
_ltm_call_on_stack:
    // x0 = fn, x1 = sig, x2 = si, x3 = ctx, x4 = new_sp
    mov x16, sp
    mov sp, x4
    stp x16, x30, [sp, #-16]!
    mov x9, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    blr x9
    ldp x16, x30, [sp], #16
    mov sp, x16
    ret



