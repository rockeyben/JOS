# MIT 6.828 LAB5

薛犇 1500012752

### i node

I 节点 region：包含了文件的重要的meta信息

- stat attributes
- pointers to its data block

data region: 往往被分成更大的data block 例如8KB

Directory entries 存放file names和指向inode的指针。如果有多个directory entry指向一个file的inode，那么就说这个file是hard linked。

JOS不使用inode，而是直接把file的meta info存在directory entry里面

### Sectors and Blocks

磁盘支持以sector为单位的读和写，JOS中，每个扇区的大小是512bytes。

File system以block为单位进行磁盘的分配和存储。

sector是磁盘的硬件属性，而block是操作系统视角适用磁盘时需要用到的概念。

block size 必须是 sector size的倍数

JOS使用4096bytes，和page size 一样大

### superblocks

file system会保留一些特殊的空间，用来集体存放 meta data，这些空间一般都很好定位，例如磁盘的最开始，或者最末尾。这些特殊的blocks被称为superblocks

JOS会把block 1作为superblock。inc/fs.h : struct Super

block 0 被用来存放boot loaders 和 partition table。现代的很多os会存放很多个sp block的copy，这样一旦一个失效了，另外的还可以用

### meta data

struct File内部有一个f_direct数组，这个数组最多可以存放10个block的标号。也就是说，如果一个file比10×4KB小的话，那么就用一个FIle结构体就可以存完了，假如比这个还要大，那么就利用一个indirect block来存放4096/4=1024个block编号。也就是说，JOS里最大是1034个block的file size。当然，在别的os里，为了存放更大的file，会支持二级，甚至三级indirect block

### Directory vs Regular File

区别在于，OS不会care Regular FIle的data block里存了什么。但是对于Directory File，OS会将它的内容解读为一系列的sub file 信息。

### Disk Access

file system environment需要能够访问磁盘，所以我们需要实现访问磁盘的功能。和传统的“一家独大”操作系统设计思路不一样，我们选择在用户层面实现IDE disk driver。

x86处理器使用EFLAGS中的IOPL位来决定保护模式下的代码是否有权限访问特殊的IO指令。由于所有的IDE disk寄存器都存在在x86的IO空间里，但是还没有被映射到内存，所以我们需要给予file system一个IO权限，来使得file system可以访问这些寄存器。这里需要修改IOPL位，来使得操作系统决定用户是否有权限访问IO空间。

```
Exercise 1. i386_init identifies the file system environment by passing the type ENV_TYPE_FS to your environment creation function, env_create. Modify env_create in env.c, so that it gives the file system environment I/O privilege, but never gives that privilege to any other environment.

Make sure you can start the file environment without causing a General Protection fault. You should pass the "fs i/o" test in make grade. 
```

```c
void
env_create(uint8_t *binary, enum EnvType type)
{
	struct Env * e;
	if(env_alloc(&e, 0) == 0){
		e->env_type = type;
		if(e->env_type == ENV_TYPE_FS)
			e->env_tf.tf_eflags |= FL_IOPL_3;
		load_icode(e, binary);
	}
}
```

在这里，我们需要在创建环境的时候判断，创建的环境是否是file system。如果是的话，就需要把它的elfags初始化的时候给予权限。之所以设置成3,我个人的想法是这和用户态的权限级别是一套的，用户态一般是3,所以这里也是设成3。

在这里我们还可以观察一下`fs/fs.c`里的`fs_init`函数：

```c
// Initialize the file system
void
fs_init(void)
{
	static_assert(sizeof(struct File) == 256);

	// Find a JOS disk.  Use the second IDE disk (number 1) if available
	if (ide_probe_disk1())
		ide_set_disk(1);
	else
		ide_set_disk(0);
	bc_init();

	// Set "super" to point to the super block.
	super = diskaddr(1);
	check_super();

	// Set "bitmap" to the beginning of the first bitmap block.
	bitmap = diskaddr(2);
	check_bitmap();
}
```

在这里，我们挂载了disk 1,也就是`fs/fs.img`，就是这个ide_set_disk函数。实际上，在jos里我们可以利用`qemu-img`指令来创建新的img，并也利用这里的函数来把它挂载到jos里，我们需要修改的就是GNUMAKEFILE里的：

```makefile
QEMUOPTS = -drive file=$(OBJDIR)/kern/kernel.img,index=0,media=disk,format=raw -serial mon:stdio -gdb tcp::$(GDBPORT)
QEMUOPTS += $(shell if $(QEMU) -nographic -help | grep -q '^-D '; then echo '-D qemu.log'; fi)
IMAGES = $(OBJDIR)/kern/kernel.img
QEMUOPTS += -smp $(CPUS)
QEMUOPTS += -drive file=$(OBJDIR)/fs/fs.img,index=1,media=disk,format=raw
IMAGES += $(OBJDIR)/fs/fs.img
QEMUOPTS += $(QEMUEXTRA)
```

需要在IMAGES项里增添新的img，即可。

### Block Cache

在文件系统里，我们需要实现一个buffer cache，其实就是一个block cache。这部分的代码在`fs/bc.c`里。所谓的buffer cache就是将一些最近访问的block存进内存，这样就可以利用时间局部性，在下一次访问的时候直接从内存中调用，而不用再重新从磁盘中读取，这样就会提升效率。

由于VM的尺寸关系，我们只能处理3GB以下的磁盘空间（4GB-1GB内核）。我们在0x10000000（DISKMAP）这里预留了3GB的VM空间，也就是到0xD0000000（DISKMAP+DISKMAX）为止，这部分区域作为磁盘的“内存映射”。比如block0会被映射到0x10000000上，block1会被映射到0x10001000上。`fs/bc.c`里帮我们写好的`diskaddr`函数非常管用，能够帮我们实现disk block number到虚拟地址的转换（还会处理一些sanity check）。

 由于我们的file system environment和别的环境不一样，是专门处理磁盘访问的，所以我们可以像这样预留这么大的地址空间，专项专用嘛。但是我们也要注意，一个现代的操作系统肯定不会这么做，因为大部分操作系统的磁盘空间都远大于3GB，对于32位机器来说是承载不了的，只有在64位机器上才能这么实现buffer cache。

由于把所有的磁盘读进内存需要很大的开销，所以我们还是实现了demand paging。当我们需要读取磁盘的时候再把这个block读进内存，因而我们就需要实现一个“block page fault”，也就是`fs/bc.c`里的`bc_fault`。现在我们可以看exercise 2了。

```
Exercise 2. Implement the bc_pgfault and flush_block functions in fs/bc.c. bc_pgfault is a page fault handler, just like the one your wrote in the previous lab for copy-on-write fork, except that its job is to load pages in from the disk in response to a page fault. When writing this, keep in mind that (1) addr may not be aligned to a block boundary and (2) ide_read operates in sectors, not blocks.

The flush_block function should write a block out to disk if necessary. flush_block shouldn't do anything if the block isn't even in the block cache (that is, the page isn't mapped) or if it's not dirty. We will use the VM hardware to keep track of whether a disk block has been modified since it was last read from or written to disk. To see whether a block needs writing, we can just look to see if the PTE_D "dirty" bit is set in the uvpt entry. (The PTE_D bit is set by the processor in response to a write to that page; see 5.2.4.3 in chapter 5 of the 386 reference manual.) After writing the block to disk, flush_block should clear the PTE_D bit using sys_page_map.

Use make grade to test your code. Your code should pass "check_bc", "check_super", and "check_bitmap". 
```

```c
	void * align_addr = ROUNDDOWN(addr, PGSIZE);
	if ((r = sys_page_alloc(0, align_addr, PTE_W | PTE_U | PTE_P)) < 0)
		panic("alloc err: %e", r);
	if ((r = ide_read(blockno * BLKSECTS, align_addr, BLKSECTS)) < 0)
		panic("ide read err: %e",r);

	// Clear the dirty bit for the disk block page since we just read the
	// block from disk
	if ((r = sys_page_map(0, align_addr, 0, align_addr, uvpt[PGNUM(align_addr)] & PTE_SYSCALL)) < 0)
		panic("in bc_pgfault, sys_page_map: %e", r);
```

对于`bc_fault`，我们需要首先分配一个页出来，这个页需要是用户可访问，可写的。接着我们调用系统内置的`ide_read`函数，这个函数会从磁盘的指定位置开始，读取指定长度的数据到内存的指定地址中。然后我们需要把这个page的dirty位置0,因为我们刚刚读取完毕，所以不可能是刚被写过的，这算一个sanity check。这里我们也可以发现一个有趣的做法，就是利用sys_page_map来修改page的权限位，是以后可以用到的技巧。

接着是`flush_block`

```c
	void * align_addr = ROUNDDOWN(addr, PGSIZE);
	// block is not in bc or is not dirty, do nothing
	if((!va_is_mapped(align_addr)) || (!va_is_dirty(align_addr)))
		return ;
	int r;
	if ((r = ide_write(blockno*BLKSECTS, align_addr, BLKSECTS)) < 0)
		panic("ide write err: %e", r);
	if ((r = sys_page_map(0, align_addr, 0, align_addr, uvpt[PGNUM(align_addr)] & PTE_SYSCALL)) < 0)
		panic(" sys page map err: %e", r);
```

这个函数的用处是：有来则必有回，既然我们有了把磁盘内容读进内存的函数`bc_fault`，就也需要有把这块内容写回磁盘的给写回去。于是就有了`flush_block`。

```c
	void * align_addr = ROUNDDOWN(addr, PGSIZE);
	// block is not in bc or is not dirty, do nothing
	if((!va_is_mapped(align_addr)) || (!va_is_dirty(align_addr)))
		return ;
	int r;
	if ((r = ide_write(blockno*BLKSECTS, align_addr, BLKSECTS)) < 0)
		panic("ide write err: %e", r);
	if ((r = sys_page_map(0, align_addr, 0, align_addr, uvpt[PGNUM(align_addr)] & PTE_SYSCALL)) < 0)
		panic(" sys page map err: %e", r);
```

这里我们首先要检查这个block是不是dirty的，如果不是，那么说明没有必要写回，安静处理就好。如果需要写回，那么我们就利用系统内置函数`ide_write`把这块内容写回磁盘，接着把page的dirty位给置0,因为我们刚刚写回，内存和磁盘已经一致了。

### Block Bitmap

bitmap相当于一个空闲块管理组织，在成熟的系统里我们可能会用成组链接法或者链表等数据结构来维护空闲块，而由于JOS里我们的磁盘空间很小，所以我们只需要用bitmap就可以，这是一个数组，0代表磁盘空闲，1代表磁盘被占用，为了节省空间，我们利用bit来表示所以就涉及到一些位运算操作。

我们来看exercise 3

````
Exercise 3. Use free_block as a model to implement alloc_block in fs/fs.c, which should find a free disk block in the bitmap, mark it used, and return the number of that block. When you allocate a block, you should immediately flush the changed bitmap block to disk with flush_block, to help file system consistency.

Use make grade to test your code. Your code should now pass "alloc_block". 
````

仿照`free_block`就可以写出来了。

```c
	int find_one = -1;
	for(int i = 2; i < super->s_nblocks; i++)
		if (bitmap[i/32] & (1 << (i%32))){
			find_one = i;
			bitmap[i/32] &= ~(1<<(i%32));
			flush_block(&bitmap[i/32]);
			break;
		}
	if (find_one == -1)
		return -E_NO_DISK;
	return find_one;
```

其实我们可以看到，这样找磁盘空闲块的方式是很开销很大的，需要遍历一遍数组才能找到空闲块，这也是我们需要一些更高级的空闲块维护方式的原因。

### File Operations

`fs/fs.c`里提供了一些基本的文件操作函数：

```c
int	file_get_block(struct File *f, uint32_t file_blockno, char **pblk);
int	file_create(const char *path, struct File **f);
int	file_open(const char *path, struct File **f);
ssize_t	file_read(struct File *f, void *buf, size_t count, off_t offset);
int	file_write(struct File *f, const void *buf, size_t count, off_t offset);
int	file_set_size(struct File *f, off_t newsize);
void	file_flush(struct File *f);
int	file_remove(const char *path);
void	fs_sync(void);
```

包括创建文件、打开文件、读写文件、删除文件等等。

在exercise 4里，我们需要实现`file_block_walk`和`file_get_block`函数，这两个函数是后续函数的基础。

`file_block_walk`

这个函数的作用是，读取File f的第`fileno`个块的真实块号。从之前的解释中可以知道，OS需要判断这个fileno是在直接索引里的，还是在后续的一级索引里的。如果是直接索引，那么直接返回File结构体内存的值就好了;如果是一级索引，那么读取这个以及索引的块号，再从这个块里读取真实块号。基本上就是这个逻辑。

```c
	int r;
	
	if (filebno >= NDIRECT + NINDIRECT)
		return -E_INVAL;
	
	if (filebno < NDIRECT){
		*ppdiskbno = f->f_direct + filebno;
		if (*ppdiskbno == 0)
			panic("*ppdiskbno is 0");
		return 0;
	}

	if (filebno >= NDIRECT){
		if (f->f_indirect > 0){
			*ppdiskbno = (uint32_t*)(DISKMAP + f->f_indirect * BLKSIZE + (filebno - NDIRECT) * 4);
			return 0;
		}
		if (!alloc){
			return -E_NOT_FOUND;
		}
		if ((r=alloc_block()) < 0)
			return -E_NO_DISK;
		void * bl_addr = (void*)(DISKMAP + r * BLKSIZE);
		memset(bl_addr, 0, PGSIZE);
		f->f_indirect = r;
		*ppdiskbno = (uint32_t*)(bl_addr + (filebno - NDIRECT) * 4);
		return 0;
	}
	return 0;
```

接着是`file_get_block`，他的功能是在上一个`file_block_walk`得到块号之后，把它在内存映射中的地址返回给用户。中间还要做一步sanity check：假如block walk显示，这个file这个位置并没有块，那么就意味着我们需要新分配一块给它。

```c
	uint32_t * ppdisk;
	int r;
	if ((r = file_block_walk(f, filebno, &ppdisk, 1)) < 0)
		return r;
	if (*ppdisk == 0){
		*ppdisk = alloc_block();
		if (*ppdisk < 0)
			return *ppdisk;
		memset(diskaddr(*ppdisk), 0, PGSIZE);
	}
	*blk = (char*)(DISKMAP + (*ppdisk)*PGSIZE);
	return 0;
```

### File System Interface

现在，因为我们之前打的一些小补丁，file system environment本身已经自洽了，现在我们需要做的就是把这个环境开放给别的进程，这就意味着我们需要实现一些进程间的通信。首先看看示意图：

```
      Regular env           FS env
   +---------------+   +---------------+
   |      read     |   |   file_read   |
   |   (lib/fd.c)  |   |   (fs/fs.c)   |
...|.......|.......|...|.......^.......|...............
   |       v       |   |       |       | RPC mechanism
   |  devfile_read |   |  serve_read   |
   |  (lib/file.c) |   |  (fs/serv.c)  |
   |       |       |   |       ^       |
   |       v       |   |       |       |
   |     fsipc     |   |     serve     |
   |  (lib/file.c) |   |  (fs/serv.c)  |
   |       |       |   |       ^       |
   |       v       |   |       |       |
   |   ipc_send    |   |   ipc_recv    |
   |       |       |   |       ^       |
   +-------|-------+   +-------|-------+
           |                   |
           +-------------------+
```

一开始，`read`函数会作用于一个给定的文件描述符，然后把后续的操作分派给合适的函数，也就是后面的`devfile_read`，实际上，我们还可以有别的设备类型，也就是dev类型，比如pipes。但是对于文件系统来说，我们需要指派的函数是`devfile_read`，这个函数特别地作用于磁盘文件，而`lib/file.c`里的所有`devfile`开头的函数都是作用于磁盘文件的。这些函数的任务是调用进程间通信函数`fsipc`，来向文件系统环境发送request。所谓的`fsipc`其实就是特地封装的专门负责和文件系统打交道的ipc，它主要做一些sanity check，然后利用`ipc_send`发送请求，并立即等待`ipc_recv`，直到文件系统返回ipc消息才退出。

以上是用户进程要做的事，现在我们来看文件系统那一端需要做什么：首先文件系统会在`fs/serv.c`的`serve`函数里不断循环，接收来自各个进程的文件请求。当一个请求到来的时候，会根据请求的类型，分派给适合的函数，比如如果是读请求，那么就分派给`serve_read`。而`serve_read`主要是解读ipc请求，从中明确具体的细节，比如读什么，读多少，等等，然后把这些都交给最终的`fs/fs.c`去做，好了，经过一层层的包工头，FS底层的小工人终于开始干活了，然后把这些东西依次返回到用户进程。

值得注意的是，我们把ipc通信里的value值设定为操作类型，比如读、写，而把一些参数全部存在传的page的`union Fsipc`里：

```c
union Fsipc {
	struct Fsreq_open {
		char req_path[MAXPATHLEN];
		int req_omode;
	} open;
	struct Fsreq_set_size {
		int req_fileid;
		off_t req_size;
	} set_size;
	struct Fsreq_read {
		int req_fileid;
		size_t req_n;
	} read;
	struct Fsret_read {
		char ret_buf[PGSIZE];
	} readRet;
	struct Fsreq_write {
		int req_fileid;
		size_t req_n;
		char req_buf[PGSIZE - (sizeof(int) + sizeof(size_t))];
	} write;
	struct Fsreq_stat {
		int req_fileid;
	} stat;
	struct Fsret_stat {
		char ret_name[MAXNAMELEN];
		off_t ret_size;
		int ret_isdir;
	} statRet;
	struct Fsreq_flush {
		int req_fileid;
	} flush;
	struct Fsreq_remove {
		char req_path[MAXPATHLEN];
	} remove;

	// Ensure Fsipc is one page
	char _pad[PGSIZE];
};
```

在用户端，我们把这个页定义在`fsipcbuf`，而在文件系统端，我们把这个页映射到`fsreq(0x0ffff000)`

对于文件系统端来说，它们并不需要再把这个页传回去，因为我们已经用share的方式使两个进程中的页共享了，所以我们只需要简单地写这个页就可以了。

接下来看exercise 5:

```
Exercise 5. Implement serve_read in fs/serv.c.

serve_read's heavy lifting will be done by the already-implemented file_read in fs/fs.c (which, in turn, is just a bunch of calls to file_get_block). serve_read just has to provide the RPC interface for file reading. Look at the comments and code in serve_set_size to get a general idea of how the server functions should be structured.

Use make grade to test your code. Your code should pass "serve_open/file_stat/file_close" and "file_read" for a score of 70/150. 
```

实现`serve_read`

```c

	struct Fsreq_read *req = &ipc->read;
	struct Fsret_read *ret = &ipc->readRet;
	int r;
	if (debug)
		cprintf("serve_read %08x %08x %08x\n", envid, req->req_fileid, req->req_n);

	// Lab 5: Your code here
	struct OpenFile * o;
	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;
	if ((r = file_read(o->o_file, (void*)(ret->ret_buf), (size_t)(req->req_n), o->o_fd->fd_offset)) < 0)
		return r;
	o->o_fd->fd_offset += r;

	return r;
```

我觉得这个exercise的突破口在于认真地观察`Fsreq_read`，`OpenFile`这两个结构体：

```c
struct Fsreq_read {
    int req_fileid;
    size_t req_n;
} read;

struct OpenFile {
	uint32_t o_fileid;	// file id
	struct File *o_file;	// mapped descriptor for open file
	int o_mode;		// open mode
	struct Fd *o_fd;	// Fd page
};
```

只要看懂这两个结构体里的内容都是干嘛的，这个exercise就解决了。在Fsreq_read里存放着fileid，我们可以利用内置的`openfile_lookup`函数根据这个fileid取寻找文件，然后返回`OpenFile`结构体，这里存着文件描述符和File结构体。

```c
struct Fd {
	int fd_dev_id;
	off_t fd_offset;
	int fd_omode;
	union {
		// File server files
		struct FdFile fd_file;
	};
};
struct File {
	char f_name[MAXNAMELEN];	// filename
	off_t f_size;			// file size in bytes
	uint32_t f_type;		// file type

	// Block pointers.
	// A block is allocated iff its value is != 0.
	uint32_t f_direct[NDIRECT];	// direct blocks
	uint32_t f_indirect;		// indirect block

	// Pad out to 256 bytes; must do arithmetic in case we're compiling
	// fsformat on a 64-bit machine.
	uint8_t f_pad[256 - MAXNAMELEN - 8 - 4*NDIRECT - 4];
} __attribute__((packed));	// required only on some 64-bit machines
```

可以看到，Fd一般存着和进程相关的信息，比如offset，打开模式，等等，而File一般存的都是静态信息，比如文件名，块号，等等。

这里我们根据函数的要求，把这些结构体中的对应项填入，就可以正确调用`file_read`了。最后不要忘记的是，在读完之后要更新fd的offset值。

```
Exercise 6. Implement serve_write in fs/serv.c and devfile_write in lib/file.c.

Use make grade to test your code. Your code should pass "file_write", "file_read after file_write", "open", and "large file" for a score of 90/150. 
```

`serve_write`:

```c
	int r;
	struct OpenFile * o;
	if ((r = openfile_lookup(envid, req->req_fileid, &o)) < 0)
		return r;
	if ((r = file_write(o->o_file, (void*)(req->req_buf), req->req_n, o->o_fd->fd_offset)) < 0)
		return r;
	o->o_fd->fd_offset += r;
	return r;
```

`serve_write`和`serve_read`大同小异，接着我们看`devfile_write`：

```c
	int r;
	n = MIN(n, sizeof(fsipcbuf.write.req_buf));
	fsipcbuf.write.req_fileid = fd->fd_file.id;
	fsipcbuf.write.req_n = n;
	memmove(fsipcbuf.write.req_buf, buf, n);
	if ((r = fsipc(FSREQ_WRITE, NULL)) < 0)
		return r;
	
	assert(r <= n);
	return r;
```

仿照`devfile_read`等函数的写法，我们可以比较轻松地写出write。有一点增加的地方就是，我们必须把需要写的内容拷贝到`fsipcbuf`结构体内，相当于要把信塞到信封里，邮递员才给送。

### Spawning Processes

Spawn这个词的字面意思是产卵，和一般的fork，无性克隆，不一样，这里的spawn生产出来的进程是“和父亲不一样的，有内容的”。如果把spawn的实际操作对应起来，会发现这个名字真的取的很形象。spawn要做的事情，就是从一个磁盘中的文件中加载一个程序，然后为它创造一个进程，并执行这个程序，其实就和unix中的先fork再执行差不多。

```
Exercise 7. spawn relies on the new syscall sys_env_set_trapframe to initialize the state of the newly created environment. Implement sys_env_set_trapframe in kern/syscall.c (don't forget to dispatch the new system call in syscall()).

Test your code by running the user/spawnhello program from kern/init.c, which will attempt to spawn /hello from the file system.

Use make grade to test your code. 
```

这里，大部分的spawn代码都已经帮我们实现好了，我们需要做的就是实现一个系统调用（非常符合JOS一贯仁慈的风格）。这个系统调用是给一个环境设置Trapframe，由于我们需要在创造完进程之后再进行装载程序等一系列操作，所以设置trapframe是要在用户态调用的，所以需要新增加这么一个系统调用。这里我们不仅要设置trapframe，还要做一些权限设置：

- 代码段设成用户级别
- 开中断
- 设置IOPL为0,就是默认有最高权限读写文件

```c
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	struct Env * e;
	int err = envid2env(envid, &e, 1);
	if (err < 0)
		return -E_BAD_ENV;
	
	user_mem_assert(e, (void*)tf, sizeof(struct Trapframe), PTE_U);
	
	e->env_tf = *tf;
	e->env_tf.tf_cs |= 3;
	e->env_tf.tf_eflags &= ~FL_IOPL_MASK;
	e->env_tf.tf_eflags |= FL_IF | FL_IOPL_0;
	return 0;
}
```

### Sharing library state across fork and spawn

`lib/fd.c`是一个重要的文件，存储了设备Dev信息，还维护了一个FDTABLE，存放着打开文件描述符表，我们希望打开文件描述符表应该是进程间共享的，这样的话，我们就需要设置一些share page。

在`fork`里，我们需要把PTE_SHARE的页直接共享映射，在spawn的`copy_shared_page`里，也要进行这样的操作。

```
Exercise 8. Change duppage in lib/fork.c to follow the new convention. If the page table entry has the PTE_SHARE bit set, just copy the mapping directly. (You should use PTE_SYSCALL, not 0xfff, to mask out the relevant bits from the page table entry. 0xfff picks up the accessed and dirty bits as well.)

Likewise, implement copy_shared_pages in lib/spawn.c. It should loop through all page table entries in the current process (just like fork did), copying any page mappings that have the PTE_SHARE bit set into the child process. 
```

对于`duppage`，加一个如下特判就可以：

```c
	if((uvpt[pn] & PTE_SHARE)){
		r = sys_page_map(0, addr, envid, addr, uvpt[pn]&PTE_SYSCALL);
		if (r < 0)
			return r;
		return 0;
	}
```

对于`copy_shared_page`：

```c

// Copy the mappings for shared pages into the child address space.
static int
copy_shared_pages(envid_t child)
{
	// LAB 5: Your code here.
	uint32_t addr;
	for (addr = UTEXT; addr < USTACKTOP; addr += PGSIZE){
		if( (uvpd[PDX(addr)]&PTE_P) && (uvpt[PGNUM(addr)]&PTE_P) && (uvpt[PGNUM(addr)]&PTE_U) 
			&& (uvpt[PGNUM(addr)]&PTE_SHARE)){
			int r = 0;
			r = sys_page_map(0, (void*)addr, child, (void*)addr, uvpt[PGNUM(addr)]&PTE_SYSCALL);
			if (r < 0)
				panic("copy share page %e\n", r);
		}
	}
	return 0;
}
```

### keyboard interface

在我们的os系统中，为了让shell能够工作，我们需要使用键盘和系统交互。qemu已经实现了利用CGA echo打出来的字的功能。但是为了使整个系统在用户态也能使用CGA功能，我们需要把键盘中断加进来，然后调用已经封装好的处理函数就可以。

```
Exercise 9. In your kern/trap.c, call kbd_intr to handle trap IRQ_OFFSET+IRQ_KBD and serial_intr to handle trap IRQ_OFFSET+IRQ_SERIAL. 
```

```c
		case IRQ_KBD + IRQ_OFFSET:
			kbd_intr();
			return ;
		case IRQ_SERIAL + IRQ_OFFSET:
			serial_intr();
```

### Shell

在这个part的开始，我们需要执行一系列指令，看看能否正确输出，在这里，给大家一个建议，如果这一步出了bug，而你又绞尽脑汁不知道这个lab哪里写错的时候，那么很有可能实在pmap.c里你就没有写好……

怎么说呢，尤其是pipe操作中，那个互相close fd的操作，就会稳定产生bug，我是之前的page_remove函数没有写好，里面调用page_lookup之后没有判断返回值是否为空，所以一直为陷入内核缺页故障。有好几个同学的反映也是这里出错，以供大家参考……

```
Exercise 10.

The shell doesn't support I/O redirection. It would be nice to run sh <script instead of having to type in all the commands in the script by hand, as you did above. Add I/O redirection for < to user/sh.c.

Test your implementation by typing sh <script into your shell

Run make run-testshell to test your shell. testshell simply feeds the above commands (also found in fs/testshell.sh) into the shell and then checks that the output matches fs/testshell.key. 
```

shell里，我们需要实现重定向操作，我觉得如果要理解`sh.c`的运作的话，可以尝试脑洞一下下面这个challenge，反正我做了一个之后就不会做后面的了，这个增添新操作符的challenge很有趣，但是似乎要对sh的运行逻辑有很好的理解，我反正看了半天还是不太会设计，所以就改做了别的lab。但是如果只是增加重定向功能的话，还是很好实现的：

```c
if ((fd = open(t, O_RDONLY)) < 0){
    cprintf("open %s for input: %e", t, fd);
    exit();
}

if (fd != 0){
    dup(fd, 0);
    close(fd);
}
```

其实就是把文件描述符和标准输入流绑定在一起，相信如果在ics这一定是一个简单的lab。。。

### challenge

我选择了第二个challenge: 定时清楚block cache

```
Challenge! The block cache has no eviction policy. Once a block gets faulted in to it, it never gets removed and will remain in memory forevermore. Add eviction to the buffer cache. Using the PTE_A "accessed" bits in the page tables, which the hardware sets on any access to a page, you can track approximate usage of disk blocks without the need to modify every place in the code that accesses the disk map region. Be careful with dirty blocks. 
```

意思就是我们的os没有定期清理buffer cache，这样如果满了，就非常尴尬。我设计的想法大体思路是：在`bc_fault`里增加一个全局变量`run_count`，每次进入这个函数，都会增加1,当访问次数到达一个阈值的时候，就开始一次清理，清理的步骤如下：

- 如果这页被访问过
  - 假如脏，就写回，然后设为未访问
- 如果这页没被访问过
  - 假如脏，就写回，然后unmap

这样就相当于给了所有页两次机会：第一次，加入你最近被访问，那么先不unmap你，因为最近可能还被访问，而只修改access位，修改这个位之后，这个页还是在内存中的，所以不会有影响; 第二次，假如你在这段时间内又被访问了，那么还是不unmap你，而如果你没有被访问，那么就只好unmap了。

```c
// challenge
	static int run_count = 0;
	if ((run_count++) % 100)
	{
		if (super) {
			uint32_t i;
			for (i = 1; i < super->s_nblocks; ++i) {
				void* addr = diskaddr(i);
				if (va_is_mapped(addr)) {
					if (va_is_accessed(addr)) {
						if (va_is_dirty(addr)) {
							flush_block(addr);
						}
						sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL);
					} else {
						if (va_is_dirty(addr)) {
							flush_block(addr);
						}
						sys_page_unmap(0, addr);
					}
				}
			}
		}
	}
```



### Question

```
1. Do you have to do anything else to ensure that this I/O privilege setting is saved and restored properly when you subsequently switch from one environment to another? Why? 
```

显然是不用担心的，因为这个IO privilege是设在eflags里的，而eflags是进程切换时必然会保存的一个寄存器，所以之前的措施已经足够保障进程切换时这个位能够被维护。

