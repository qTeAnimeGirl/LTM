.global _start
.text
.align 2
_start:
    // write(1, msg, 39)
    mov x0, #1
    adr x1, msg
    mov x2, #39
    mov x8, #64 // sys_write
    svc #0

    // exit(0)
    mov x0, #0
    mov x8, #93 // sys_exit
    svc #0

.data
msg:
    .ascii "Hello from unmodified Linux ARM64 ELF!\n"
