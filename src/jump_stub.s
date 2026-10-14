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
    mov x16, x0

    // Load guest sp
    ldr x1, [x16, #248]
    mov sp, x1

    // Load guest pc
    ldr x17, [x16, #256]

    // Load fp (x29) and lr (x30)
    ldp x29, x30, [x16, #232]

    // Load registers in pairs
    ldp x2, x3, [x16, #16]
    ldp x4, x5, [x16, #32]
    ldp x6, x7, [x16, #48]
    ldp x8, x9, [x16, #64]
    ldp x10, x11, [x16, #80]
    ldp x12, x13, [x16, #96]
    ldp x14, x15, [x16, #112]
    ldp x18, x19, [x16, #144]
    ldp x20, x21, [x16, #160]
    ldp x22, x23, [x16, #176]
    ldp x24, x25, [x16, #192]
    ldp x26, x27, [x16, #208]
    ldr x28, [x16, #224]

    // Load x0 and x1 last
    ldp x0, x1, [x16, #0]

    br x17
