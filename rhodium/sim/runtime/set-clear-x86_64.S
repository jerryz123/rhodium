# Computes wide clear-wins scoreboard updates using baseline System V AMD64 SSE2.
.text
.p2align 4
.globl rds_set_clear_x86_64
.type rds_set_clear_x86_64, @function
rds_set_clear_x86_64:
    cmp $2, %r8
    jb .Ltail
.p2align 4
.Lpair:
    movdqu (%rsi), %xmm0
    movdqu (%rdx), %xmm1
    por %xmm1, %xmm0
    movdqu (%rcx), %xmm1
    pandn %xmm0, %xmm1
    movdqu %xmm1, (%rdi)
    add $16, %rsi
    add $16, %rdx
    add $16, %rcx
    add $16, %rdi
    sub $2, %r8
    cmp $2, %r8
    jae .Lpair
.Ltail:
    test %r8, %r8
    jz .Ldone
    mov (%rsi), %rax
    or (%rdx), %rax
    mov (%rcx), %r9
    not %r9
    and %r9, %rax
    mov %rax, (%rdi)
.Ldone:
    ret
.size rds_set_clear_x86_64, .-rds_set_clear_x86_64
.section .note.GNU-stack,"",@progbits
