# MIT 6.828 LAB4

## Introduction

这个Lab我们将实现抢占式的多进程机制。在Part A中我们将实现JOS中的round robin调度，以及一些基础的和进程相关的系统调用。Part B内，我们将实现unix风格的fork()，可以让一个进程生成自己的一份拷贝。在Part C中，我们会实现进程间通信（IPC）。

## Excercise

### Excercise 1

```
Implement mmio_map_region in kern/pmap.c. To see how this is used, look at the beginning of lapic_init in kern/lapic.c. You'll have to do the next exercise, too, before the tests for mmio_map_region will run. 
```

首先要介绍一下在JOS中，我们将实现symmetric multiprocessing : SMP。也就是每个CPU对于系统资源都有平等的访问权限。虽然所有的CPU在功能上是平等的，但是在boot的过程中，这些CPU会被分成两部分：bootstrap processor(BSP)和application processor(AP)。BSP负责初始化系统，而AP是在操作系统开始运行之后，由BSP激活的。至于哪个CPU是BSP，这是烧在BIOS里的。

在一个SMP系统中，每个CPU都有一个Local APIC单元，LAPIC负责传播和接收中断，同时，它也给每个CPU提供了身份标识。在这个Lab中，我们使用LAPIC的如下功能：

- 可以调用cpunum()知道我们的code是在哪块cpu上运行。
- 从BSP发送STARTUP中断来让别的AP开始运行。
- 在part C中，我们利用LAPIC里的内置计数器，来实现抢占式调度。

那么处理器是怎么访问LAPIC的呢？答案是MMIO，在memory maped io中，有一部分物理地址被硬件方式连接到了一些IO设备的寄存器上，这样，当我们使用load/store指令访问这些地址的时候，就相当于在访问IO设备的寄存器。例如我们在Lab1里就可以通过0xA0000处的IO hole来访问VGA设备。LAPIC的IO hole在0xFE000000的地方（比4G小32MB），如果使用KERBASE来访问的话有点太高了，所以JOS在virtural memory的MMIOBASE这个地方预留了4MB的空间，让我们映射这些IO设备。

那么我们需要实现一下pmap.c里的mmio_map_region。

这里需要注意的地方在code的comment里也说了，由于这片VA不是普通的DRAM：我们不能cache它们。这很好理解，想象一下，IO设备的输出端口每被读一次，当前的值就没有后续意义了，因为就可能有新的有意义的值过来，如果我们还读cache里的值，那么就会错过新的值。同样，对于写操作，我们也需要设置成write through。在之前我们已经实现了一个非常好用的功能函数boot_map_region，所以只要调用一下然后注意权限的设置就可以了。

```c
	static uintptr_t base = MMIOBASE;
	size = ROUNDUP(size, PGSIZE);
	if(base+size > MMIOLIM){
		panic("mmio overflow!");
	}
	boot_map_region(kern_pgdir, base, size, pa, PTE_W | PTE_PCD | PTE_PWT | PTE_P);
	base = base + size;
	return (void*)(base - size);
```

### Exercise 2

AP bootstrap

在boot AP之前，BSP应该收集各个AP的信息，包括AP总数，AP ID，MMIO 地址等等。kern/mpconfig.c里的mp_init()函数会检索这些信息，这些信息在哪呢，在BIOS的MP configuration table里。

boot_aps()函数会启动AP bootstrap进程，AP会进入实模式，就像boot.S一样。然后boot_aps会把kern/mpentry.S的内容拷进一个实模式可以访问的地方。和boot loader不一样，我们是可以控制AP从哪里开始执行的，我们把entry地址存在MPENTRY_ADDR(0x7000)这个地方，但其实640KB以下的任何对齐的物理地址都是可以的。

在那之后，boot_aps会一个个地激活AP，具体的方式是发送STARTUP IPI给LAPIC，也会发送AP的开始执行地址。在AP进入保护模式后，会调用mp_main()初始化。boot_aps会等待AP在CpuInfo内存放一个CPU_STARTED信号来判断是否开始激活下一个AP。

```
 Read boot_aps() and mp_main() in kern/init.c, and the assembly code in kern/mpentry.S. Make sure you understand the control flow transfer during the bootstrap of APs. Then modify your implementation of page_init() in kern/pmap.c to avoid adding the page at MPENTRY_PADDR to the free list, so that we can safely copy and run AP bootstrap code at that physical address. Your code should pass the updated check_page_free_list() test (but might fail the updated check_kern_pgdir() test, which we will fix soon). 
```

这里的要求非常友好，boot_aps和mp_main都写好了，我们只要静静地欣赏就行……要修改的地方是page init里，需要把MPENTRY_PADDR这个地方标成not free。

```c
size_t i;
for (i = 0; i < npages; i++) {
    pages[i].pp_ref = 0;
    pages[i].pp_link = page_free_list;
    // 4MB, which is mentioned in ;entry.S'
    if( i == 0 || (i >= IOPHYSMEM/PGSIZE && i < 1024) || i == MPENTRY_PADDR/PGSIZE){
        continue;
    }
    page_free_list = &pages[i];
}
```

### Excercise 3

per-cpu state and initialization

```
Modify mem_init_mp() (in kern/pmap.c) to map per-CPU stacks starting at KSTACKTOP, as shown in inc/memlayout.h. The size of each stack is KSTKSIZE bytes plus KSTKGAP bytes of unmapped guard pages. Your code should pass the new check in check_kern_pgdir(). 
```

既然涉及到多核处理，那么接下来对于多核系统的初始化也要做一些操作。由于我们是多核共享一个地址空间，所以在内存初始化的时候要为他们精心分配不同的栈空间。按照要求，栈从KSTACKTOP开始往下增长，每个CPU拥有KSTKSIZE+KSTKGAP的栈空间。其中KSTKGAP是为了保护栈溢出而做的措施，以防一个CPU出故障后影响到别的CPU，也就是所谓的guard page。

```c
int i = 0;
for(i = 0; i < NCPU; i++){
    uintptr_t kstacktop_i = KSTACKTOP - i * (KSTKSIZE + KSTKGAP);
    boot_map_region(kern_pgdir, kstacktop_i - KSTKSIZE, KSTKSIZE,
                    PADDR(percpu_kstacks[i]), PTE_W|PTE_P);
}
```

### Exercise 4

```
The code in trap_init_percpu() (kern/trap.c) initializes the TSS and TSS descriptor for the BSP. It worked in Lab 3, but is incorrect when running on other CPUs. Change the code so that it can work on all CPUs. (Note: your new code should not use the global ts variable any more.) 
```

在初始化Task State Segment的时候，之前是只初始化了一个CPU的TSS，在多CPU的时候，要根据cpunum来初始化不同的区域。

```c
thiscpu->cpu_ts.ts_esp0 = KSTACKTOP - cpunum()*(KSTKSIZE+KSTKGAP);
thiscpu->cpu_ts.ts_ss0 = GD_KD;
thiscpu->cpu_ts.ts_iomb = sizeof(struct Taskstate);

// Initialize the TSS slot of the gdt.
gdt[(GD_TSS0 >> 3)+cpunum()] = SEG16(STS_T32A, (uint32_t) (&thiscpu->cpu_ts),
                                     sizeof(struct Taskstate) - 1, 0);
gdt[(GD_TSS0 >> 3)+cpunum()].sd_s = 0;

// Load the TSS selector (like other segment selectors, the
// bottom three bits are special; we leave them 0)
ltr(GD_TSS0 + (cpunum() << 3));
```

### Excercise 5

```
 Apply the big kernel lock as described above, by calling lock_kernel() and unlock_kernel() at the proper locations. 
```

关于锁

JOS提供了一个大的内核锁，定义在kern/spinlock.h里。所谓内核大锁，就是当一个进程进入内核模式时，会锁住所有的内核资源，直到它返回用户模式时，才会释放锁。换句话说，在这种设定下，用户模式下的进程可以在不同的CPU上同时运行，但是在内核模式下，只有一个进程才能运行。

JOS也提供了lock_kernel(), unlock_kernel()两个函数，用来获得、释放锁。根据要求，我们需要在如下4个地方上锁：

- 在i386_init()内，在BSP唤醒其他CPU之前上锁。
- mp_main()，在初始化AP之后上锁，然后调用sched_yield进入内核态去开始运行AP上的进程
- trap()，从用户态陷入后，要上锁，然后检查trap是发生在内核态还是用户态。
- env_run()，在返回用户态之前，释放锁。

i386_init

```c
// Acquire the big kernel lock before waking up APs
lock_kernel();
// Starting non-boot CPUs
boot_aps();
```

mp_main

```c
// Now that we have finished some basic setup, call sched_yield()
// to start running processes on this CPU.  But make sure that
// only one CPU can enter the scheduler at a time!
//
// Your code here:
lock_kernel();
sched_yield();
```

trap

```c
if ((tf->tf_cs & 3) == 3) {
    // Trapped from user mode.
    // Acquire the big kernel lock before doing any
    // serious kernel work.
    // LAB 4: Your code here.

    lock_kernel();

    assert(curenv);

    // Garbage collect if current enviroment is a zombie
    if (curenv->env_status == ENV_DYING) {
        env_free(curenv);
        curenv = NULL;
        sched_yield();
    }

    // Copy trap frame (which is currently on the stack)
    // into 'curenv->env_tf', so that running the environment
    // will restart at the trap point.
    curenv->env_tf = *tf;
    // The trapframe on the stack should be ignored from here on.
    tf = &curenv->env_tf;
}
```

env_run

```c
unlock_kernel();
env_pop_tf(&e->env_tf);
```

个人理解：

总之，以上的修改就是贯彻”内核态必须同步“的思想，前两个限制是为了在初始化的内核阶段保持多CPU间的同步。后两个修改是为了让后续运行的过程中，一旦从用户态陷入内核态（通过trap），就上锁;一旦从内核态返回用户态（通过调度，也就是env_run），就释放锁，这很符合操作系统的哲学。

### Exercise 6

Round Robin调度

sched_yield这个函数用来负责选择哪个进程下一个上CPU，它会从刚刚运行的进程开始，遍历envs数组，然后挑选第一个ENV_RUNNABLE的进程，并调用env_run去运行这个进程。

这里要注意，绝对不能选择ENV_RUNNING的进程，因为我们不允许同一个进程运行在两个不同的CPU上。同时，我们还增加了新提供的sys_yield()系统调用，来在用户模式下主动地让出CPU资源（其实就是提供了调用sched_yield的接口）。

```c
struct Env *idle;
int i = 0;

if(curenv){
    idle = curenv;
}
else{
    idle = envs;
}

for(i; i < NENV; i++){
    if(idle == envs + NENV)
        idle -= NENV;
    if(idle->env_status == ENV_RUNNABLE){
        //cprintf("find one %x %d %d\n", idle, idle -envs,  NENV);
        env_run(idle);
    }
    idle += 1;	
}


if(curenv && curenv->env_status == ENV_RUNNING){
    env_run(curenv);
    return ;
}
```

这里，我们还不能忘记给sys_yield添加syscall的注册，在dispatch的时候，将这个syscall对应到相应的函数上。我就是没看清这个题目要求在这里debug了好久……

### Excercise 7

实现创建进程的系统调用

为了实现一个unix风格的fork，我们需要处理一大堆关于拷贝地址空间的问题。我们知道，fork一个进程后，两个进程在有些地方共享相同的地址映射的（如果是copy-on-write机制，后面会更加具体地实现），有些是需要重新分配物理地址的，所以有这样3个相关的系统调用需要我们实现：

- sys_page_alloc：允许用户在指定的进程地址空间下，分配一段物理内存并将它映射到某段虚拟地址上。
- sys_page_map：将一个进程的地址映射关系拷贝到另一个进程内，注意，只是拷贝映射关系，而不是拷贝地址里的内容，这样新的和旧的进程就共享了同一块物理地址。
- sys_page_unmap：取消一段地址上的映射关系。

接着是两个关于进程设置的系统调用：

- sys_exofork：原型函数，创建一个和原进程一模一样的进程，唯一不同的地方在于返回值的不同。我们知道父进程会返回子进程的envid，而子进程会返回0。但是这里有一个实现细节，那就是，我们一开始并不会把子进程设置为RUNNABLE，所以子进程并不会运行，也就不会返回，那么我们是如何通过返回值来区分父子进程呢？这里用到一个非常非常trick的技巧，那就是强行把子进程的tf里的eax改成0,”假装“返回了0,可以说是非常骚的操作了。
- sys_env_set_status：将一个进程设置为ENV_RUNNABLE或者ENV_NOT_RUNNABLE

```
	Implement the system calls described above in kern/syscall.c and make sure syscall() calls them. You will need to use various functions in kern/pmap.c and kern/env.c, particularly envid2env(). For now, whenever you call envid2env(), pass 1 in the checkperm parameter. Be sure you check for any invalid system call arguments, returning -E_INVAL in that case. Test your JOS kernel with user/dumbfork and make sure it works before proceeding. 
```

sys_exofork:

```c
	struct Env*e;
	int err = env_alloc(&e, curenv->env_id); 
	if(err == 0){
		e->env_status = ENV_NOT_RUNNABLE;
		memcpy(&(e->env_tf), &(curenv->env_tf), sizeof(struct Trapframe));
		e->env_tf.tf_regs.reg_eax = 0; // tweaked !!!!!!!! oh my godness!!!
		// quoted from dumbfork.c
		// Allocate a new child environment.
		// The kernel will initialize it with a copy of our register state,
		// so that the child will appear to have called sys_exofork() too -
		// except that in the child, this "fake" call to sys_exofork()
		// will return 0 instead of the envid of the child.
		return e->env_id;
	}
	
	return err;
```

sys_env_set_status:

```c
	struct Env*e;
	int err = envid2env(envid, &e, 1);
	
	if(err < 0)
		return -E_BAD_ENV;
	
	if(status != ENV_NOT_RUNNABLE && status != ENV_RUNNABLE)
		return -E_INVAL;
	
	e->env_status = status;
	
	return 0;
```

sys_page_alloc:

```c
	struct Env*e;

	int err = envid2env(envid, &e, 1);
	//cprintf("err is %x envid is %x\n", err, envid);
	if(err < 0)
		return err;

	if((unsigned)va >= UTOP || va != ROUNDUP(va, PGSIZE))
		return -E_INVAL;

	if(!(perm & PTE_P))
		return -E_INVAL;

	if(!(perm & PTE_U))
		return -E_INVAL;
	
	if((perm | PTE_SYSCALL) != PTE_SYSCALL)
		return -E_INVAL;


	struct PageInfo* pg = page_alloc(ALLOC_ZERO);
	pg->pp_ref ++;
	if(pg == NULL)
		return -E_NO_MEM;
	
	err = page_insert(e->env_pgdir, pg, va, perm);
	if(err < 0){
		pg->pp_ref--;
		page_free(pg);
	}
	
	return err;
```

sys_page_map:

```c
	struct Env*src_env;
	struct Env*dst_env;
	int err1 = envid2env(srcenvid, &src_env, 1);
	int err2 = envid2env(dstenvid, &dst_env, 1);
	if(err1 < 0 || err2 < 0)
		return -E_BAD_ENV;
	

	if((unsigned)srcva >= UTOP || (unsigned)dstva >= UTOP
		|| srcva != ROUNDUP(srcva, PGSIZE)
		|| dstva != ROUNDUP(dstva, PGSIZE))
		return -E_INVAL;
	
	pte_t * pte;
	struct PageInfo*src_pg = page_lookup(src_env->env_pgdir, srcva, &pte);
	
	if(src_pg == NULL)
		return -E_INVAL;

	if(!(perm & PTE_P))
		return -E_INVAL;

	if(!(perm & PTE_U))
		return -E_INVAL;
	
	if((perm | PTE_SYSCALL) != PTE_SYSCALL)
		return -E_INVAL;
	
	uint32_t src_perm = *(pte) & 0xfff;
	if(!(src_perm & PTE_W) && (perm & PTE_W))
		return -E_INVAL;

	int err = page_insert(dst_env->env_pgdir, src_pg, dstva, perm);
	
	return err;
	
```

sys_page_unmap:

```c

	struct Env*e;
	int err = envid2env(envid, &e, 1);
	if(err < 0)
		return err;
	
	if((unsigned)va >= UTOP || va != ROUNDUP(va, PGSIZE))
		return -E_INVAL;
	
	page_remove(e->env_pgdir, va);
	return 0;
```

个人觉得，这个exercise除了exofork的那个假装返回的骚操作，别的就是一些细致的sanity check，需要按照要求一步步地完成，别的就是调用之前已经实现的pmap.c里的函数，进行包装。

### Exercise 8

现在我们进入到了part B，就是要实现一个效率更高一点的fork，那就是采用了copy-on-write机制的fork。所谓copy on write，就是我们只拷贝地址的映射，并把这些page人为地标为read-only的，这样当我们尝试去写这些page的时候，就会产生page fault，但是这个page fault是我们事先预估到的，是我们认为设计的，我们可以通过类似于PTE_COW这样的标志位来判断。所以，我们可以对这样的page fault进行特殊处理，那就是重新分配一个新的物理空间给它，这样我们就实现了内存空间的动态分配，而不是一次性把所有的内容都拷贝给子进程。

那么我们首先实现一下新的page fault。和之前的陷入内核的page fault不一样，这一次我们把page fault后转向的函数改成了在用户态下自定义的一个函数，这个函数可以实现我们想要的page fault的功能，比如分配新的页，读进需要的内容，等等。那么我们就需要把这个进程的env_pgfault_upcall修改，指向我们的函数指针。但是在设置之前，我们需要检查传入的函数指针是否是用户可以访问的区域，假如我们不做这个sanity check，那么用户可能可以传入一个恶意的内核态的函数指针，间接地进入内核态，就像测试样例中的faultevilhandler做的那样。

```c
	struct Env*e;
	int err = envid2env(envid, &e, 1);
	if(err < 0)
		return err;
	// check whether the func is invalid, for test faultevilhandler
	user_mem_assert(e, func, 1, PTE_U);
	e->env_pgfault_upcall = func;
	return 0;
```

### Exercise 9

之后就是栈了。将处理错误的栈和普通的栈区分开来是一种良好的设定，它能保证处理错误的代码是“安全的”，同时也不会影响到原程序的执行。在这里，我们设置了一个User Exception Stack，代号是UXSTACKTOP。当我们触发user mode page fault的时候，需要我们实现栈的切换。

在切换栈之后，我们要把一个类似TrapFrame的东西push到栈里，在这里是一个UTrapFrame结构，里面保存了返回原来代码需要的现场信息：

```
                    <-- UXSTACKTOP
trap-time esp
trap-time eflags
trap-time eip
trap-time eax       start of struct PushRegs
trap-time ecx
trap-time edx
trap-time ebx
trap-time esp
trap-time ebp
trap-time esi
trap-time edi       end of struct PushRegs
tf_err (error code)
fault_va            <-- %esp when handler is run
```

这些需要我们在汇编代码里实现。

但是还有一种情况，那就是加入进程已经在user exception stack下执行了，那么我们就不是从栈顶开始push了，而是从当前栈顶：tf->esp开始push，而且我们需要先push一个32-bit empty word。为什要这个empty word呢？因为我们需要在这里存一个临时变量：在从UXSTACK返回的时候，我们需要很小心。我们不能用jmp，因为那样需要用一个寄存器来存目的地址。我们也不能直接ret，因为这样的话栈还是没有切换回去，所以JOS用了一种非常tricky的方式：

- 把eip先push到原来的栈上
- 在ret之前切换到原来的栈，这样就可以既ret回原来的指令，也切换回了原来的栈

假如我们没有递归地进入UXSTACK，那么我们就不需要push empty word。但是如果是递归进入的话，那么“原来的栈”就是现在的栈了，于是就要空出一个位置来存这个eip。

啊，真的是非常非常精妙啊。

先看一下新的trap.c要怎么写吧：

```c
	if(curenv->env_pgfault_upcall){
		uintptr_t cur_esp = tf->tf_esp;
		uintptr_t utf_addr;
		// already in user exception stack
		if(cur_esp >= UXSTACKTOP - PGSIZE && cur_esp <= UXSTACKTOP - 1){
			user_mem_assert(curenv, (void*)(tf->tf_esp-sizeof(struct UTrapframe)-4), sizeof(struct UTrapframe), PTE_W);
			utf_addr = tf->tf_esp-sizeof(struct UTrapframe)-4;
		}
		else{
			user_mem_assert(curenv, (void*)(UXSTACKTOP-sizeof(struct UTrapframe)), sizeof(struct UTrapframe), PTE_W);
			utf_addr = UXSTACKTOP-sizeof(struct UTrapframe);
		}

		// a good way to set memory without 'memcpy'
		struct UTrapframe*utf = (struct UTrapframe*)utf_addr;
		utf->utf_regs = tf->tf_regs;
		utf->utf_eip = tf->tf_eip;
		utf->utf_eflags = tf->tf_eflags;
		utf->utf_esp = tf->tf_esp;
		utf->utf_err = tf->tf_err;
		utf->utf_fault_va = fault_va;
	
		// set next instruction
		curenv->env_tf.tf_eip = (uintptr_t)curenv->env_pgfault_upcall;
		// switch to user exception stack
		curenv->env_tf.tf_esp = utf_addr;
		env_run(curenv);
	}
```

个人感觉比较tricky的地方在于，怎么把utf存到栈上，一开始我还以为要写什么内联汇编手动push进去……想了很久，后来才知道原来可以直接用取地址的方式存进去，非常简单，把起始地址强转成一个UTrapFrame的结构，然后往里写就行了。

### Excersice 10

```asm
	movl 0x28(%esp), %edx
	subl $0x4, 0x30(%esp)
	movl 0x30(%esp), %eax
	movl %edx, (%eax)

	// Restore the trap-time registers.  After you do this, you
	// can no longer modify any general-purpose registers.
	// LAB 4: Your code here.
	addl $0x8, %esp
	popal

	// Restore eflags from the stack.  After you do this, you can
	// no longer use arithmetic operations or anything else that
	// modifies eflags.
	// LAB 4: Your code here.
	addl $0x4, %esp
	popfl // special pop for eflags


	// Switch back to the adjusted trap-time stack.
	// LAB 4: Your code here.
	popl %esp
	// Return to re-execute the instruction that faulted.
	// LAB 4: Your code here.
	ret
```

如果要写好这个exercise，那么对于之前的为什么要push empty word的这个问题要非常非常深刻地理解，否则我们可能会误以为，临时的eip是直接存在当前所有的位置顶上。这是不对的，实际上，我们需要先把存在UXSTACK里的tf->esp这个trap-time stack拿出来，存在这个的顶上。我们查找一下定义，大概知道utrapframe占多少字节，就可以精确地找到tf->esp了。之后就是popal和popfl。这里需要注意popfl，是一个陌生的指令，专门用来pop eflags的。我们还需要注意一个坑：pop完之后，esp已经自动上升了，我们不要再手动加了……

### Exercise 11

```c
void
set_pgfault_handler(void (*handler)(struct UTrapframe *utf))
{
    int r;

	if (_pgfault_handler == 0) {
		// First time through!
		// LAB 4: Your code here.
		if(sys_page_alloc(0, (void*)(UXSTACKTOP-PGSIZE), PTE_U|PTE_W|PTE_P)<0)
			panic("sys page alloc failed\n");
	}

	// Save handler pointer for assembly to call.
	_pgfault_handler = handler;
	if(sys_env_set_pgfault_upcall(0, _pgfault_upcall) < 0)
		panic("sys env set pgfault upcall failed\n");
```

这里的代码逻辑是这样的，如果我们的pagefault handler还没有赋值，说明我们的user exception stack还没有设置好，所以需要先分配物理也给它。

在那之后，我们要把强转的handler传给_pgfault_handler，这样才会在汇编代码里跳到正确的地方。

### Exercise 12

现在我们就要在fork.c中实现copy-on-write机制了。

fork()的基本控制流:

1. 父进程 用`set_pgfault_handler`设置pgfault()为 page fault handler
2. 父进程调用`sys_exofork()` 创建子环境.
3. 对于 每一个 在UTOP下方的 可以writable 或 copy-on-write 的页, 父函数 调用 `duppage`, duppage 会映射copy-on-write页到 子进程的地址 然后再重新把copy-on-write页映射到它自己的地址空间. duppage 会设置父和子的 PTE 因此 页都是不可写的, 并且在`avail`项中包含 `PTE_COW`  来区分copy-on-write pages 和真正的只读页. 用户异常栈 不需要重映射,它应当在子进程中重新申请并映射. fork() 页需要处理哪些现有的 不可写 也不是 copy-on-write的页. [感觉文档这里不太合理 duppage 具体 可以再分开讲]
4. 父进程设置 子进程的user page fault entrypoint .
5. 父进程标记子进程runnable.

每一次 环境写向一个没有权限写的copy-on-write 页, 会触发page fault. 下面是处理流程:

1. 内核传递 页错误到`_pgfault_upcall` 也就是上面说的pgfault().
2. pgfault() 检测导致fault操作是否是写 (check for `FEC_WR` in the error code) 并且检查页是否是`PTE_COW` 如果不满足则panic.
3. pgfault() 申请 新的页 映射到一个零时的位置 并复制 copy-on-write 页的内容到 新的页里. 然后修改映射关系到新的页,新的页的权限为W+R.

```c
static int
duppage(envid_t envid, unsigned pn)
{
	int r;
	void*addr = (void*)(pn*PGSIZE);

	r = 0;
	
	if(((uvpt[pn] & PTE_W)) || ((uvpt[pn] & PTE_COW))){
		r = sys_page_map(0, addr, envid, addr, PTE_COW|PTE_U|PTE_P);
		if(r < 0)
			return r;
		r = sys_page_map(0, addr, 0, addr, PTE_COW|PTE_U|PTE_P);
		if(r < 0)
			return r;
	}
	else{
		r = sys_page_map(0, addr, envid, addr, PTE_U|PTE_P);
		return r;
	}

	return 0;
}
```

```c
envid_t
fork(void)
{
	// LAB 4: Your code here.
	
	int r;

	set_pgfault_handler(pgfault);

	envid_t envid;
	uint32_t addr;
	extern unsigned char end[];
	envid = sys_exofork();

	if(envid < 0){
		panic("sys_exofork: %e", envid);
	}
	else if(envid == 0){
		// child
		thisenv = &envs[ENVX(sys_getenvid())];
		//cprintf("child finished\n");
		return 0;
	}

	for (addr = 0; addr < UTOP; addr += PGSIZE){
		// don't copy user exception stack 
		if(addr>= USTACKTOP && addr < UXSTACKTOP)
			continue;
		if((uvpd[PDX(addr)]&PTE_P) && (uvpt[PGNUM(addr)]&PTE_P) && (uvpt[PGNUM(addr)]&PTE_U))
			duppage(envid, PGNUM(addr));
	}

	sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), PTE_U|PTE_W|PTE_P);
	extern void _pgfault_upcall();
	sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);
	return envid;
	
	// panic("fork not implemented");
}
```

```c
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.

	if(!(err & FEC_WR)){
		panic("handling user pg fault, not a write access\n");
	}
	if(!( (uvpd[PDX(addr)]&PTE_P) && (uvpt[PGNUM(addr)]&PTE_P) && (uvpt[PGNUM(addr)]&PTE_COW) ))
		panic("not a cow page\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	addr = ROUNDDOWN(addr, PGSIZE);
	if(sys_page_alloc(0, PFTEMP, PTE_W|PTE_U|PTE_P)<0)
		panic("cow pgfault: sys page alloc failed\n");
	memcpy(PFTEMP, addr, PGSIZE);
	if(sys_page_map(0, PFTEMP, 0, addr, PTE_W|PTE_U|PTE_P) < 0)
		panic("cow pgfault: sys page map failed\n");
	if(sys_page_unmap(0, PFTEMP)<0)
		panic("cow pgfault: sys page unmap failed\n");

}
```

个人感觉这个部分大部分是照着dumbfork来写，思路是比较清晰的。有点难度的地方是学会利用uvpt和uvpd来方便地取到pte，从而判断权限。

### Exercise 13

为了实现时钟中断，我们需要添加一些新的中断项，为了方便，这里带了一个大礼包……顺带加了好几个，后面的lab5会用到的。

由于之前实现了challenge，所以添加interrupt的方式如下：

```c
	for(i = 0; i < 16 ; i++)
		if(i==IRQ_TIMER || i==IRQ_KBD || i==IRQ_ERROR || i==IRQ_IDE ||
			i==IRQ_SERIAL || i==IRQ_SPURIOUS){
				SETGATE(idt[i+IRQ_OFFSET], 0, GD_KT, funs[i+IRQ_OFFSET], 0);
			}
```

并在trapentry.S里添加项如下：

```asm
.data
	.space 48
.text
	NOEC(i_timer, IRQ_TIMER+IRQ_OFFSET)
	NOEC(i_kbd, IRQ_KBD+IRQ_OFFSET)
	haha()
	haha()
	NOEC(i_serial, IRQ_SERIAL+IRQ_OFFSET)
	haha()
	haha()
	NOEC(i_spurious, IRQ_SPURIOUS+IRQ_OFFSET)
	haha()
	haha()
	haha()
	haha()
	haha()
	haha()			
	NOEC(i_ide, IRQ_IDE+IRQ_OFFSET)
	haha()
	NOEC(i_syscall, 48)
	haha()
	haha()				
	NOEC(i_error, IRQ_ERROR+IRQ_OFFSET)
```

最后在env_alloc里修改，让创建的进程都是开中断的：

```c
e->env_tf.tf_eflags |= FL_IF;
```

### Exercise 14

在trap_dispatch中分配时间中断的处理函数：

```c
		case 32: 
			lapic_eoi();
			sched_yield(); return ; break;
```

先用lapic_eoi()确认收到中断，然后再调用sched_yield()，表示时间片到了，要进入调度函数，换进程啦。

### Exercise 15

这里我们需要实现Inter-Process Communication(IPC)

首先我们实现系统调用sys_ipc_recv和sys_ipc_try_send。

对于sys_ipc_recv，我们需要一直阻塞，直到返回值已经准备好了。怎么阻塞呢？首先把env_ipc_recving设成1,然后在env_ipc_dstva里设dstva，表明想要接受的态度。然后再把自己的状态设成NOT_RUNNABLE，这样就一直阻塞，直到把它唤醒。

```c
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
	
	curenv->env_ipc_recving = 1;
	if((unsigned)dstva < UTOP && dstva != ROUNDDOWN(dstva, PGSIZE)){
		return -E_INVAL;
	}

	curenv->env_ipc_dstva = dstva;

	curenv->env_status = ENV_NOT_RUNNABLE;
	sched_yield();

	return 0;
}
```

对于sys_ipc_try_send

- 如果接收者当前没有正在准备接收，那么会返回错误
- 假如发送成功了：
  - 接收者的env_ipc_recving变为0
  - 接收者的env_ipc_from设成发送者的envid
  - env_ipc_value和env_ipc_perm都设成相应的值。
  - 接受者的状态被重新标为RUNNABLE

```c
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	
	struct Env*e;
	int err;
	err = envid2env(envid, &e, 0);
	if(err < 0)
		return -E_BAD_ENV;
	
	if(e->env_ipc_recving == 0)
		return -E_IPC_NOT_RECV;
	
	unsigned addr = (unsigned)srcva;

	e->env_ipc_perm = 0;

	if(addr < UTOP){
		if(addr != ROUNDDOWN(addr, PGSIZE))
			return -E_INVAL;

		if(!(perm & PTE_P))
			return -E_INVAL;

		if(!(perm & PTE_U))
			return -E_INVAL;
		
		if((perm | PTE_SYSCALL) != PTE_SYSCALL)
			return -E_INVAL;
		
		pte_t * pte;
		struct PageInfo*src_pg = page_lookup(curenv->env_pgdir, srcva, &pte);
		
		if(src_pg == NULL)
			return -E_INVAL;

		uint32_t src_perm = *(pte) & 0xfff;
		if((perm & PTE_W) && !(src_perm & PTE_W))
			return -E_INVAL;
		if((unsigned)(e->env_ipc_dstva) < UTOP){
			err = page_insert(e->env_pgdir, src_pg, e->env_ipc_dstva, perm);
			if(err < 0)
				return -E_NO_MEM;
			e->env_ipc_perm = perm;
		}
		
	}

	e->env_ipc_recving = 0;
	e->env_ipc_from = curenv->env_id;
	e->env_ipc_value = value;
	e->env_tf.tf_regs.reg_eax = 0;
	e->env_status = ENV_RUNNABLE;
	return 0;
}
```

最后再实现lib/ipc.c里的包装函数：

```c
void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
	// LAB 4: Your code here.
	// panic("ipc_send not implemented");
	void*send_pg;
	if(pg)
		send_pg = pg;
	else
		send_pg = (void*)UTOP;
	
	int err;
	while(1){
		err = sys_ipc_try_send(to_env, val, send_pg, perm);
		if(err == 0)
			break;
		if(err < 0 && err != -E_IPC_NOT_RECV)
			panic("ipc send err here\n");
		sys_yield();
	}
}

```

```c
int32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
	// LAB 4: Your code here.
	// panic("ipc_recv not implemented");

	int err = 0;
	if(pg == NULL)
		err = sys_ipc_recv((void*)UTOP);
	else
		err = sys_ipc_recv(pg);
	
	if(err == 0){
		if(from_env_store)
			*from_env_store = thisenv->env_ipc_from;
		if(perm_store)
			*perm_store = thisenv->env_ipc_perm;
		
		return thisenv->env_ipc_value;
	}
	else {
		if(from_env_store)
			*from_env_store = 0;
		if(perm_store)
			*perm_store = 0;
		return err;
	}

}
```

## Challenge

我实现了一个简单的scheduler，采用的是简单的priority schedule。

首先在Env结构体中加了一个priority:

```c
struct Env{
    // ....
    int priority;
}
```

在进程运行的过程中，我们还需要能够修改priority，所以增加了一个系统调用：sys_change_priority:

```c
static int
sys_change_priority(int val)
{
	curenv->priority = val;
	return 0;
}
```

在fork.c中，我们修改fork，实现一个pr_fork:

```c
envid_t pr_fork(int priority)
{	
	int r;

	set_pgfault_handler(pgfault);

	envid_t envid;
	uint32_t addr;
	extern unsigned char end[];
	envid = sys_exofork();

	if(envid < 0){
		panic("sys_exofork: %e", envid);
	}
	else if(envid == 0){
		// child
		thisenv = &envs[ENVX(sys_getenvid())];
		sys_change_priority(priority);
		//cprintf("child finished\n");
		return 0;
	}

	for (addr = 0; addr < UTOP; addr += PGSIZE){
		// don't copy user exception stack 
		if(addr>= USTACKTOP && addr < UXSTACKTOP)
			continue;
		if((uvpd[PDX(addr)]&PTE_P) && (uvpt[PGNUM(addr)]&PTE_P) && (uvpt[PGNUM(addr)]&PTE_U))
			duppage(envid, PGNUM(addr));
	}

	sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), PTE_U|PTE_W|PTE_P);
	extern void _pgfault_upcall();
	sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);
	return envid;
	
	// panic("fork not implemented");

}
```

这个pr_fork会传入一个priority作为参数，然后把进程的priority设置成这个。

然后修改sched函数：每次挑选一个priority值最小的（优先级最高）的进程，作为调度进程。

```c
	// challenge
	struct Env * picked_env = NULL;
	for(i; i < NENV; i++){
		if(idle == envs + NENV)
			idle -= NENV;
		if(idle->env_status == ENV_RUNNABLE){
			if(picked_env == NULL || idle->priority < picked_env->priority)
				picked_env = idle;
		}
		idle += 1;
	}
	
	// if haven't found a env, or curenv have higher priority than picked env, just run the curenv
	if(curenv && curenv->env_status == ENV_RUNNING && 
		((picked_env==NULL || curenv->priority < picked_env->priority))){
		env_run(curenv);
	}

	if(picked_env){
		env_run(picked_env);
	}
```



我修改了hello.c运行程序：

```c
void
umain(int argc, char **argv)
{
	/*
	cprintf("hello, world\n");
	cprintf("i am environment %08x\n", thisenv->env_id);
	*/
	int i;
	for (i = 1; i <= 5; ++i) {
		int pid = pr_fork(i);
		if (pid == 0) {
			cprintf("child %x is now living!\n", i);
			int j;
			for (j = 0; j < 5; ++j) {
				cprintf("child %x is yielding!\n", i);
				sys_yield();
			}
			break;
		}
	}
}
```

输出结果如下：

```
SMP: CPU 0 found 1 CPU(s)
enabled interrupts: 1 2
[00000000] new env 00001000
[00001000] new env 00001001
[00001000] new env 00001002
child 1 is now living!
child 1 is yielding!
[00001000] new env 00001003
child 2 is now living!
child 2 is yielding!
[00001000] new env 00001004
child 3 is now living!
child 3 is yielding!
[00001000] new env 00001005
child 4 is now living!
child 4 is yielding!
[00001000] exiting gracefully
[00001000] free env 00001000
child 5 is now living!
child 5 is yielding!
child 1 is yielding!
child 1 is yielding!
child 1 is yielding!
child 1 is yielding!
[00001001] exiting gracefully
[00001001] free env 00001001
child 2 is yielding!
child 2 is yielding!
child 2 is yielding!
child 2 is yielding!
[00001002] exiting gracefully
[00001002] free env 00001002
child 3 is yielding!
child 3 is yielding!
child 3 is yielding!
child 3 is yielding!
[00001003] exiting gracefully
[00001003] free env 00001003
child 4 is yielding!
child 4 is yielding!
child 4 is yielding!
child 4 is yielding!
[00001004] exiting gracefully
[00001004] free env 00001004
child 5 is yielding!
child 5 is yielding!
child 5 is yielding!
child 5 is yielding!
[00001005] exiting gracefully
[00001005] free env 00001005

```

可以看到，我们按照序号分配priority值之后，它的调度都是严格按照序号从小到大的顺序进行调度的。

## Question

### Q1

  `MPBOOTPHYS`的定义如下：

```asm
#define MPBOOTPHYS(s) ((s) - mpentry_start + MPENTRY_PADDR)
```

由于我们是要把这段boot代码放在MPENTRY_PADDR上执行，而链接的时候它的地址是相对于mpentry_start而言的，所以这里必须要使用这个转换。

### Q2

假设A和B两个CPU都在执行用户态程序，这时A进入内核态，然后B突然发生错误，必须也进入内核态，B也必须push一些东西到内核栈上，那么此时B并不会检查锁，因为这个是写在硬件里的中断机制。

### Q3

因为e是存在内核态代码里的，根据lab3里的实现，所有pgdir的内核部分都是相同的（除了一小块），所以切换后不会有影响

### Q4

因为寄存器里存放了很多很关键的信息，而且我们不能预测进程在什么时候被打断，所以必须全部存下来，以便恢复的时候使用。由于进程的切换需要进入内核态，所以这是在陷入trap的时候由硬件保存的。
