	.file	"test.c"
	.text
	.section .rdata,"dr"
	.align 8
.LC1:
	.ascii "{\"result\": %lld, \"average_ms\": %.5f, \"runs\": %d, \"n\": %d}\12\0"
	.section	.text.startup,"x"
	.p2align 4
	.globl	main
	.def	main;	.scl	2;	.type	32;	.endef
	.seh_proc	main
main:
	pushq	%r15
	.seh_pushreg	%r15
	pushq	%r14
	.seh_pushreg	%r14
	pushq	%r13
	.seh_pushreg	%r13
	pushq	%r12
	.seh_pushreg	%r12
	pushq	%rbp
	.seh_pushreg	%rbp
	pushq	%rdi
	.seh_pushreg	%rdi
	pushq	%rsi
	.seh_pushreg	%rsi
	pushq	%rbx
	.seh_pushreg	%rbx
	subq	$104, %rsp
	.seh_stackalloc	104
	.seh_endprologue
	movl	$1000000, %r13d
	movl	$1, %r14d
	movl	%ecx, %ebx
	movq	%rdx, %rsi
	call	__main
	cmpl	$1, %ebx
	jg	.L22
.L2:
	movslq	%r13d, %rax
	xorl	%ebx, %ebx
	xorl	%r15d, %r15d
	movabsq	$7378697629483820647, %rbp
	leaq	2(%rax,%rax), %r12
	leaq	80(%rsp), %rax
	movq	%rax, 56(%rsp)
	.p2align 4
	.p2align 3
.L8:
	leaq	64(%rsp), %rdx
	movl	$1, %ecx
	call	clock_gettime64
	testl	%r13d, %r13d
	jle	.L12
	movl	$2, %r8d
	xorl	%edi, %edi
	xorl	%r11d, %r11d
	xorl	%r9d, %r9d
	xorl	%r10d, %r10d
	xorl	%ecx, %ecx
	jmp	.L7
	.p2align 4,,10
	.p2align 3
.L24:
	cmpq	$49999, %rcx
	jg	.L14
	xorl	%r11d, %r11d
	movq	%rdx, %r10
.L5:
	addq	$2, %r8
	movl	$1, %r9d
	cmpq	%r8, %r12
	je	.L23
.L7:
	addq	%r8, %rcx
	movq	%rcx, %rax
	imulq	%rbp
	movq	%rcx, %rax
	sarq	$63, %rax
	sarq	$2, %rdx
	subq	%rax, %rdx
	cmpq	%rdx, %r10
	sete	%al
	testb	%r9b, %al
	jne	.L5
	testb	%r11b, %r11b
	je	.L24
.L14:
	addq	$2, %r8
	movq	%rdx, %rdi
	movq	%rdx, %r10
	movl	$1, %r11d
	movl	$1, %r9d
	cmpq	%r8, %r12
	jne	.L7
.L23:
	xorl	%eax, %eax
	testb	%r11b, %r11b
	cmove	%rax, %rdi
.L4:
	movq	56(%rsp), %rdx
	movl	$1, %ecx
	addl	$1, %ebx
	call	clock_gettime64
	movq	80(%rsp), %rax
	subq	64(%rsp), %rax
	imulq	$1000000000, %rax, %rax
	movl	88(%rsp), %edx
	subl	72(%rsp), %edx
	movslq	%edx, %rdx
	addq	%rdx, %rax
	addq	%rax, %r15
	cmpl	%r14d, %ebx
	jne	.L8
.L3:
	pxor	%xmm1, %xmm1
	pxor	%xmm0, %xmm0
	movl	%r13d, 32(%rsp)
	movl	%r14d, %r9d
	cvtsi2sdl	%r14d, %xmm0
	cvtsi2sdq	%r15, %xmm1
	divsd	%xmm0, %xmm1
	movq	%rdi, %rdx
	leaq	.LC1(%rip), %rcx
	divsd	.LC0(%rip), %xmm1
	movq	%xmm1, %r8
	movapd	%xmm1, %xmm2
	call	__mingw_printf
	xorl	%eax, %eax
	addq	$104, %rsp
	popq	%rbx
	popq	%rsi
	popq	%rdi
	popq	%rbp
	popq	%r12
	popq	%r13
	popq	%r14
	popq	%r15
	ret
	.p2align 4,,10
	.p2align 3
.L12:
	xorl	%edi, %edi
	jmp	.L4
.L22:
	movq	8(%rsi), %rcx
	call	atoi
	movl	%eax, %r13d
	cmpl	$2, %ebx
	je	.L2
	movq	16(%rsi), %rcx
	call	atoi
	movl	%eax, %r14d
	testl	%eax, %eax
	jg	.L2
	xorl	%r15d, %r15d
	xorl	%edi, %edi
	jmp	.L3
	.seh_endproc
	.section .rdata,"dr"
	.align 8
.LC0:
	.long	0
	.long	1093567616
	.def	__main;	.scl	2;	.type	32;	.endef
	.ident	"GCC: (Rev11, Built by MSYS2 project) 15.2.0"
	.def	clock_gettime64;	.scl	2;	.type	32;	.endef
	.def	atoi;	.scl	2;	.type	32;	.endef
