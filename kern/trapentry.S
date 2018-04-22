/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>



###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps

#define EC(name, num)\
.data;\
	.long name;\
.text;\
	.global name;\
	.type name, @function;\
	.align 2;\
	name:\
	pushl $(num);\
	jmp _alltraps

#define NOEC(name, num)\
.data;\
	.long name;\
.text;\
	.global name;\
	.type name, @function;\
	.align 2;\
	name:\
	push $0;\
	pushl $(num);\
	jmp _alltraps

#define haha()\
.data;\
	.long 0

.data
	.p2align 2
	.globl funs

funs:

.text

/*challenge here*/

	NOEC(t_divide, 0)
	NOEC(t_debug, 1)
	EC(t_nmi, 2)
	NOEC(t_breakpoint, 3)
	NOEC(t_overflow, 4)
	NOEC(t_bound_check, 5)
	NOEC(t_illegal_op, 6)
	NOEC(t_device, 7)
	EC(t_double_fault, 8)
	haha()
	EC(t_tss, 10)
	EC(t_segment_not_present, 11)
	EC(t_stack, 12)
	EC(t_gp_fault, 13)
	EC(t_pg_fault, 14)
	haha()
	EC(t_fp_err, 16)
	EC(t_align, 17)
	EC(t_machine, 18)
	EC(t_simd_err, 19)
.data
	.space 112
.text
	NOEC(i_syscall, 48)



/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
/*
	TRAPHANDLER_NOEC(t_divide, 0)
	TRAPHANDLER_NOEC(t_debug, 1)
	TRAPHANDLER(t_nmi, 2)
	TRAPHANDLER_NOEC(t_breakpoint, 3)
	TRAPHANDLER_NOEC(t_overflow, 4)
	TRAPHANDLER_NOEC(t_bound_check, 5)
	TRAPHANDLER_NOEC(t_illegal_op, 6)
	TRAPHANDLER_NOEC(t_device, 7)
	TRAPHANDLER(t_double_fault, 8)
	TRAPHANDLER(t_tss, 10)
	TRAPHANDLER(t_segment_not_present, 11)
	TRAPHANDLER(t_stack, 12)
	TRAPHANDLER(t_gp_fault, 13)
	TRAPHANDLER(t_pg_fault, 14)
	TRAPHANDLER(t_fp_err, 16)
	TRAPHANDLER(t_align, 17)
	TRAPHANDLER(t_machine, 18)
	TRAPHANDLER(t_simd_err, 19)
	TRAPHANDLER_NOEC(i_syscall, 48)
*/

/*
 * Lab 3: Your code here for _alltraps
 */

_alltraps:
	pushl %ds
	pushl %es
	pushal
	pushl $GD_KD
	popl %ds 
	pushl $GD_KD
	popl %es
	pushl %esp
	call trap 