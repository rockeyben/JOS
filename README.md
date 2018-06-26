# MIT 6.828 LAB6

## Introduction

这次操统实习课的project选择lab6，主要是为操作系统配置网络功能，在这个lab中，我们需要为JOS设置网卡的驱动，并完善一些基本的功能，比如传送、接收包。

## Configuration

本次的lab需要用课程专门的qemu版本，否则很多功能都实现不了，配置方法如下：

```shell
> git clone https://github.com/geofft/qemu.git qemu-1.7.0 -b 6.828-1.7.0 --depth=1 && cd qemu-1.7.0
> ./configure --prefix=/home/oslab/qemu-1.7.0/dist --target-list="i386-softmmu"
> make && make install
> vim ../mit-jos/conf/env.mk
```

在开始写代码之前，我们需要对qemu里的网络配置有大体的了解，我做了一个总结，如下：

## Qemu's virtual network

我们会使用qemu的user mode network stack，因为不需要任何权限。我们更新了makefile让qemu支持user mode network stack和虚拟的**e1000网卡**。

默认的，qemu提供了一个IP为10.0.2.2的虚拟路由器，并会为JOS分配一个10.0.2.15的IP。在`net/ns.h`里，我们已经做好了设置。

虽然qemu的虚拟网络允许JOS对外界的internet连接，但是JOS的10.0.2.15的IP地址对于qemu外的网络来说没有意义（qemu相当于一个NAT）。所以我们不能直接连接JOS内部的服务器，即使是从运行qemu的主机也不行。为了解决这个问题，我们直接在配置里设置好了，qemu会在主机上运行一个服务器，这个服务器的某个端口和JOS的某个端口是已经连接好多的，这个服务器就负责在虚拟网络和真实主机之间传送数据。

### packet inspection

makefile也配置了qemu的network stack，让它能记录所有接收和发送的packets，并存在`qemu.pcap`里。为了解析截获的packet，我们可以使用`tcpdump`:

```
tcpdump -XXnr qemu.pcp
```

或者也可以使用`Wireshark`来图形化地查看pcap file。

### Debugging the E1000

我们应该为能使用仿真硬件而感到幸运。因为E1000是运行在软件下的，所以E1000可以向我们汇报所有的问题。通常来说，一个跟金属打交道的驱动开发员是享受不到这种奢侈的。

e1000提供了很多debug 输出：

| Flag      | Meaning                                             |
| --------- | --------------------------------------------------- |
| tx        | Log packet transmit operations                      |
| txerr     | Log transmit ring errors                            |
| rx        | Log changes to RCTL                                 |
| rxfilter  | Log filtering of incoming packets                   |
| rxerr     | Log receive ring errors                             |
| unknown   | Log reads and writes of unknown registers           |
| eeprom    | Log reads from the EEPROM                           |
| interrupt | Log interrupts and changes to interrupt  registers. |

```

```

```
make E1000_DEBUG=tx,txerr ....
```

e1000是在`hw/e1000.c`里实现的。

## The Network Server

从0开始写network stack是非常难的。所以我们适用了`1wIP`，这是一个开源的轻量级TCP/IP protocol suite。在这个lab里，1wIP可以被当作一个实现了BSD socket接口的，有输入输出端口的黑盒。

一个network server是由以下4个环境组成的：

- 核心network server environment，包括了socket call dispatcher 和1wIP。
- 输入环境
- 输出环境
- 计时器环境

![img](file:///home/rockeyben/OS/ns.png?lastModify=1529916957)

这个lab里我们会着重完成绿色的部分。

### Core Network Server Environment

核心网络栈由socket call dispatcher和1wIP组成。

socket call dispatcher就像一个file server一样。user environment使用`stubs`(`lib/nsipc.c`)向核心网络环境传输IPC消息。如果你看一下`lib/nsipc.c`，你会看到我们用和找到file server一样的方法找到了core network env：`i386_init` 用NS_TYPE_NS type创建了NS环境，所以我们扫描envs数组，找到那个特殊的env type。对于每一个user env IPC，NS里的dispatcher代表用户会调用1wIP提供的合适的BSD socket接口。

普通的用户进程不会直接调用`nsipc_*`这样的函数。他们会调用`lib/sockets.c`里的API，这些API把sockets抽象成file descripter，所以用户就像访问on-disk file一样访问那些sockets。像`connect` `accept`这样的操作是sockets特有的，但是像`read`,`write`,`close`这样的操作会进入`lib/fd.c`里的普通的file descripter 流程。就像file server会为每个openfile维护一个file id一样，1wIP也会为每一个open socket产生一个unique ID。在file server & network server里，我们都通过`struct Fd`里存储的信息来完成fd 到 file id的映射关系。

虽然看起来NS和FS很像很像，但是有一个关键的不同。BSD socket calls（如accept和recv）可能会无限期阻塞。如果dispatcher让1wIP执行了一个这样的阻塞调用，那么dispatcher也会被阻塞，换句话说就是整个系统在某一个时刻只能有一个network call。这是不可接受的。所以NS使用了user-level thread来防止阻塞整个server env。对于每一个到来的IPC，dispatcher会创建一个新的线程，并在线程里处理请求。如果这个thread被阻塞了，那么只有这个thread陷入睡眠，别的thread还是可以继续运行的。

### Output Environment

当服务socket call的时候，1wIP会产生包，让net card传输。1wIP会用NSREQ_OU功能TPUT IPC把包送给output env。output env 的工作就是接收这些包然后通过系统调用把这些包送给设备驱动程序。

### Input Environmnet

network card收到的包需要被送入1wIP。对于每一个被设备驱动收到的包，input env会把它从kernel space里取出来（也是利用之后我们自己实现的系统调用）然后把这些包送给core server env。通过NSREQ_INPUT IPC。

之所以把包输入的功能从core net env里分离出来，是因为JOS使得同时接收很多IPC messages 然后选择这个操作很困难。我们没有一个`select`系统调用，能够使JOS能检查所有的输入资源，并辨识哪一个是准备好被处理的。

### Timer Environment

Timer env 周期性地向core net env 发送NSREQ_TIMER IPC，通知它已经超时了。

## Network Interface Card

```
Exercise 1. Add a call to time_tick for every clock interrupt in kern/trap.c. Implement sys_time_msec and add it to syscall in kern/syscall.c so that user space has access to the time. 
```

 感觉设计者是从最简单也是最基本的开始让我们做，先为Timer Environment设置时钟。我们需要在`kern/trap.c`里修改时钟中断，每次时钟中断的时候都调用一下`time_tick`函数。我们可以在`kern/trap.c`里看到这个函数：

```c
static unsigned int ticks;

void
time_init(void)
{
	ticks = 0;
}

// This should be called once per timer interrupt.  A timer interrupt
// fires every 10 ms.
void
time_tick(void)
{
	ticks++;
	if (ticks * 10 < ticks)
		panic("time_tick: time overflowed");
}

unsigned int
time_msec(void)
{
	return ticks * 10;
}
```

kernel维护了一个全局“时钟”`ticks`。每次调用`time_tick`，都使这个时钟加1，直到溢出。由于这个函数是在内核中的，所以在trap.c里可以直接调用：

```c
		case 32: 
			time_tick();
			lapic_eoi();
			sched_yield(); 
			return ;
```

那么我们还需要在用户态实现一个时钟接口，使得我们能够访问kernel中的时钟变量。大体的思路就是添加一个系统调用，然后在这个系统调用中调用`kern/time.c`里的`time_msec`，会返回当前时间。

```c
static int
sys_time_msec(void)
{
	// LAB 6: Your code here.
	return time_msec();
}
```

## PCI Interface

### Initialization

PCI code会遍历PCI 总线，寻找设备。当它找到一个设备的时候，就会读取设备的vendor ID和device ID，用这两个值作为key取寻找pci_attach_vendor array。这个数组的结构是这样的：

```c
struct pci_driver {
    uint32_t key1, key2;
    int (*attachfn) (struct pci_func *pcif);
};
```

当它找到了匹配的项的时候，就会调用这个项的`attachfn`来执行设备初始化。

JOS里我们这样来表示一个PCI func:

```c
struct pci_func {
    struct pci_bus *bus;

    uint32_t dev;
    uint32_t func;

    uint32_t dev_id;
    uint32_t dev_class;

    uint32_t reg_base[6];
    uint32_t reg_size[6];
    uint8_t irq_line;
};
```

最后的3项是我们比较关心的。因为它们记录了negotiated memory, I/O, interrupt resource。

reg_base记录了memory-mapped IO regions或base IO ports for IO port resources的基址。reg_size记录了mmio的大小，或IO port的个数。irq_line记录了分配给设备的中断线。

当attach function被调用的时候，就代表设备被找到了，但是并不代表设备被激活了。这意味这PCI还没有决定好该给设备分配哪些资源，比如地址空间，IRQ线等等，因此pci_func的最后3项还是空的。attach function还要调用`pci_func_enable`，这将会激活设备，协调好这些资源，然后填充`struct pci_func`。

```
Exercise 3. Implement an attach function to initialize the E1000. Add an entry to the pci_attach_vendor array in kern/pci.c to trigger your function if a matching PCI device is found (be sure to put it before the {0, 0, 0} entry that mark the end of the table). You can find the vendor ID and device ID of the 82540EM that QEMU emulates in section 5.2. You should also see these listed when JOS scans the PCI bus while booting.

For now, just enable the E1000 device via pci_func_enable. We'll add more initialization throughout the lab.

We have provided the kern/e1000.c and kern/e1000.h files for you so that you do not need to mess with the build system. They are currently blank; you need to fill them in for this exercise. You may also need to include the e1000.h file in other places in the kernel.

When you boot your kernel, you should see it print that the PCI function of the E1000 card was enabled. Your code should now pass the pci attach test of make grade. 
```

在这里我们做一个简单的设置，就是把e1000的设备信息添加到`pci_attach_vendor`数组里，这样操作系统就可以找到我们的网卡了，添加的3维元组的意思是{厂家id，设备id，激活函数}。查一下intel的手册就可以查到。而激活函数是需要我们自己写的，特定的设备肯定有自己的特定的激活函数，对于e1000来说，我们就要在`e1000.c`里写好这个函数（我们自定义的，取别的名字也可以）`e1000_attach`。

```c
// pci_attach_vendor matches the vendor ID and device ID of a PCI device. key1
// and key2 should be the vendor ID and device ID respectively
struct pci_driver pci_attach_vendor[] = {
	{ 0x8086, 0x100e, &e1000_attach},
	{ 0, 0, 0 },
};
```

之后就是完善这个`e1000_attach` 了，在这个部分我们只需要完成最基本的一步：调用`pci_func_enable`来填充pci_func结构体。

```c
int e1000_attach(struct pci_func * pcif)
{
    pci_func_enable(pcif);
    return 0;
}
```

## Memory-mapped IO

我们已经在lab5中了解了，JOS把IO映射到一个专门的地址空间内。网络也是一种IO，所以我们需要把e1000的部分寄存器映射到地址空间中，而这段空间是操作系统和网卡协商得到的，会在初始化的时候存入`pci_func`里，首地址存在`reg_base[0]`，大小存在`reg_size[0]`。

```
Exercise 4. In your attach function, create a virtual memory mapping for the E1000's BAR 0 by calling mmio_map_region (which you wrote in lab 4 to support memory-mapping the LAPIC).

You'll want to record the location of this mapping in a variable so you can later access the registers you just mapped. Take a look at the lapic variable in kern/lapic.c for an example of one way to do this. If you do use a pointer to the device register mapping, be sure to declare it volatile; otherwise, the compiler is allowed to cache values and reorder accesses to this memory.

To test your mapping, try printing out the device status register (section 13.4.2). This is a 4 byte register that starts at byte 8 of the register space. You should get 0x80080783, which indicates a full duplex link is up at 1000 MB/s, among other things.
```



```c
volatile uint32_t * e1000;

int e1000_attach(struct pci_func * pcif)
{
    pci_func_enable(pcif);
 	e1000 =  mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    assert(e1000[E1000_STATUS / 4] == 0x80080783);
    return 0;
}
```

我们在`e1000.h`里定义了一个数组型变量e1000，把mmio_map之后返回的地址赋值给它，这样就使得这个数组指向了那块映射区域，因为我们后续要对这个区域做很多设置，所以需要这样一个变量来方便操作。最后我们可以检查一个“状态位”寄存器的值是否为0x80080783，来判断我们的设置是否正确。

这里我们要在`e1000.h`里定义很多宏，可以参考给的代码，但其实亲测这个代码里很多都是用不到的，而且还缺了很多！需要根据后续的要求在intel手册里找各种偏移值啊什么的。不过它给了我们大致的印象，以及它也给了关键的传输队列描述符定义：

```c
/* Transmit Descriptor */
struct e1000_tx_desc {
    uint64_t buffer_addr;       /* Address of the descriptor's data buffer */
    union {
        uint32_t data;
        struct {
            uint16_t length;    /* Data buffer length */
            uint8_t cso;        /* Checksum offset */
            uint8_t cmd;        /* Descriptor control */
        } flags;
    } lower;
    union {
        uint32_t data;
        struct {
            uint8_t status;     /* Descriptor status */
            uint8_t css;        /* Checksum start */
            uint16_t special;
        } fields;
    } upper;
};

/* Receive Descriptor */
struct e1000_rx_desc {
    uint64_t buffer_addr; /* Address of the descriptor's data buffer */
    uint16_t length;     /* Length of data DMAed into data buffer */
    uint16_t csum;       /* Packet checksum */
    uint8_t status;      /* Descriptor status */
    uint8_t errors;      /* Descriptor Errors */
    uint16_t special;
};
```

这三个结构体是我们必须会用到的，这里mark一下。

## DMA

对DMA我们已经非常熟悉了，cpu告诉设备io任务，然后设备自动和内存交互。这里我们可以接触到DMA底层的细节。

对于驱动器来说，它需要为传输队列和接收队列分配空间，设置DMA描述符，然后告诉e1000这些队列的位置。但是之后的事情都是异步的。假如我们要传输一个包，我们会把它拷贝到下一个DMA描述符中，之后就交给DMA去做。这些队列都是环形的，意味着我们需要处理好“尾指针”。还需要注意的是，队列中对应的地址都是物理地址，因为DMA是直接和物理内存打交道的，不通过mmu，所以在设置DMA描述符的时候都要注意。

### Transmitting Packets

由于传输队列和接收队列是相互独立的，所以我们可以先单独实现传输队列。

因为DMA描述符是采用了C struct，所以我们可以像访问结构体一样来访问某些控制位：

```
63            48 47   40 39   32 31   24 23   16 15             0
  +---------------------------------------------------------------+
  |                         Buffer address                        |
  +---------------+-------+-------+-------+-------+---------------+
  |    Special    |  CSS  | Status|  Cmd  |  CSO  |    Length     |
  +---------------+-------+-------+-------+-------+---------------+
```

接下来我们根据intel手册section 3.3的内容来总结一下transmit descriptor中一些重要的内容：

```c
struct tx_desc
{
	uint64_t addr; // 传输地址（物理）
	uint16_t length; //传输长度
	uint8_t cso; // checksum offset，表示在包的哪里是checksum
	uint8_t cmd; // comand line field，一系列控制位，其中后续会用到的是RS位，如果这个位被设置，那么ethernet就被要求汇报状态信息。
	uint8_t status; // status field，一系列状态信息，比如TU(Transmission underun)，LC(Late Collision)，EC(Excess Collisions)，还有后续会用到的一个常用的DD(Descriptor Done)，这个bit代表传输已经结束，而且数据已经被写回，这就意味着该描述符是空闲的了，可以被使用。但是如果要访问这个位，那么首先要设置cmd里的RS位，这样才会汇报信息。
	uint8_t css; // checksum start field，表示从哪里开始计算checksum
	uint16_t special;
};
```

```
Exercise 5. Perform the initialization steps described in section 14.5 (but not its subsections). Use section 13 as a reference for the registers the initialization process refers to and sections 3.3.3 and 3.4 for reference to the transmit descriptors and transmit descriptor array.

Be mindful of the alignment requirements on the transmit descriptor array and the restrictions on length of this array. Since TDLEN must be 128-byte aligned and each transmit descriptor is 16 bytes, your transmit descriptor array will need some multiple of 8 transmit descriptors. However, don't use more than 64 descriptors or our tests won't be able to test transmit ring overflow.

For the TCTL.COLD, you can assume full-duplex operation. For TIPG, refer to the default values described in table 13-77 of section 13.4.34 for the IEEE 802.3 standard IPG (don't use the values in the table in section 14.5).
```

根据以上的信息，我们已经可以初始化队列了，根据secion 14.5描述的步骤：

- 为DMA描述符分配空间
- 将TDLEN寄存器设置成描述符数组的长度
- 初始化队列头和尾（TDH/TDT）为0。
- 设置控制寄存器TCTL：
  - TCTL.EN：enable
  - TCTL.PSP: Pad Short Packets
  - TCTL.CT: Collision Threshold, 默认是0x10
  - TCTL.COLD: Collision Distance，默认是0x40
- IPG位的设置，10, 6, 8

```c
struct e1000_tx_desc tx_desc_buf[E1000_TX_LEN] __attribute__ ((aligned(PGSIZE)));
struct e1000_data tx_data_buf[E1000_TX_LEN] __attribute__ ((aligned (PGSIZE)));

struct e1000_rx_desc rx_desc_buf[E1000_RX_LEN] __attribute__ ((aligned (PGSIZE)));
struct e1000_data rx_data_buf[E1000_RX_LEN] __attribute__ ((aligned (PGSIZE)));
```

首先设置这样的数组，`e1000_data`是我定义的，用来存放数据的结构体：

```c
struct e1000_data {
    uint8_t data[DATA_SIZE]; // DATA_SIZE 2048
};
```

然后长度的设置是这样的：

```c
#define E1000_TX_LEN   64
#define E1000_RX_LEN   128
```

接着就是初始化了：

```c
void e1000_init()
{
    // transmitting pkt
    e1000[E1000_TDBAL / 4] = PADDR(tx_desc_buf);
    e1000[E1000_TDBAH / 4] = 0x0;
    e1000[E1000_TDH / 4] = 0x0;
    e1000[E1000_TDT / 4] = 0x0;
    e1000[E1000_TDLEN / 4] = E1000_TX_LEN * sizeof(struct e1000_tx_desc);
    e1000[E1000_TCTL / 4] = vmask(1, E1000_TCTL_EN) |
                                vmask(1, E1000_TCTL_PSP) |
                                vmask(0x10, E1000_TCTL_CT) |
                                vmask(0x40, E1000_TCTL_COLD);
    e1000[E1000_TIPG / 4] = vmask(10, E1000_TIPG_IPGT) |
                                vmask(6, E1000_TIPG_IGPR1) |
                                vmask(8, E1000_TIPG_IGPR2);
```

这里的vmask是我定义的一个宏，方便将某个值设置到特定的位上。

```c
#define vmask(v, mask) v * ((mask) & ~((mask) << 1))
```

在完成这个exercise之后，我们运行`make E1000_DEBUG=TXERR,TX qemu`，能看到`e1000: tx disabled`，这个信息（必须使用课程指定的qemu）。

这些设置做好了之后，就意味着我们可以开始传包了，传包函数要做的事情主要就是：

- 检查是否有空闲描述符
- 有，就把数据拷贝进去
- 设置描述符的相关信息
- 更新描述符队列信息（头、尾）

按照这个逻辑，我们可以构想出这样一个传包函数：

```c
int e1000_trans_pkt(uint8_t * addr, size_t size)
{
    uint32_t tail = e1000[E1000_TDT / 4];
    struct e1000_tx_desc * tx_tail = &tx_desc_buf[tail];
	
    // 检查DD位，是否Done了，表示当前描述符是否空闲，如果不空闲（其实就意味着队列已经满了，因为我们用的是循环队列），那么就返回错误值。
    if (tx_tail->upper.fields.status != E1000_TXD_STAT_DD)
        return -1;
    
    if (size > DATA_SIZE)
        size = DATA_SIZE;
    
    memmove(&tx_data_buf[tail], addr, size); // 把数据拷贝进下一个描述符
    
    tx_tail->lower.flags.length = size;
    tx_tail->upper.fields.status = 0;
    tx_tail->lower.data |= E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP; // 要求报告信息，并且需要设置EOP，代表当前就是数据包的结尾，否则会传不出去包，亲测......

    e1000[E1000_TDT / 4] = (tail + 1) % E1000_TX_LEN; // 更新队尾指针
    return 0;
}
```

接着运行`make E1000_DEBUG=TXERR,TX qemu`，能看到：

```
e1000: index 0: 0x271f00 : 9000002a 0
...
```

这样的信息，而且看到了之后还必须要能在抓包日志`qemu.pcap`中读取到记录，我之前如果没有设EOP位，整个传输是可以完成的，但是qemu.pcap里就是空的。

```
Exercise 7. Add a system call that lets you transmit packets from user space. The exact interface is up to you. Don't forget to check any pointers passed to the kernel from user space.
```

接着就是实现系统调用了，给用户态接口，这个比较简单，报告里就不赘述了。

### Transmitting Packets: Network Server

实现系统调用之后，我们就可以在用户态的Network Server Environment做一些事情了：它本身会进入一个循环，不停地从core network server接受`NSREQ_OUTPUT`消息，如果收到的话，就使用之前写的传输函数，把request请求的数据给设备发过去。而这种IPC是在`net/lwip/jos/jif/jif.c`里的`low_level_outpit`发送的，将会把1wIP的栈给粘在JOS系统上。每一个IPC都包括一个`Nsipc`结构体，这个结构体里还有一个重要的结构体`jif_pkt`：

```c
struct jif_pkt {
    int jp_len;
    char jp_data[0];
};
```

接着我们就要在`net/output.c`里实现这样一个循环，处理请求，发送数据：

```c
	int32_t req;
	while(1){
		req = ipc_recv(0, &nsipcbuf, 0);
		if (req == NSREQ_OUTPUT){
            // sys_send_pkt就是我写的系统调用的名字
			while(sys_send_pkt(nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len) < 0){
				sys_yield();
			}
		}
	}
```

## Receiving Packets

接受队列和传送队列很像，除了它包含正在等待pkt的空buffer。因此，当网络在闲置状态的时候，传输队列是“空”的，而接收队列是“满”的。（注意空和满的概念）

当E1000收到一个pkt的时候，它首先检查这个包是否满足本卡的一些条件（比如这个包是不是送到E1000的MAC地址），如果没有满足任何条件，就会无视这个包。如果通过了检查，E1000会尝试从接收队列的头那里请求一个新的描述符。如果头（RDH）遇到了尾（RDT），那么接收队列就满了，然后网卡就会丢包。如果有一个空闲的接收描述符，那么就把这个包的数据copy进去，设置DD和EOP位，然后增加RDH。

如果E1000受到了一个大于pkt buffer的包，那么它会请求足够多的descriptor来装这个包。为了表明这件事发生了，它会把所有申请的desc设置DD位，但是只在最后的那个desc上设置EOP位。我们可以在driver中处理好这件事，同时也可以简单得设置我们的网卡“不接受长包”。

```
Exercise 10. Set up the receive queue and configure the E1000 by following the process in section 14.4. You don't have to support "long packets" or multicast. For now, don't configure the card to use interrupts; you can change that later if you decide to use receive interrupts. Also, configure the E1000 to strip the Ethernet CRC, since the grade script expects it to be stripped.

By default, the card will filter out all packets. You have to configure the Receive Address Registers (RAL and RAH) with the card's own MAC address in order to accept packets addressed to that card. You can simply hard-code QEMU's default MAC address of 52:54:00:12:34:56 (we already hard-code this in lwIP, so doing it here too doesn't make things any worse). Be very careful with the byte order; MAC addresses are written from lowest-order byte to highest-order byte, so 52:54:00:12 are the low-order 32 bits of the MAC address and 34:56 are the high-order 16 bits.

The E1000 only supports a specific set of receive buffer sizes (given in the description of RCTL.BSIZE in 13.4.22). If you make your receive packet buffers large enough and disable long packets, you won't have to worry about packets spanning multiple receive buffers. Also, remember that, just like for transmit, the receive queue and the packet buffers must be contiguous in physical memory.

You should use at least 128 receive descriptors
```

接下来就要配置接收队列了：

```c
    // receiving pkt
    uint32_t ral = (eerd(EEPROM_MAC_ADDR2) << 16) | eerd(EEPROM_MAC_ADDR1);
    uint32_t rah = eerd(EEPROM_MAC_ADDR3);
    e1000[E1000_RAL / 4] = ral;
    e1000[E1000_RAH / 4] = rah | E1000_RAH_AV;
    // assert(ral == 0x12005452);
    // assert(rah == 0x5634);
    e1000[E1000_MTA / 4] = 0;
    e1000[E1000_IMS / 4] = 0; 
    e1000[E1000_RDBAL / 4] = PADDR(rx_desc_buf);
    e1000[E1000_RDBAH / 4] = 0x0;
    e1000[E1000_RDH / 4] = 0x1;
    e1000[E1000_RDT / 4] = 0x0;
    e1000[E1000_RDLEN / 4] = E1000_RX_LEN * sizeof(struct e1000_rx_desc);
    e1000[E1000_RCTL / 4] = E1000_RCTL_EN |
                                E1000_RCTL_SZ_2048 |
                                E1000_RCTL_SECRC;
```

在两个assert之后的部分和传输队列比较相似，但是之前有一个设置MAC地址的部分，需要注意，这里其实可以直接把assert里的值设置进去（exercise里已经说明了可以直接设，因为qemu的MAC地址已经固定了）。但是这里采用了另一种做法，我们可以看`eerd`这个函数：

```c
static uint16_t
eerd(uint8_t addr) {
    uint32_t word = addr;
    e1000[E1000_EERD / 4]  = (word << E1000_EEPROM_RW_ADDR_SHIFT) | E1000_EEPROM_RW_REG_START;
    while (!(e1000[E1000_EERD / 4] & E1000_EEPROM_RW_REG_DONE));
	return e1000[E1000_EERD / 4] >> E1000_EEPROM_RW_REG_DATA_SHIFT;
}
```

实际上我们可以通过读取E1000_EERD这个寄存器的值来读取MAC地址，具体的操作就是上面的了。

之后我们就可以仿照传输队列那样，写系统调用，然后完成`net/input.c`：

```
Exercise 11. Write a function to receive a packet from the E1000 and expose it to user space by adding a system call. Make sure you handle the receive queue being empty.
```

```
Exercise 12. Implement net/input.c. 
```

```c
	uint32_t req = 0;
	while(1){
		while(sys_page_alloc(0, &nsipcbuf, PTE_U|PTE_P|PTE_W) < 0);	
		while(1){
            // sys_recv_pkt是系统调用的名字
			nsipcbuf.pkt.jp_len = sys_recv_pkt(nsipcbuf.pkt.jp_data);
			if(nsipcbuf.pkt.jp_len < 0)
				continue;
			sys_yield();
			break;
		}
		while(sys_ipc_try_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U|PTE_W|PTE_P) < 0);
	}
```

按照要求，我们需要接收数据包，然后把数据发送到ns环境中。

## Web Server

```
Exercise 13. The web server is missing the code that deals with sending the contents of a file back to the client. Finish the web server by implementing send_file and send_data. 
```

web server就是更加上层的封装了，可以看到`user/httpd.c`这个文件，然后需要我们补全`send_file`和`send_data`这两个函数，这两个函数的关系很容易看出来，那就是`send_file`会进行一系列文件操作，然后再调用`send_data`，所以我们需要先实现`send_data`：

```c
static int
send_data(struct http_request *req, int fd)
{
	// LAB 6: Your code here.
	char * buf;
	int r;
	struct Stat stat;

	if((r = fstat(fd, &stat)) < 0){
		die("fstat failed");
	}

	if(stat.st_size > 1518){
		die("packet size is too large, max 1518 bytes");
	}

	buf = malloc(stat.st_size);

	if((r = readn(fd, buf, stat.st_size)) != stat.st_size){
		die("Haven't finish reading entire data");
	}

	if((r = write(req->sock, buf, stat.st_size)) != stat.st_size){
		die("Haven't finish sending back entire data");
	}

	free(buf);
	return 0;
}
```

在这里我们可以体会到套接字socket的思想，当我们建立连接了之后，在用户看来，对网络的传输和接受数据就像对一个文件描述符进行操作一样，我们只需要按照套接字建立描述符，然后对描述符进行IO操作就可以了。所以这里只需要分配一段内存中转空间，从磁盘中把数据读出来，然后写入`req->sock`这个抽象出来的网络套接字就可以了。

而`send_file`之前还要做的更多的是一些sanity check，比如文件是否存在：

```c

static int
send_file(struct http_request *req)
{
	int r;
	off_t file_size = -1;
	int fd;

	// open the requested url for reading
	// if the file does not exist, send a 404 error using send_error
	// if the file is a directory, send a 404 error using send_error
	// set file_size to the size of the file

	// LAB 6: Your code here.
	struct Stat stat;

	if((fd = open(req->url, O_RDONLY)) < 0){
		send_error(req, 404);
		r = fd;
		goto end;
	}

	if((r = fstat(fd, &stat)) < 0)
		goto end;
	
	if(stat.st_isdir){
		send_error(req, 404);
		r = -1;
		goto end;
	}

	file_size = stat.st_size;

	if ((r = send_header(req, 200)) < 0)
		goto end;

	if ((r = send_size(req, file_size)) < 0)
		goto end;

	if ((r = send_content_type(req)) < 0)
		goto end;

	if ((r = send_header_fin(req)) < 0)
		goto end;

	r = send_data(req, fd);

end:
	close(fd);
	return r;
}
```

这里可以看到，我们对url的操作也像对文件名的操作一样，直接调用open，因为url本身也是目录化的。这样就可以读取本地的文件了，如果无法打开，或者文件不存在，那么就可以调用`send_error`来发送我们喜闻乐见的404了。

到此位置，lab6就完成了：

```
testtime: OK (7.8s) 
pci attach: OK (1.0s) 
testoutput [5 packets]: OK (1.9s) 
testoutput [100 packets]: OK (2.4s) 
Part A score: 35/35

testinput [5 packets]: OK (1.8s) 
testinput [100 packets]: OK (2.4s) 
tcp echo server [echosrv]: OK (2.1s) 
web server [httpd]: 
  http://localhost:26002/: OK (1.3s) 
  http://localhost:26002/index.html: OK (2.3s) 
  http://localhost:26002/random_file.txt: OK (2.1s) 
Part B score: 70/70

Score: 105/105
```



