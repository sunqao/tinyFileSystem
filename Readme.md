# Rea

## 基本介绍

这里是用一个二进制的文件来模拟一个磁盘，通过向这个二进制文件中写入数据来模拟向磁盘中写入数据，在这个文件系统中我们实现了进入文件夹`cd`，创建文件夹`mkdir`，创建文件`mkf`，删除文件夹`rmdir`，删除文件`rmf`， 读取文件内容`read`，打开文件`open`，向文件中写入内容`write`，列出当前文件夹的所有文件`ls`，更改文件权限`attrib`等命令

## 文件系统的基本结构（单位：byte，字节）

```c
// 以下的单位都是字节
#define DATA_BLOCK 263680 // 数据块的起始地址
#define BLOCK_SIZE 512    // 块大小
#define DISK_START 0      // 磁盘的开始地址
#define BLOCK_BITMAP 512  // 数据块的位图的起始地址
#define INODE_BITMAP 1024 // inode的位图的起始地址
#define INODE_TABLE 1536  // inode的索引结点表的起始地址
#define INODE_SIZE 64     // 每个inode的大小是64b，单位字节
```



![image-20230210111215411](https://typora-1310242472.cos.ap-nanjing.myqcloud.com/typora_img/image-20230210111215411.png)

### **组描述符：**

用来存储文件系统的详细信息，比如块个数、块大小、空闲块

```c
// 组描述符的数据结构
struct group_desc
{
    char bg_volume_name[16];             /*卷名，也就是文件的系统名*/
    unsigned short bg_block_bitmap;      /*保存块位图的块号*/
    unsigned short bg_inode_bitmap;      /*保存索引结点位图的块号*/
    unsigned short bg_inode_table;       /*索引结点表的起始块号*/
    unsigned short bg_free_blocks_count; /*本组空闲块的个数*/
    unsigned short bg_free_inodes_count; /*本组空闲索引结点的个数*/
    unsigned short bg_used_dirs_count;   /*本组目录的个数*/
    char bg_pad[4];                      /*填充(0xff)*/
};
```

### **数据块位图**

我们通过位图来查找空闲块，数据块位图`512`字节，一共`4096`位，所以数据块的总个数也是`4096`个

### **`inode`位图**

同数据块位图一样，根据索引位图可知索引结点块的是`512*8`个，最多对应`512*8`个文件，每一个索引块，也就是索引结点，对应一个文件，一个索引结点块的大小是`64`字节

### **`inode`索引结点表**

**因为用户输入文件名进行比较找到对应的文件目录项的时候是将文件目录项所在的盘块一起调入内存的**，但是我们寻找对应的目录项的时候用来对比的是文件名，所以前面调入内存的盘块中的目录项中的其他描述信息是无效的，这时`unix`采用了将文件名与文件描述信息分开的方法，**使文件描述信息与文件名分开单独形成一个称为索引结点的数据结构**，这样可以减小文件目录项的大小，于是一个磁盘块所包含的目录项更多，从而减小平均调入内存的磁盘块数量。

`inode`索引结点表共有`512`个索引结点块，每个索引结点块的大小是`64`字节，一共可以对应`512*8`个文件

索引结点的结构如下：

```c
// 索引结点的数据结构
struct inode
{
    unsigned short i_mode;     /*文件类型及访问权限*/
    unsigned short i_blocks;   /*文件的数据块个数*/
    unsigned long i_size;      /*大小( 字节)*/
    unsigned long i_atime;     /*访问时间*/
    unsigned long i_ctime;     /*创建时间*/
    unsigned long i_mtime;     /*修改时间*/
    unsigned long i_dtime;     /*删除时间*/
    unsigned short i_block[8]; /*指向数据块的指针*/
    char i_pad[24];            /*填充(0xff)*/
};
```

### 目录项

文件的目录项其实就是目录文件的内容，每一个目录项对应当前目录下的一个文件，当前目录文件下的所有的文件的目录项组成一个目录文件，作为目录文件的内容写入在数据块中，所以目录文件其实也是一种文件，在数据块中一个数据块大小是`512`字节，最多包含`32`个目录项，一个目录项的大小是`16`字节，目录项的结构如下所示

```c
struct dir_entry
{                            // 目录项结构
    unsigned short inode;    /*文件的索引节点号*/
    unsigned short rec_len;  /*目录项长度*/
    unsigned short name_len; /*文件名长度*/
    char file_type;          /*文件类型(1: 普通文件， 2: 目录.. )*/
    char name[9];            /*文件名*/
};

struct dir_entry dir[32];         //  目录项缓冲区
//一个目录文件的一个数据块中对应32个目录项
```

### **数据块**

数据块的个数是`512*8`个，每块的大小是`512`字节，存放的是文件的内容（包括目录文件的目录项）

## 基本对应关系

当我们创建一个文件并向文件中写入东西的时候我们需要考虑以下几个地方：

1. 我们需要知道当前的目录，在当前的目录下创建一个文件
2. 创建一个文件之后需要修改当前目录以及新创建的文件的信息
3. 写入内容的时候是先写到一个缓冲区，然后再更新到磁盘中的，同理，更新信息的时候也是先写入到一个缓冲区然后再写入到磁盘存储的

定义的各种缓冲区如下：

```c
//缓冲区的意思时临时写入内容的地方，当内容写入进缓冲区后再写入到文件中
char Buffer[512];          // 针对数据块的 缓冲区
char tempbuf[4097];        // 输入缓冲区
unsigned char bitbuf[512]; // 位图缓冲区，这个位图缓冲是通用的
struct group_desc super_block[1]; //   组描述符缓冲区
struct inode inode_area[1];       //   节点缓冲区
struct dir_entry dir[32];         //  目录项缓冲区
```

当数据写入到缓冲区需要写入磁盘，或者我们需要从磁盘取出数据放入缓冲区的时候，我们将对磁盘的读写模拟为对一个二进制文件的读写，以读写文件的索引结点为例：

```c
void update_inode_entry(unsigned short i) // 写入更新第i个inode入口
{
    fseek(fp, INODE_TABLE + (i - 1) * INODE_SIZE, SEEK_SET); // 重定位到inode索引表
    fwrite(inode_area, INODE_SIZE, 1, fp);                   // 将节点缓冲区内容存储在文件中，存储在节点索引表中
}
void reload_inode_entry(unsigned short i) // 读取第i个inode入口
{
    fseek(fp, INODE_TABLE + (i - 1) * INODE_SIZE, SEEK_SET); // 重定位指针到inode索引表1536处加第i个inode
    fread(inode_area, INODE_SIZE, 1, fp);                    // 载入第i个inode入口到inode_area缓冲区中
}
```

















