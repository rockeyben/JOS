// hello, world
#include <inc/lib.h>

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
