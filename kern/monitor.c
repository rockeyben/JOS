// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "trace back through stack to find debugging infomation", mon_backtrace},
	{ "showmappings", "show vm mapping", mon_VMmapping},
	{ "sm", "show vm mapping", mon_VMmapping},
	{ "setperm", "set permission", set_VMperm},
	{ "dumpVM", "dump contents within a range of VM", dump_VM}
};

/***** Implementations of basic kernel monitor commands *****/

int parse_0x(char *s_num)
{
	int res = 0;
	char num;

	//cprintf("Parsing...\n");

	char * ptr = s_num+2;
	while((num=*(ptr++))!= '\0')
	{
		res = res * 16;
		if(num>=48 && num<=57)
			res += (num - '0');
		else if(num>=65 && num <=70)
			res += (num - 'A') + 10;
		else if(num>=97 && num <=102)
			res += (num - 'a') + 10;
		else{
			cprintf("wrong number format\n");
			return -1;
		}
	}
	return res;
}

int mon_VMmapping(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 3){
		cprintf("wrong input format\n");
		return -1;
	}

	uintptr_t v_s = 0, v_e = 0;
	v_s = (uintptr_t)parse_0x(argv[1]);
	v_e = (uintptr_t)parse_0x(argv[2]);

	if(v_s == -1 || v_e == -1)
	{
		return -1;
	}

	cprintf("Trying to show mappings in vm range [0x%x, 0x%x]\n", v_s, v_e);

	uintptr_t va = v_s;

	for(;va<=v_e;va+=PGSIZE)
	{
		struct VMmappinginfo info;
		debuginfo_VMmapping(va, &info);
		cprintf("va:%x pa:%x perm: ", (&info)->va, (&info)->pa);
		int perm = (&info)->perm;
		if((perm & PTE_W) != 0)
			cprintf("PTE_W ");
		if((perm & PTE_U) != 0)
			cprintf("PTE_U ");
		if((perm & PTE_P) != 0)
			cprintf("PTE_P ");
		cprintf("\n");
	}

	return 0;
}

int set_VMperm(int argc, char **argv, struct Trapframe*tf)
{
	if(argc != 3)
	{
		cprintf("wrong input format\n");
		return -1;
	}

	int perm = 0;
	char p;
	char * ptr = argv[1];

	while((p = *(ptr++))!='\0'){
		if(p == 'w')
			perm |= PTE_W;
		else if(p == 'u')
			perm |= PTE_U;
		else if(p == '!'){
			p = *(ptr++);
			if(p == 'w')
				perm &= (~PTE_W);
			else if(p == 'u')
				perm &= (~PTE_U);
		}
	}

	uintptr_t va = 0;
	va = (uintptr_t)parse_0x(argv[2]);
	
	debug_set_VMperm(va, perm);

	return 0;
}


int dump_VM(int argc, char **argv, struct Trapframe*tf)
{
	if(argc != 4){
		cprintf("wrong input format\n");
		return -1;
	}

	// dump physical address
	if(argv[1][1] == 'p'){
		physaddr_t p_s = 0, p_e = 0;
		p_s = (physaddr_t)parse_0x(argv[2]);
		p_e = (physaddr_t)parse_0x(argv[3]);

		if(p_s == -1 || p_e == -1)
		{
			return -1;
		}

		physaddr_t pa = p_s;
		for(;pa<=p_e;pa+=PGSIZE){
			int i = 0;
			char * content = (char*)page2kva(pa2page(pa));;
			for(;i<=PGSIZE;i++){
				cprintf("%x\n", content[i]);
			}
			cprintf("\n");
		}
	}

	if(argv[1][1] == 'v'){
		uintptr_t v_s = 0, v_e = 0;
		v_s = (uintptr_t)parse_0x(argv[2]);
		v_e = (uintptr_t)parse_0x(argv[3]);

		if(v_s == -1 || v_e == -1)
		{
			return -1;
		}

		uintptr_t va = v_s;
		for(;va<=v_e;va+=PGSIZE){
			int i = 0;
			char * content = (char*)va;
			for(;i<=PGSIZE;i++){
				cprintf("%x\n", content[i]);
			}
			cprintf("\n");
		}
	}

	return 0;

}

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t ebp, eip;
	asm volatile("movl %%ebp,%0" : "=r" (ebp));
	uint32_t * ptr = (uint32_t*)ebp;

	while(ebp != 0)
	{
		cprintf("ebp %x eip %x args %08x %08x %08x %08x %08x\n", ebp, *(ptr+1), 
		        *(ptr+2), *(ptr+3), *(ptr+4), *(ptr+5), *(ptr+6));
		struct Eipdebuginfo info;
		debuginfo_eip(*(ptr+1), &info);
		cprintf("%s:%d: %.*s+%d\n", (&info)->eip_file, (&info)->eip_line, 
				(&info)->eip_fn_namelen, (&info)->eip_fn_name, *(ptr+1)-(&info)->eip_fn_addr);
		ebp = *(ptr);
		ptr = (uint32_t*)ebp;
	}
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
