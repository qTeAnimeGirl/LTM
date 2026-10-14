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

.global _ltm_resume_context
_ltm_resume_context:
    // x0 = pointer to arm_thread_state64_t
    ldr w1, [x0, #264]      // Load __cpsr
    msr nzcv, x1            // Restore NZCV condition flags
    ldr x1, [x0, #248]      // Load guest __sp
    mov sp, x1
    ldr x16, [x0, #256]     // Load target __pc into x16
    ldr x17, [x0, #136]     // Restore guest x17 from offset 136
    ldp x29, x30, [x0, #232]
    ldr x28, [x0, #224]
    ldp x26, x27, [x0, #208]
    ldp x24, x25, [x0, #192]
    ldp x22, x23, [x0, #176]
    ldp x20, x21, [x0, #160]
    ldp x18, x19, [x0, #144]
    ldp x14, x15, [x0, #112]
    ldp x12, x13, [x0, #96]
    ldp x10, x11, [x0, #80]
    ldp x8, x9, [x0, #64]
    ldp x6, x7, [x0, #48]
    ldp x4, x5, [x0, #32]
    ldp x2, x3, [x0, #16]
    ldp x0, x1, [x0, #0]
    br x16

.global _ltm_sigreturn_stub
_ltm_sigreturn_stub:
    mov x8, #139
    brk #0
