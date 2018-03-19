# MIT 6.828 LAB1

薛犇 1500012752

## Exercise 1

第一个练习是让我们熟悉一下x86汇编语法，由于之前上过微机实验，所以这部分还OK

## Exercise 2

这个部分是让熟悉一下qemu自带的gdb的调试方式。这里遇到过一个小坑，我以为让开两个terminal然后一个make qemu-gdb一个make gdb是为了对比，结果发现它是采用了一个类似ipython那样的机制，把gdb在本地服务器上封装，所以开两个窗口是必须的。

## Exercise 3

**1Q: At what point does the processor start executing 32-bit code? What exactly causes the switch from 16- to 32-bit mode?**

0x7c32, 采用一个ljmp指令从16-bit段跳到了32-bit段 

**2Q: What is the last instruction of the boot loader executed, and what is the first instruction of the kernel it just loaded?**

boot loader的最后一条指令：

```asm
7d6b:	ff 15 18 00 01 00    	call   *0x10018
```

也就是这样一个炫酷的函数调用方式进入kernel

```c
 ((void (*)(void)) (ELFHDR->e_entry))();
```

kernel的第一条指令：（warm up）

```asm
movw   $0x1234,0x472
```



**3Q: Where is the first instruction of the kernel**

pa： 0x10000c
va： 0xf010000c
这里面包含了一次虚拟地址的映射，因为KERNBASE的虚拟地址是0xF0000000, 所以c文件link到的地址也是KERNBASE+。。。。开始的，
但是到这里为止还没有开启虚拟地址模式，所以采用了一次手动的重定位(有一个RELOC的宏)，把[KERNBASE, KERNBASE+4MB]的虚拟地址空间和[0, 4MB]的物理地址空间对应起来。

**4Q: How does the boot loader decide how many sectors it must read in order to fetch the entire kernel from disk? Where does it find this information?**

从ELF头文件里知道的, ph->p_memsz告诉boot loader总共需要读取多少大小的数据，然后用这个总量除以SECTSIZE就知道需要读取多少个扇区了。



## Exercise 4

关于C语言的简介，还有理解指针。



## Exercise 5

关于链接地址（VMA）和加载地址（LMA），链接地址是编译器以为程序应该执行的（虚拟内存）位置，而加载地址是程序实际在内存（准确的说是虚拟内存）中存在的位置，一般来说两者是一致的，如果改了会发生错误。

根据传统，BIOS是在0x7c00这个位置把boot扇区加载进内存的，所以程序也要在这个地方开始执行，也就是链接到这个地方，仔细观察一下boot/Makefrag的话可以发现有这么一行：

```makefile
$(V)$(LD) $(LDFLAGS) -N -e start -Ttext 0x7C00 -o $@.out $^
```

虽然不一定完全看懂，但是可以猜测到，它是通过-Ttext把0x7c00链接到start这个符号上的，而start就是汇编asm中最常见的那个入口地址。

## Exercise 6

关于ELF文件：

ELF文件的全称是Executable and Linkable File，可执行可链接文件。而在通常的语境下，ELF往往表示一种格式，就像MP3有MP3的打开方式，JPG有JPG的打开方式一样，计算机需要一种格式来“正确打开”这些可执行或者可链接的文件。

ELF文件以一个header开头，这个header里包含了很多信息，其中，programme header会把所有需要加载的section的信息都包含在内，包括从哪里开始加载，加载多长，以及加载的内容是什么类型的。

我们关注以下几个程序段：

- .text 可执行的指令
- .rodata 只读数据
- .data 数据（全局变量）

如果想要对ELF文件有详细的了解，欢迎选修体系实习，体验手动解析elf的快感：）


## Exercise 7


- OS经常在非常高的地址空间运行，比如0xf0100000, 目的是为了让低的空间给用户使用。

- JOS通过kern/entrypgdir.c把[0, 4MB) 与 [KERNBASE, KERNBASE+4MB)的虚拟空间映射到[0,4MB)的物理地址空间

- entry_pgtable 中的第n项对应第n个页（4KB）

- 关于：__attribute__((__aligned__(PGSIZE)))
__attribute__是用来对某一个结构体做一些限制的，而__aligned__是一种属性，也就是必须以某种方式对齐。

- 在entry.S set CR0_PG flag之前，系统还是使用物理地址的，set 之后，就开始使用虚拟地址了。

**Q: What is the first instruction after the new mapping is established that would fail to work properly if the mapping weren't in place? Comment out the movl %eax, %cr0 in kern/entry.S, trace into it, and see if you were right.**

是jmp后的第一句，因为set cr0之后还在低地址空间，通过一个jmp跳到高地址空间。

```asm
movl $0x0,%ebp
```

## Exercise 8

修改代码如下：

```c
// (unsigned) octal
case 'o':
    // Replace this with your code.
    num = getuint(&ap, lflag);
    base = 8;
    goto number;
```


目的是为了达成八进制的打印。可以根据十进制或者十六进制的解析方式，举一反三，很容易得到修改的方式。其实就是先把要打印的数值得到，然后把进制得到，之后number处的代码会处理好后续的问题。

cprintf是一个很实用的函数，其实和平常的printf差不多，后面很多时候会用到。



**1Q: Explain the interface between printf.c and console.c. Specifically, what function does console.c export? How is this function used by printf.c?**

console.c是和硬件设备层面交互的函数，处理CGA,VGA输出以及键盘输入。
printf.c则是和用户层面交互的函数，提供了打印字符的函数，也提供了一个多样化打印模式的平台。例如可以通过printfmt等方式
规定如何打印。

cputchar(int c)

printf.c通过putch函数调用cputchar函数

**2Q: Explain the following from console.c:**

``` c
1      if (crt_pos >= CRT_SIZE) {
2              int i;
3              memmove(crt_buf, crt_buf + CRT_COLS, (CRT_SIZE - CRT_COLS) * sizeof(uint16_t));
4              for (i = CRT_SIZE - CRT_COLS; i < CRT_SIZE; i++)
5                      crt_buf[i] = 0x0700 | ' ';
6              crt_pos -= CRT_COLS;
7      }
```

当打印出来的字符超过了屏幕字符buffer的容量的时候，会把屏幕整体向上平移一行，最上面一行消失，空出来的最底下一行变成背景色。


**3Q: Trace the execution of the following code step-by-step:**

int x = 1, y = 3, z = 4;
cprintf("x %d, y %x, z %d\n", x, y, z);

呃呃呃，至于怎么跑这些代码，我个人的做法是，找到kern/init.c文件，这个文件里的void i386_init(void)是一定会被执行的，而且是在kernel已经加载完成之后，所以可以run一些代码了，lab1的make grade就是在这个文件里调用一些函数，输出一下看看你有没有写对。

在里面加了一个函数，里面写上想跑的代码，然后再gdb断点。

**3Q_1: In the call to cprintf(), to what does fmt point? To what does ap point?**

fmt 指向"x %d, y %x, z %d\n"
ap 指向后面的一堆变量, x, y, z

**3Q_2: List (in order of execution) each call to cons_putc, va_arg, and vcprintf. For cons_putc, list its argument as well. For va_arg, list what ap points to before and after the call. For vcprintf list the values of its two arguments.**

ps: 可以通过watch ap来监视ap的变化情况，这也算一个断点，当执行到ap值改变的时候，gdb的continue语句也会停下来。

vcprintf (fmt=0xf0101820 "x %d, y %x, z %d\n", ap=0xf010ffc4 "\001")
cons_putc (120)
cons_putc (32)
va_arg():
  Old value = (va_list) 0xf010ffc4 "\001"
  New value = (va_list) 0xf010ffc8 "\003"
cons_putc (49)
cons_putc (44)
cons_putc (32)
cons_putc (121)
cons_putc (32)
  Old value = (va_list) 0xf010ffc8 "\003"
  New value = (va_list) 0xf010ffcc "\004"
cons_putc (51)
cons_putc (44)
cons_putc (32)
cons_putc (122)
cons_putc (32)
  Old value = (va_list) 0xf010ffcc "\004"
  New value = (va_list) 0xf010ffd0 "\224"
cons_putc (52)
cons_putc (10)


**4Q: Run the following code.**

    unsigned int i = 0x00646c72;
    cprintf("H%x Wo%s", 57616, &i);

**What is the output?**

He110 World

**5Q: In the following code, what is going to be printed after 'y='? (note: the answer is not a specific value.) Why does this happen?**

    cprintf("x=%d y=%d", 3);

应该是ap list越界后的值，尝试过，打印出来一个0

**6Q: Let's say that GCC changed its calling convention so that it pushed arguments on the stack in declaration order, so that the last argument is pushed last. How would you have to change cprintf or its interface so that it would still be possible to pass it a variable number of arguments?**

没有想清楚

## Exercise 9

**Determine where the kernel initializes its stack, and exactly where in memory its stack is located. How does the kernel reserve space for its stack? And at which "end" of this reserved area is the stack pointer initialized to point to?**

- 在entry.S里:
``` asm
	ovl	$0x0,%ebp			# nuke frame pointer
	ovl	$(bootstacktop),%esp
```
- 0xf0110000 esp
- 在enrty.S的最下面的.data段，利用.space语句（这个语句专门用来开空间的）开辟了一段大小为KSTKSIZE的空间，用做Kernel自己的stack空间。
KSTKSIZE的大小在<inc/memlayout.h>里有定义，是8个page的大小。
- 在顶部，栈是自顶向下增大的

## Exercise 10

**How many 32-bit words does each recursive nesting level of test_backtrace push on the stack, and what are those words?**

逐条解释一下汇编语句干了什么。总共push了5次，开辟了8个32-bit words大小的空间。

```asm
f0100040:	55                   	push   %ebp //为了开辟新的栈区域，所以把原来的ebp指针push进栈内保存起来。
f0100041:	89 e5                	mov    %esp,%ebp //开辟新的栈区域
f0100043:	53                   	push   %ebx // 因为调用cprintf的之前要用到ebx寄存器作为中介寄存器把参数x从栈里取出来，所以这里要先把ebx入栈存起来
f0100044:	83 ec 0c             	sub    $0xc,%esp // 把栈扩大3个32-bit。用来存放局部变量，虽然我也不知道有什么局部变量
f0100047:	8b 5d 08             	mov    0x8(%ebp),%ebx //0x8(%ebp)存放的其实就是传进test_backtrace里的x，存进ebx寄存器
	cprintf("entering test_backtrace %d\n", x);
f010004a:	53                   	push   %ebx //把ebx寄存器里的x再存到栈里
f010004b:	68 c0 1a 10 f0       	push   $0xf0101ac0 //把需要打印的字符串地址也存进栈里
f0100050:	e8 77 0a 00 00       	call   f0100acc <cprintf> //调用cprintf函数
	if (x > 0)
f0100055:	83 c4 10             	add    $0x10,%esp // 释放栈空间
f0100058:	85 db                	test   %ebx,%ebx
f010005a:	7e 11                	jle    f010006d <test_backtrace+0x2d>
	test_backtrace(x-1);
f010005c:	83 ec 0c             	sub    $0xc,%esp //开辟栈空间
f010005f:	8d 43 ff             	lea    -0x1(%ebx),%eax // x-1
f0100062:	50                   	push   %eax // 把x-1压进栈里，循环调用下一个test_backtrace
f0100063:	e8 d8 ff ff ff       	call   f0100040 <test_backtrace>
f0100068:	83 c4 10             	add    $0x10,%esp
f010006b:	eb 11                	jmp    f010007e <test_backtrace+0x3e>
```

## Excercise 11

要求：修改mon_backtrace，打印出如下信息：

- ebp

- eip

- 5个args


  实现的核心，我认为要就是要搞清楚以下几个问题：

**1Q. eip存在哪里了？**

其实复习一下就知道，call这条指令相当于以下三步：

1. push EIP
2. jmp

所以就是说，eip就存在ebp位置的上面一个word。

知道了这点之后，参考read_ebp()里的asm，直接可以调用

```c
uint32_t ebp, eip;
asm volatile("movl %%ebp,%0" : "=r" (ebp));
```

这是把寄存器里的值调出来的一种方式。

然后比较重要的一步就是强转指针，这也是lab要求里强调的

```c
uint32_t * ptr = (uint32_t*)ebp;
```

然后就可以把ebp的数值当作指针来使用，传入的参数在eip的上面，调用cprintf打印出来即可。

**2Q. 上一个函数的ebp存在哪里了？**

我们会发现每次进入一个函数的时候，为了开辟新的栈空间，都会常规操作一下：

```asm
push %ebp
mov %esp, %ebp
```

也就是说，上一个函数的ebp就被存在当前ebp指向的那个位置里，所以每次只要把ebp里的值取出来再赋给ebp就行了。

**3Q.什么时候停止递归追踪？**

我们发现最开始的时候ebp是0，所以递归到ebp是0的时候就行。

代码如下：

```c
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
		ebp = *(ptr);
		ptr = (uint32_t*)ebp;
	}
	return 0;
}
```



## Exercise 12

要求：打印出函数更详细的信息：

- 这个语句所在的文件
- 这个语句在整个文件的哪行
- 这个语句在哪个函数里
- 这个语句在函数的哪行

stabs，symbol table strings，用来存放debug的信息，主要是字符串信息，也就是符号名字。可以把stabs想象成一个很长的数组，文件、函数、函数内的行号等信息都按照代码的逻辑顺序存放在stabs里，例如，假如一个函数在某个文件里，那么这个函数对应的stabs项也会在文件对应的stabs项之后的某个特定位置。

观察了一下debuginfo_eip，知道查找的时候需要比对stabs项的类型，查看stabs.h文件里定义的宏，知道这种type叫做N_SLINE，也就是0x44。之前的代码已经帮忙定位到了哪个文件，哪个函数了，所以只要在这个区间里查找满足是N_SLINE类型的， 且地址等于eip的stabs项就行了。

了解stabs结构体里包含的东西也比较重要。地址一般存在n_value里，行号一般存在n_desc里。

没有用二分查找实现，简单的遍历，因为感觉已经定位到函数了，所以查找的规模也不会很大。

代码如下：

```c
int findit = -1;
for(int ii=lline; ii <= rline; ii++)
{
    if(stabs[ii].n_type == 0x44)
    {
        if(stabs[ii].n_value <= addr && stabs[ii+1].n_value > addr)
        {
            info->eip_line = stabs[ii].n_desc;
            findit = 0;
        }
    }
}

if(findit == -1)
{
    return -1;
}
```

修改mon_backtrace如下：

```c
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
```

然后还要把backtrace加入到命令行函数里，其实原本的代码已经封装的很好了，只要在kern/monitor.c的Commands数组里加入backtrace这个选项，以及它对应的调用函数即可。

```c
static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "trace back through stack to find debugging infomation", mon_backtrace}
};
```



## Challenge

彩色界面！

上微机原理课的时候知道，传一个字符给打印端口的时候，低8位是字符，高8位就是对应的颜色。

找来找去，发现在kern/console.c里的cga_putc函数里的第一行就进行了如下设置：

```c
// if no attribute given, then use black on white
if (!(c & ~0xFF))
    c |= 0x700;
```

也就是说，只要提前把字符的颜色设置好，那么cga就会打印出这个颜色。

标准的方式是使用ANSI escape code来解析颜色，但是为了简单，所以我采用了一种很简单的方式，实现4种颜色的打印：

1. 在printfmt.c里修改printfmt，定义@（就和平时的%类似），作为转意字符
2. @后面可以跟R，G，B，Y四个字符，表示能打印红，绿，蓝，黄四种颜色
3. 解析到@之后的颜色，然后把这次需要打印的字符的高8位都OR上对应的颜色。

主要的实现代码如下：

```c
while (1) {
    while ((ch = *(unsigned char *) fmt++) != '%' && ch != '@' ) {
        if (ch == '\0')
            return;
        putch(ch | color_mode, putdat);
    }
    
    if(ch == '@'){
        switch (ch = *(unsigned char *) fmt++){
            case 'R': color_mode = 4 << 8;break;
            case 'G': color_mode = 2 << 8;break;
            case 'B': color_mode = 9 << 8;break;
            case 'Y': color_mode = 6 << 8;break;
            default:
                putch('@' | color_mode, putdat);
                for (fmt--; fmt[-1] != '@'; fmt--)
                    /* do nothing */;
                break;
        }
        continue;
    }
```

当然还有一些小修改，比如把字符OR上颜色之类的，这里就不多赘述。

使用例子如下：

```c
cprintf("@Rerror"); //打印红色的"error"
cprintf("@Gpass!"); //打印绿色的"pass!"
```



**彩色界面还支持命令行模式！**

输入：

```bash
ansi --[color] [text]
```

就可以以一种颜色打印出想要的文本

例如想用红色打印"error"，就可以输入：

```bash
ansi --red error
```

目前支持输入red, blue, yellow, green。欢迎尝试。
