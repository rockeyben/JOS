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



