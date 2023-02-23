// 以下的单位都是字节
#define DATA_BLOCK 263680 // 数据块的起始地址
#define BLOCK_SIZE 512    // 块大小
#define DISK_START 0      // 磁盘的开始地址
#define BLOCK_BITMAP 512  // 数据块的位图的起始地址
#define INODE_BITMAP 1024 // inode的位图的起始地址
#define INODE_TABLE 1536  // inode的索引结点表的起始地址
#define INODE_SIZE 64     // 每个inode的大小是64b，单位字节
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <signal.h>

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

// 目录项入口的数据结构
struct dir_entry
{                            // 目录项结构
    unsigned short inode;    /*索引节点号*/
    unsigned short rec_len;  /*目录项长度*/
    unsigned short name_len; /*文件名长度*/
    char file_type;          /*文件类型(1: 普通文件， 2: 目录.. )*/
    char name[9];            /*文件名*/
};

// 缓冲区的意思时临时写入内容的地方，当内容写入进缓冲区后再写入到文件中
char Buffer[512];          // 针对数据块的 缓冲区
char tempbuf[4097];        // 输入缓冲区
unsigned char bitbuf[512]; // 位图缓冲区，这个位图缓冲是通用的
unsigned short index_buf[256];
short fopen_table[16]; //  文件打开表

unsigned short last_alloc_inode; //  最近分配的节点号
unsigned short last_alloc_block; //  最近分配的数据块号，与bitbuf位图的每一位对应，last_alloc_block的值是几就对应bitbuf位图的第几位

unsigned short current_dir;       //   当前目录的节点号，也就是目录文件的索引结点号
struct group_desc super_block[1]; //   组描述符缓冲区
struct inode inode_area[1];       //   节点缓冲区
struct dir_entry dir[32];         //  目录项缓冲区
char current_path[256];           //    当前路径名
unsigned short current_dirlen;
FILE *fp;

/*************************************        alloc          *******************************************************/

//  fseek(FILE *stream, long int offset, int whence)
// 设置流 stream 的文件位置为给定的偏移 offset，
// 参数 offset 意味着从给定的 whence 位置查找的字节数
// 文件重定向后会覆盖原来的文件内容

void update_group_desc() // 写入更新组描述符到文件中
{
    fseek(fp, DISK_START, SEEK_SET);        // 重载定位符到文件的开头，下次开始向文件中写东西的时候从开头开始写
    fwrite(super_block, BLOCK_SIZE, 1, fp); // 将组描述符里内容存储到文件中
}
void reload_group_desc() // 读取组描述符
{
    fseek(fp, DISK_START, SEEK_SET); // 重定位流上文件内部位置指针
    // 加载文件中内容到组描述符中
    fread(super_block, BLOCK_SIZE, 1, fp);
    // superblock是组描述符缓冲区，在文件加载1个BLOCK_SIZE大小的块，存入superblock中（这里是缓冲区）
}

void update_inode_bitmap() // 写入更新inode位图到文件中
{
    fseek(fp, INODE_BITMAP, SEEK_SET);
    fwrite(bitbuf, BLOCK_SIZE, 1, fp);
}
void reload_inode_bitmap() // 读取inode位图
{
    fseek(fp, INODE_BITMAP, SEEK_SET); // 定位到索引节点位图
    fread(bitbuf, BLOCK_SIZE, 1, fp);  // 将存储的索引节点信息存入位图缓冲区
}
void update_block_bitmap() // 写入更新block位图到文件中
{
    fseek(fp, BLOCK_BITMAP, SEEK_SET); // 重定位到块位图
    fwrite(bitbuf, BLOCK_SIZE, 1, fp); // 将bitbuf内容写入文件存储
}
void reload_block_bitmap() // 读取block位图
{
    fseek(fp, BLOCK_BITMAP, SEEK_SET); // 定位到数据块位图
    fread(bitbuf, BLOCK_SIZE, 1, fp);  // 将存储的数据块位图信息存入位图缓冲区bitbuf
}

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

void reload_dir(unsigned short i) // 读取第i个目录
{
    fseek(fp, DATA_BLOCK + i * BLOCK_SIZE, SEEK_SET); // 重定位指针指向数据块部分与第i个目录
    fread(dir, BLOCK_SIZE, 1, fp);                    // 载入数据到目录项缓冲区中
}
void update_dir(unsigned short i) // 写入更新第i个目录
{
    fseek(fp, DATA_BLOCK + i * BLOCK_SIZE, SEEK_SET); // 定位到数据块的i目录或文件
    fwrite(dir, BLOCK_SIZE, 1, fp);                   // 写入其到文件存储下来
}
void reload_block(unsigned short i) // 读取第i个数据块
{
    fseek(fp, DATA_BLOCK + i * BLOCK_SIZE, SEEK_SET);
    fread(Buffer, BLOCK_SIZE, 1, fp);
}
void update_block(unsigned short i) // 写入更新第i个数据块
{
    fseek(fp, DATA_BLOCK + i * BLOCK_SIZE, SEEK_SET);
    fwrite(Buffer, BLOCK_SIZE, 1, fp);
}

int alloc_block() // 分配一个数据块,返回数据块号;
{
    // bitbuf共有512个字节，表示4096个数据块。根据last_alloc_block/8计算它在bitbuf的哪一个字节
    //  cur等于最近一个分配的数据块号，一个号对应8个位，即8个数据块
    // 哪怕这个数据块已经被分配过了也不要紧，在后面找位图块中的空闲0时会自动向后寻找
    unsigned short cur = last_alloc_block;
    unsigned char con = 128;
    int flag = 0;
    if (super_block[0].bg_free_blocks_count == 0)
    {
        // 本组无空闲块，报错返回
        printf("There is no block to be alloced!\n");
        return (0);
    }
    reload_block_bitmap();     // 载入数据块位图（文件中信息）到bitbuf数组中
    cur = cur / 8;             // 找到分配的数据块是在哪个位图块字节中
    while (bitbuf[cur] == 255) // bitbuf数组之中的一个数是一个字节，如果全满的话那就是11111111
    {
        if (cur == 511) // 如果所有的位图都已经满了则从头开始
            cur = 0;
        else
            cur++; // 若块位图cur处为255，则已全部使用，需要找下一个位图块，一个位图块是一个字节，八位
    }
    // 找到一个空闲的位图块后与100000000按位与，并用flag来记录位图块的哪一位是0，也就是空闲的数据块
    // con = 1 0000 0000
    while (bitbuf[cur] & con)
    {
        con = con / 2; // 将100000000的1不断往后移动继续按位与操作
        flag++;
    }

    bitbuf[cur] = bitbuf[cur] + con; // 将这个块的0的位置变成1

    last_alloc_block = cur * 8 + flag;     // 记录此时分配的块的位置
    update_block_bitmap();                 // 将修改后的块位图写入文件存储
    super_block[0].bg_free_blocks_count--; // 组描述符空闲块个数减一
    update_group_desc();                   // 将组描述符内容存储到文件中
    return last_alloc_block;               // 返回数据块号
}

void remove_block(unsigned short del_num)
{
    // 删除一个block，将对应的位图设置为0即可
    // 下次分配的时候直接覆盖这个数据块的内容
    unsigned short tmp;
    // 得到这个数据块在哪一个位图块中
    tmp = del_num / 8;
    // 将数据块位图加载到位图缓冲区中
    reload_block_bitmap();
    // 数据块位图bitbuf一共有512个字节，即bitbuf[512]，共可以对应512 * 8个数据块
    switch (del_num % 8) // 更改block位图
    {
        // del_num % 8得到对应的位图块中的哪一位
    case 0:
        bitbuf[tmp] = bitbuf[tmp] & 127; // 01111111
        break;
    case 1:
        bitbuf[tmp] = bitbuf[tmp] & 191; // 10111111
        break;
    case 2:
        bitbuf[tmp] = bitbuf[tmp] & 223; // 11011111
        break;
    case 3:
        bitbuf[tmp] = bitbuf[tmp] & 239; // 11101111
        break;
    case 4:
        bitbuf[tmp] = bitbuf[tmp] & 247; // 11110111
        break;
    case 5:
        bitbuf[tmp] = bitbuf[tmp] & 251; // 11111011
        break;
    case 6:
        bitbuf[tmp] = bitbuf[tmp] & 253; // 11111101
        break;
    case 7:
        bitbuf[tmp] = bitbuf[tmp] & 254; // 11111110
        break;
    }
    // 将位图缓冲更新到磁盘的数据块位图中
    update_block_bitmap();

    super_block[0].bg_free_blocks_count++;
    // 将组描述符更新到磁盘中
    update_group_desc();
}

int get_inode() // 分配一个inode,返回序号
{
    // 基本思想与分配数据块的时候一样
    //  记录当前分配的索引结点块号
    unsigned short cur = last_alloc_inode;
    unsigned char con = 128;
    int flag = 0;
    if (super_block[0].bg_free_inodes_count == 0)
    {
        // 如果没有空闲的索引结点块号了报错返回
        printf("There is no Inode to be alloced!\n");
        return 0;
    }
    reload_inode_bitmap();
    // 减1是因为cur是因为索引是从1开始的
    // 索引是从1开始但是位图是从0开始的所以得减一
    cur = (cur - 1) / 8;

    // 以下的思想和分配数据块的思想相同
    while (bitbuf[cur] == 255)
    {
        // 如果当前字节所在的位图已满

        if (cur == 511) // 如果索引位图块是最后一块，就从头开始
            cur = 0;
        else
            cur++; // 如果不是最后一块，就进入到下一块的索引位图
    }
    // 与con = 1 0000 0000 进行按位与操作找到当前的位图块是哪一位为0
    while (bitbuf[cur] & con)
    {
        con = con / 2;
        flag++;
    }
    // 将这一位置为一
    bitbuf[cur] = bitbuf[cur] + con;
    // 记下此时最近分配的索引块号，索引块号是从1开始的
    last_alloc_inode = cur * 8 + flag + 1;
    // 更新索引位图信息
    update_inode_bitmap();
    super_block[0].bg_free_inodes_count--;
    // 更新组描述符信息
    update_group_desc();
    // 返回索引结点的块号
    return last_alloc_inode;
}
//
void remove_inode(unsigned short del_num)
{
    unsigned short tmp;
    tmp = (del_num - 1) / 8;
    reload_inode_bitmap();
    switch ((del_num - 1) % 8) // 更改block位图
    {
    case 0:
        bitbuf[tmp] = bitbuf[tmp] & 127;
        break;
    case 1:
        bitbuf[tmp] = bitbuf[tmp] & 191;
        break;
    case 2:
        bitbuf[tmp] = bitbuf[tmp] & 223;
        break;
    case 3:
        bitbuf[tmp] = bitbuf[tmp] & 239;
        break;
    case 4:
        bitbuf[tmp] = bitbuf[tmp] & 247;
        break;
    case 5:
        bitbuf[tmp] = bitbuf[tmp] & 251;
        break;
    case 6:
        bitbuf[tmp] = bitbuf[tmp] & 253;
        break;
    case 7:
        bitbuf[tmp] = bitbuf[tmp] & 254;
        break;
    }
    update_inode_bitmap();
    super_block[0].bg_free_inodes_count++;
    update_group_desc();
}

/************************************   dir ************************************/
// 为新增目录或文件分配 dir_entry
// 对于新增文件，只需分配一个 inode 号
// 对于新增目录，除了 inode 号外，还需要分配数据区存储.和..两个目录项
void dir_prepare(unsigned short tmp, unsigned short len, int type) // 新目录和文件初始化.and ..
{
    // tmp是新文件对应的索引结点号
    // len是新文件的文件名长度
    // type是新文件的文件类型
    // 将文件的索引结点读入到索引结点缓冲inode_area[0]中
    reload_inode_entry(tmp); // 得到新目录的节点入口地址
    if (type == 2)
    {
        // 如果这个文件是目录文件
        // 这个目录文件先有两个目录项，一个是 . 一个是 ..
        inode_area[0].i_size = 32;
        // 拥有一个数据块
        inode_area[0].i_blocks = 1;
        // 分配一个数据块，返回数据块号
        inode_area[0].i_block[0] = alloc_block();
        // 第一个目录文件.的索引结点号也就是自己
        dir[0].inode = tmp;
        // 第二个目录文件..的索引结点号是新目录文件的上层目录的索引结点号，也就是当前目录的结点号
        dir[1].inode = current_dir;
        // 第一个目录文件.的文件名也就是自己
        dir[0].name_len = len;
        // 第二个目录文件..的文件名是新目录文件的上层目录的文件名，也就是当前目录的文件名
        dir[1].name_len = current_dirlen;
        // 两个文件的类型都是目录文件
        dir[0].file_type = dir[1].file_type = 2;
        // 初始化新目录文件第一个数据块中剩下29个目录项
        for (type = 2; type < 32; type++)
            dir[type].inode = 0;
        // 设置这两个目录项的名字
        strcpy(dir[0].name, ".");
        strcpy(dir[1].name, "..");
        // 将新目录文件的目录项缓冲写入到新目录文件得到的数据块中
        update_dir(inode_area[0].i_block[0]);
        // 新文件目录的权限
        inode_area[0].i_mode = 01006; // drwxrwxrwx:目录
    }
    else
    {
        // 如果是一个普通的文件
        inode_area[0].i_size = 0;
        inode_area[0].i_blocks = 0;
        inode_area[0].i_mode = 0407; // drwxrwxrwx:文件
    }
    // 将这个新文件的索引结点写入到磁盘中
    update_inode_entry(tmp);
}

// 查找当前目录中名为tmp的文件或目录，并得到该文件的inode号，它在上级目录中是第几块数据块以及这个数据块中是第几个目录项
unsigned short reserch_file(char tmp[9], int file_type, unsigned short *inode_num, unsigned short *block_num, unsigned short *dir_num)
{
    // 查找文件并改写缓冲区里节点号，所在目录节点的数据块号（0~7）、目录项所在号
    unsigned short j, k;
    // current_dir是当前目录文件的索引节点号
    // 读取当前目录的索引信息到inode_area中，这是一个inode的结构体，包含文件的类型，访问权限等等
    reload_inode_entry(current_dir);
    j = 0;
    // inode_area[0].i_blocks这个是当前目录文件所占的数据块个数，最大是7
    while (j < inode_area[0].i_blocks)
    {

        // 读取目录文件的内容，其实也就是读取目录文件中的目录项内容
        // 读取目录文件的第j块数据块的内容到目录项缓冲区中也就是dir[32]这个目录项数组
        reload_dir(inode_area[0].i_block[j]);
        k = 0;
        // 遍历这个数据块的32个目录项，寻找目标文件
        // 注意下寻找文件的时候文件名以及文件类型必须完全一致才行
        while (k < 32)
        { // file_type=2代表是目录文件，找到tmp文件对应的节点号，数据块号，以及当前的目录号
            if (!dir[k].inode || dir[k].file_type != file_type || strcmp(dir[k].name, tmp))
                k++;
            else
            {
                // 得到寻找的文件的索引结点号
                *inode_num = dir[k].inode;
                // 得到寻找的文件在上级目录中是第几块数据块
                *block_num = j;
                // 得到寻找的文件在上级目录中数据块中是第几项目录项
                *dir_num = k;
                return 1;
            }
        }
        // 若当前数据块的目录项中没有当前文件的目录项，则继续遍历上级目录所包含的数据块
        j++;
    }
    return 0;
}

//
void cd(char tmp[9])
{
    //cd命令的实现
    unsigned short i, j, k, flag;
    // 查找目录文件名，i表示的是文件对应的索引块号
    // j是在上级目录中是第几块数据块
    // k是这个数据块中是第几个目录项
    flag = reserch_file(tmp, 2, &i, &j, &k);
    if (flag)
    {
        // 更改当前目录的索引块号
        current_dir = i;
        // 如果tmp=..则是返回上一级目录
        if (!strcmp(tmp, "..") && dir[k - 1].name_len)
        {
            //如果是返回上一级目录，那么k对应的是..文件的目录项，k - 1对应的就是.文件的目录项
            //即k - 1对应的就是当前目录文件的目录项
            //这时需要将当前路径去除当前目录的名字也就是.目录文件的名字 
            current_path[strlen(current_path) - dir[k - 1].name_len - 1] = '\0';
            //然后将当前目录文件的长度设置为..文件的名字的长度，也就是上级目录的名字的长度
            current_dirlen = dir[k].name_len;
        }
        else if (!strcmp(tmp, "."))
        //如果是当前目录则不做改变
            ;
        else if (strcmp(tmp, ".."))
        {
            // 进入tmp目录
            // strcmp(tmp, "..")如果相等的话返回的是0，这里不相等，也就是进入到一个普通的木中
            current_dirlen = strlen(tmp);
            //打印当前加上tmp后的目录路径
            strcat(current_path, tmp);
            strcat(current_path, "/");
        }
    }
    else
        printf("The directory %s not exists!\n", tmp);
}

//
void del(char tmp[9])
{
    //删除一个普通文件
    unsigned short i, j, k, m, n, flag;
    m = 0;
    // 查找目录文件名，i表示的是文件对应的索引块号
    // j是在上级目录中是第几块数据块
    // k是这个数据块中是第几个目录项
    flag = reserch_file(tmp, 1, &i, &j, &k);
    if (flag)
    {
        flag = 0;
        //查看文件打开表里的文件是否被打开
        while (fopen_table[flag] != dir[k].inode && flag < 16)
            flag++;
        //fopen_table[16]里最多可以打开16个文件，这里面存放的是文件的索引结点号
        //如果这个文件被打开了
        //删除这个文件
        if (flag < 16)
            fopen_table[flag] = 0;
        //将这个被删除的文件的索引信息加载到inode_area[0]中
        reload_inode_entry(i);
        //释放这个文件占有的所有的数据块
        while (m < inode_area[0].i_blocks)
            remove_block(inode_area[0].i_block[m++]);
        
        inode_area[0].i_blocks = 0;
        inode_area[0].i_size = 0;
        //释放这个文件占有的索引块
        remove_inode(i);
        //将这个被删除文件的上级目录文件的索引加载到inode_area[0]中
        reload_inode_entry(current_dir);
        //释放掉这个被删除文件占据的目录项
        dir[k].inode = 0;
        // if(k!=0)dir[k-1].rec_len+=dir[k].rec_len ;
        // 更新上级目录对应的数据块中的目录项
        update_dir(inode_area[0].i_block[j]);
        // 更新上级目录文件的大小
        inode_area[0].i_size -= 16;
        m = 1;
        printf("%d\n", i);
        printf("%d\n",inode_area[0].i_blocks);
        //释放掉被删除文件的目录项之后
        //释放掉目录文件中目录项的空闲数据块
        while (m <= inode_area[0].i_blocks)
        {
            printf("个数：%d\n", inode_area[0].i_blocks);
            printf("m: %d", m);
            flag = n = 0;
            //将一个块中的所有目录加载到dir中
            reload_dir(inode_area[0].i_block[m]);
            //遍历当前dir块的所有的inode计算为空的目录项
            while (n < 32)
            {
                if (!dir[n].inode)
                    flag++;
                n++;
            }
            // 如果这个目录块32个目录项全为空
            if (flag == 32)
            {
                //从目录文件中删除这个数据块
                remove_block(inode_area[0].i_block[m]);
                inode_area[0].i_blocks--;
                //将记录数据块索引结点的值向前移动覆盖掉这个值
                while (m < inode_area[0].i_blocks)
                    inode_area[0].i_block[m] = inode_area[0].i_block[++m];
            }else{
                //继续遍历数据块
                m++;
            }
        }
        //更新当前目录的
        update_inode_entry(current_dir);
    }
    else
        printf("The file %s not exists!\n", tmp);
}

// 创造一个文件或者是目录，type = 1就是创造一个普通文件
// type = 2就是创造一个目录
void mkdir(char tmp[9], int type)
{
    unsigned short tmpno, i, j, k, flag;
    // 获得当前目录文件的索引结点给inode_area[0]
    reload_inode_entry(current_dir);
    if (!reserch_file(tmp, type, &i, &j, &k)) // 未找到同名文件
    {
        if (inode_area[0].i_size == 4096) // 目录项已满
        {                                 // 当前目录最多有8个数据块，每个数据块512字节，如果当前目录文件的大小是4096说明没有多余的目录项了
            printf("Directory has no room to be alloced!\n");
            return;
        }
        flag = 1;
        if (inode_area[0].i_size != inode_area[0].i_blocks * 512) // 目录中有某些个块中32个项未满
        {
            // 当前目录拥有了i_blocks个数据块但是当前目录的大小不是i_blocks * 512
            // 注意拥有一个数据块并不等于目录文件大小增加512字节，只有写入东西的时候才会增加文件的大小
            i = 0;
            // 遍历当前目录文件的每一块数据块
            while (flag && i < inode_area[0].i_blocks)
            {
                reload_dir(inode_area[0].i_block[i]);
                j = 0;
                // 遍历每一块数据块的32个目录项，找到是哪一个目录项为空
                while (j < 32)
                {
                    // 目录项为空的标志就是这个目录项对应的文件没有索引结点，也就是索引结点号为0（索引结点是从1开始的)
                    if (dir[j].inode == 0)
                    {
                        // 跳出循环，这里使用flag是为了跳出外层循环
                        // 得到在当前目录下的第i块数据块中的第j个目录项为空，可以设置为创建文件的目录项
                        flag = 0;
                        break;
                    }
                    // 继续遍历目录项
                    j++;
                }
                // 继续遍历数据块
                i++;
            }
            // 给这个文件分配一个索引结点并返回索引块号
            tmpno = dir[j].inode = get_inode();
            // 设置文件对应的目录项中文件名的长度
            dir[j].name_len = strlen(tmp);
            // 设置文件对应的目录项中文件的类型
            dir[j].file_type = type;
            // 设置文件对应的目录项中的文件名
            strcpy(dir[j].name, tmp);
            // 在上面中跳出外层循环的时候i多加了一次，所以这里是写入当前目录的第i - 1块数据块
            // 得到这个数据块的块号，然后将32个目录项写入到这个数据块中
            update_dir(inode_area[0].i_block[i - 1]);
        }
        else
        {
            // 如果当前目录拥有的数据块中的目录项全满
            // 然后分配一个数据块
            // 注意i_blocks的取值是小于等于8，其一开始的值是0
            // 每分配一个数据块，就将数据块的块号赋给i_block[i]，并且i_blocks + 1
            // i_blocks比i_block始终大一
            inode_area[0].i_block[inode_area[0].i_blocks] = alloc_block();
            inode_area[0].i_blocks++;
            // 将新分配的数据块的32个目录项读入到目录项缓冲区中
            reload_dir(inode_area[0].i_block[inode_area[0].i_blocks - 1]);
            // 给新文件分配一个所以结点，得到索引结点号，也就是索引结点的块号
            tmpno = dir[0].inode = get_inode();
            // 在目录项中写入新文件的文件名长度
            dir[0].name_len = strlen(tmp);
            // 在目录项中写入新文件的文件类型
            dir[0].file_type = type;
            // 在目录项中写入文件名字
            strcpy(dir[0].name, tmp);
            // 初始化新块后面的目录项，后面的目录项为空，所以对应的索引结点为0
            for (flag = 1; flag < 32; flag++)
                dir[flag].inode = 0;
            // 将新的目录项写入到新分配的数据块中，数据块号为i_block[inode_area[0].i_blocks - 1]
            update_dir(inode_area[0].i_block[inode_area[0].i_blocks - 1]);
        }
        // 一个数据块512字节，包含32个目录项，一个目录项16字节，一个目录文件最大为8 * 512字节，也就是4k
        // 在当前目录文件新加入了一个目录项，所以当前目录文件要加上16个字节
        inode_area[0].i_size += 16;
        // 将当前目录的缓冲索引结点的信息重新写入当面目录对应磁盘中的的索引结点中
        update_inode_entry(current_dir);
        // 为新文件初始化索引结点
        // 或是为新的目录文件初始化这个目录文件的目录项
        dir_prepare(tmpno, strlen(tmp), type);
    }
    else // 已经存在同名文件或目录
    {
        if (type == 1)
            printf("File has already existed!\n");
        else
            printf("Directory has already existed!\n");
    }
}

//
void rmdir(char tmp[9])
{
    unsigned short i, j, k, flag;
    unsigned short m, n;
    //当前目录与上级目录不可以被删除
    if (!strcmp(tmp, "..") || !strcmp(tmp, "."))
    {
        printf("The directory can not be deleted!\n");
        return;
    }
    // 查找目录文件名，i表示的是文件对应的索引块号
    // j是在上级目录中是第几块数据块
    // k是这个数据块中是第几个目录项
    flag = reserch_file(tmp, 2, &i, &j, &k);
    if (flag)
    {
        //将这个目录文件的索引信息载入到inode_area[0]中
        reload_inode_entry(dir[k].inode); // 找到要删除的目录的节点并载入
        // 目录文件中只有.and ..的时候才可以删除
        if (inode_area[0].i_size == 32)   
        {
            //清零
            inode_area[0].i_size = 0;
            inode_area[0].i_blocks = 0;
            // reload_dir(inode_area[0].i_block[0]);
            // dir[0].inode=0;
            // dir[1].inode=0;
            // 释放数据块
            remove_block(inode_area[0].i_block[0]);
            // 得到当前目录的节点，释放被删除目录文件所占的索引块
            reload_inode_entry(current_dir); 
            remove_inode(dir[k].inode);
            //被删除目录的目录项释放
            dir[k].inode = 0;
            update_dir(inode_area[0].i_block[j]);
            //修改当前目录文件的大小
            inode_area[0].i_size -= 16;
            flag = 0;
            m = 1;
            //删除目录文件释放其在上级目录中对应的目录项后
            //若目录项所在数据块为空，则释放掉这个数据块
            while (flag < 32 && m <= inode_area[0].i_blocks)
            {
                flag = n = 0;
                reload_dir(inode_area[0].i_block[m]);
                while (n < 32)
                {
                    if (!dir[n].inode)
                        flag++;
                    n++;
                }
                //如果被删除的目录文件所在的数据项所在的数据块为空
                //则释放掉这个数据块
                if (flag == 32)
                {
                    remove_block(inode_area[0].i_block[m]);
                    inode_area[0].i_blocks--;
                    while (m < inode_area[0].i_blocks)
                        inode_area[0].i_block[m] = inode_area[0].i_block[++m];
                }else{
                    //继续遍历
                    m ++;
                }
            }
            update_inode_entry(current_dir);
        }
        else
            printf("Directory is not null!\n");
    }
    else
        printf("Directory to be deleted not exists!\n");
}
//

void ls()
{
    printf("items         type           mode          size\n"); // 15*4
    unsigned short i, j, k, tmpno, no;
    i = 0;
    //将当前目录的基本信息加载到inode_area[0]中
    reload_inode_entry(current_dir);
    //遍历目录文件的每一个目录项
    while (i < inode_area[0].i_blocks)
    {
        k = 0;
        //将当前数据块的目录项集合加载到dir中
        reload_dir(inode_area[0].i_block[i]);
        //遍历这32个目录项
        while (k < 32)
        {
            if (dir[k].inode)
            {
                //如果这个目录项对应的文件存在
                //打印文件的名字
                printf("%s", dir[k].name);

                //如果这是一个目录文件
                if (dir[k].file_type == 2)
                {
                    j = 0;//j用来控制文件的打印格式
                    //将这个目录文件的信息读取到inode_area[0]中
                    reload_inode_entry(dir[k].inode);
                    //文件的名字控制在15个空格长度
                    if (!strcmp(dir[k].name, ".."))
                        while (j++ < 13)
                            printf(" ");
                    else if (!strcmp(dir[k].name, "."))
                        while (j++ < 14)
                            printf(" ");
                    else
                        while (j++ < 15 - dir[k].name_len)
                            printf(" ");
                    printf("<DIR>          ");
                    //从文件信息中读取文件的权限
                    switch (inode_area[0].i_mode & 7)
                    {
                    case 1:
                        printf("____x");
                        break;
                    case 2:
                        printf("__w__");
                        break;
                    case 3:
                        printf("__w_x");
                        break;
                    case 4:
                        printf("r____");
                        break;
                    case 5:
                        printf("r___x");
                        break;
                    case 6:
                        printf("r_w__");
                        break;
                    case 7:
                        printf("r_w_x");
                        break;
                    }
                    //目录文件不打印大小
                    printf("         ----");
                }
                else if (dir[k].file_type == 1)
                {
                    //如果是一个普通的文件
                    j = 0;
                    //将这个目录文件的信息读取到inode_area[0]中
                    reload_inode_entry(dir[k].inode);

                    while (j++ < 15 - dir[k].name_len)
                        printf(" ");
                    printf("<FILE>         ");
                    //打印文件的权限
                    switch (inode_area[0].i_mode & 7)
                    {
                    case 1:
                        printf("____x");
                        break;
                    case 2:
                        printf("__w__");
                        break;
                    case 3:
                        printf("__w_x");
                        break;
                    case 4:
                        printf("r____");
                        break;
                    case 5:
                        printf("r___x");
                        break;
                    case 6:
                        printf("r_w__");
                        break;
                    case 7:
                        printf("r_w_x");
                        break;
                    }
                    //打印文件的大小
                    printf("         %d bytes     ", inode_area[0].i_size);
                }
                printf("\n");
            }
            //继续下一个目录项
            k++;
            reload_inode_entry(current_dir);
        }
        //继续下一个当前目录的数据块
        i++;
    }
}

/***********************            file    *****************************************************************/

//short fopen_table[16];是一个short类型的数组，这里面存放这最多16个已经打开的文件的inode块号
unsigned short search_file(unsigned short Ino) 
{// 在打开文件表中查找是否已打开文件
    unsigned short fopen_table_point = 0;
    while (fopen_table_point < 16 && fopen_table[fopen_table_point++] != Ino)
        ;
    if (fopen_table_point == 16)
        return 0;
    //如果这个文件的inode块号在文件打开表中，就说明这个文件被打开了，返回1
    return 1;
}


void read_file(char tmp[9]) // 读文件
{
    // 给文件名来读取文件的内容
    unsigned short flag, i, j, k;
    // 查找目录文件名，i表示的是文件对应的索引块号
    // j是在上级目录中是第几块数据块
    // k是这个数据块中是第几个目录项
    flag = reserch_file(tmp, 1, &i, &j, &k); // 返回文件目录项的信息
    if (flag)
    {

        if (search_file(dir[k].inode))//查看这个文件是否在打开文件列表里
        {
            //加载这个文件的索引信息到inode_area[0]中
            reload_inode_entry(dir[k].inode);
            if (!(inode_area[0].i_mode & 4)) // i_mode:111b:读,写,执行
            {
                printf("The file %s can not be read!\n", tmp);
                return;
            }
            //读取这个文件的数据块，将数据块中的内容写出来
            for (flag = 0; flag < inode_area[0].i_blocks; flag++)
            {
                reload_block(inode_area[0].i_block[flag]);
                Buffer[512] = '\0';
                printf("%s", Buffer);
            }
            //如果仅仅是在前面的for循环中flag = 0但是没有进入循环说明这个文件没有分配数据块这个文件为空
            if (flag == 0)
                printf("The file %s is empty!\n", tmp);
            else
                printf("\n");
        }
        else
            printf("The file %s has not been opened!\n", tmp);
    }
    else
        printf("The file %s not exists!\n", tmp);
}

void write_file(char tmp[9]) // 写文件
{
    //给出文件名向文件中写入东西
    unsigned short flag, i, j, k, size = 0, need_blocks;
    // 查找目录文件名，i表示的是文件对应的索引块号
    // j是在上级目录中是第几块数据块
    // k是这个数据块中是第几个目录项
    flag = reserch_file(tmp, 1, &i, &j, &k);
    if (flag)//写入文件的时候必须保证文件存在
    {
        if (search_file(dir[k].inode))//保证文件打开
        {
            //将文件的索引信息加载到inode_area中
            reload_inode_entry(dir[k].inode);
            if (!(inode_area[0].i_mode & 2)) // i_mode:111b:读,写,执行
            {
                printf("The file %s can not be writed!\n", tmp);
                return;
            }
            while (1)
            {
                //从键盘读入写入的数据
                tempbuf[size] = getchar();
                //写入的数据以#结束
                if (tempbuf[size] == '#')
                {
                    tempbuf[size] = '\0';
                    break;
                }
                if (size >= 4096)
                {//文件大小不能超过4kb，也就是8个512字节的数据块
                    printf("Sorry,the max size of a file is 4KB!\n");
                    tempbuf[size] = '\0';
                    break;
                }
                size++;
            }
            //计算存入文件数据需要的数据块
            need_blocks = strlen(tempbuf) / 512;
            //不足512字节的进一
            if (strlen(tempbuf) % 512)
                need_blocks++;
            //将数据存入到数据块中
            if (need_blocks < 9)
            {
                while (inode_area[0].i_blocks < need_blocks)
                {
                    //分配数据块，并把分配的数据块号存在文件的索引信息中
                    inode_area[0].i_block[inode_area[0].i_blocks] = alloc_block();
                    inode_area[0].i_blocks++;
                }
                j = 0;
                //向数据块中写入数据
                while (j < need_blocks)
                {
                    //need_blocks - 1表示的是最后一个数据块号存放的下标
                    //如果j不是最后一块数据块
                    if (j != need_blocks - 1)
                    {
                        //将数据块的内容读取到buffer中
                        reload_block(inode_area[0].i_block[j]);
                        //然后用tembuf的内容覆盖掉Buffer
                        memcpy(Buffer, tempbuf + j * BLOCK_SIZE, BLOCK_SIZE);
                        //再将buffer的内容写入到磁盘的数据块中
                        update_block(inode_area[0].i_block[j]);
                    }
                    else
                    {
                        //如果是最后一块数据块
                        reload_block(inode_area[0].i_block[j]);
                        //仍然是将tembuf的内容覆盖掉Buffer，但是要控制一下长度
                        memcpy(Buffer, tempbuf + j * BLOCK_SIZE, strlen(tempbuf) - j * BLOCK_SIZE);
                        //文件的写入方式是覆盖掉文件以前的内容
                        //如果写入的内容大于文件本身的大小则直接在当前写入的地方加上休止符即可
                        //如果小于当前文件的内容，则之前文件从写入的字符已经带有结束符，不用再写入
                        if (strlen(tempbuf) > inode_area[0].i_size)
                        {
                            Buffer[strlen(tempbuf) - j * BLOCK_SIZE] = '\0';
                            inode_area[0].i_size = strlen(tempbuf);
                        }
                        update_block(inode_area[0].i_block[j]);
                    }
                    //继续向分配的数据块中写入内容
                    j++;
                }
                //更新文件的索引信息
                update_inode_entry(dir[k].inode);
            }
            else
                printf("Sorry,the max size of a file is 4KB!\n");
        }
        else
            printf("The file %s has not opened!\n", tmp);
    }
    else
        printf("The file %s does not exist!\n", tmp);
}

//
void close_file(char tmp[9]) // 关闭文件
{
    unsigned short flag, i, j, k;
    // 查找目录文件名，i表示的是文件对应的索引块号
    // j是在上级目录中是第几块数据块
    // k是这个数据块中是第几个目录项
    flag = reserch_file(tmp, 1, &i, &j, &k);
    if (flag)
    {
        if (search_file(dir[k].inode))//在文件打开列表中查找文件是否打开
        {
            flag = 0;
            //找到文件打开列表中文件的下标
            while (fopen_table[flag] != dir[k].inode)
                flag++;
            
            //关闭文件
            fopen_table[flag] = 0;
            printf("File: %s! closed\n", tmp);
        }
        else
            printf("The file %s has not been opened!\n", tmp);
    }
    else
        printf("The file %s does not exist!\n", tmp);
}

void attrib_file(char tmp[9]) 
{// 改变文件读写权限
    unsigned short flag, i, j, k;
    // 查找目录文件名，i表示的是文件对应的索引块号
    // j是在上级目录中是第几块数据块
    // k是这个数据块中是第几个目录项
    flag = reserch_file(tmp, 1, &i, &j, &k);
    if (flag)
    {
        int choose;
        //将当前文件的索引信息加载到inode_area[0]中
        reload_inode_entry(i);
        printf("Select what do you want to change\n");
        printf("1.r\n");
        printf("2.w\n");
        printf("3.x\n");
        printf("4.rw\n");
        printf("5.rx\n");
        printf("6.wx\n");
        printf("7.rwx\n");
        scanf("%d", &choose);
        switch (choose)
        {
        case 1:
            inode_area[0].i_mode = 4;
            break;
        case 2:
            inode_area[0].i_mode = 2;
            break;
        case 3:
            inode_area[0].i_mode = 1;
            break;
        case 4:
            inode_area[0].i_mode = 6;
            break;
        case 5:
            inode_area[0].i_mode = 5;
            break;
        case 6:
            inode_area[0].i_mode = 3;
            break;
        case 7:
            inode_area[0].i_mode = 7;
            break;
        }
        update_inode_entry(i);
    }
    else
        printf("The file %s does not exist!\n", tmp);
}

void open_file(char tmp[9])
{//打开文件
    unsigned short flag, i, j, k;
    // 查找目录文件名，i表示的是文件对应的索引块号
    // j是在上级目录中是第几块数据块
    // k是这个数据块中是第几个目录项
    flag = reserch_file(tmp, 1, &i, &j, &k);
    if (flag)
    {
        if (search_file(dir[k].inode))//如果文件已经打开
            printf("The file %s has opened!\n", tmp);
        else
        {
            flag = 0;
            //找到文件打开表的下一个空缺
            while (fopen_table[flag])
                flag++;
            fopen_table[flag] = dir[k].inode;
            printf("File %s! opened\n", tmp);
        }
    }
    else
        printf("The file %s does not exist!\n", tmp);
}
/*********************************    format   ******************************************/
void initialize_disk()
{
    // 初始格式化磁盘
    int i = 0;
    printf("Creating the ext2 file system\n");
    printf("Please wait ");
    while (i < 1)
    {
        printf("... ");
        // sleep(1);
        i++;
    }
    printf("\n");
    //最近分配的索引结点和数据块都初始化
    last_alloc_inode = 1;
    last_alloc_block = 0;
    for (i = 0; i < 16; i++)
        fopen_table[i] = 0; // 清空文件打开表
    for (i = 0; i < BLOCK_SIZE; i++)
        Buffer[i] = 0; // 清空缓冲区，通过缓冲区清空文件，即清空磁盘
    fp = fopen("FS_zqw_zzw.txt", "w+b");
    fseek(fp, DISK_START, SEEK_SET);
    for (i = 0; i < 4611; i++)
        fwrite(Buffer, BLOCK_SIZE, 1, fp); // 清空文件，即清空磁盘全部用0填充
    reload_group_desc();                   // 载入组描述符到super_block[0]
    reload_inode_entry(1);                 // 载入第1个inode到inode_area[0]
    reload_dir(0);                         // 载入第0个目录到dir[32]中
    strcpy(current_path, "[root@ C:/");    // 改路径名

    // 初始化组描述符内容
    strcpy(super_block[0].bg_volume_name, "EXT2FS"); // 改卷名，初始化组描述符内容
    super_block[0].bg_block_bitmap = BLOCK_BITMAP;   // 初始化组描述符块位图块号（位置）
    super_block[0].bg_inode_bitmap = INODE_BITMAP;   // 初始化索引节点位图块号（位置）
    super_block[0].bg_inode_table = INODE_TABLE;     // 初始化索引节点表（位置）
    super_block[0].bg_free_blocks_count = 4096;      // 初始化本组空闲块个数
    super_block[0].bg_free_inodes_count = 4096;      // 初始化本组空闲索引节点个数
    super_block[0].bg_used_dirs_count = 0;           // 初始化本组目录个数
    update_group_desc();                             // 更新组描述符内容

    reload_block_bitmap(); // 载入block位图,将存储的数据块位图信息存入位图缓冲区bitbuf
    reload_inode_bitmap(); // 载入inode位图,将存储的索引节点信息存入位图缓冲区

    //根目录仍然有. 和 ..两个目录文件
    inode_area[0].i_mode = 518;               // 文件类型及访问权限
    inode_area[0].i_blocks = 0;               // 文件的数据块个数
    inode_area[0].i_size = 32;                // 节点大小
    inode_area[0].i_atime = 0;                // 节点访问时间
    inode_area[0].i_ctime = 0;                // 节点创建时间
    inode_area[0].i_mtime = 0;                // 节点修改时间
    inode_area[0].i_dtime = 0;                // 节点删除时间
    inode_area[0].i_block[0] = alloc_block(); // 分配一个数据块，返回一个数据块号（初始化的0）

    inode_area[0].i_blocks++;
    current_dir = get_inode();       // 根据last_alloc_inode分配一个inode（初始化的1）
    update_inode_entry(current_dir); // 将结点缓冲area_inode[0]更新到磁盘中

    dir[0].inode = dir[1].inode = current_dir; // 在根目录中. 和.. 的索引结点号都是当前目录本身
    dir[0].name_len = 0;                       // 文件名长度
    dir[1].name_len = 0;                       // 文件名长度
    dir[0].file_type = dir[1].file_type = 2;   // 1:文件;2:目录
    strcpy(dir[0].name, ".");                  // 创建当前目录.
    strcpy(dir[1].name, "..");                 // 创建上一级目录..
    update_dir(inode_area[0].i_block[0]);//将dir更新到数据块中，inode_area[0].i_block[0]的内容是第一个数据块的块号
    printf("The ext2 file system has been installed!\n");
}

void initialize_memory() // 初始化内存
{
    int i = 0;

    //  最近分配的节点号
    last_alloc_inode = 1;
    //  最近分配的数据块号
    last_alloc_block = 0;
    for (i = 0; i < 16; i++)
        fopen_table[i] = 0; // 将文件打开表所有值初始化为0
    // 初始化当前的路径
    strcpy(current_path, "[root@ C:/");
    current_dir = 1;
    fp = fopen("FS_zqw_zzw.txt", "r+b");
    if (fp == NULL)
    {
        printf("The File system does not exist!\n");
        //初始化磁盘
        initialize_disk();
        return;
    }
    reload_group_desc(); // 重载磁盘内容，载入组描述符，从文件中读出存入superblock
}

void format()
{//格式化
    initialize_disk();
    initialize_memory();
}

void help()
{
    printf("   ***************************************************************************\n");
    printf("   *                   An simulation of ext2 file system                     *\n");
    printf("   *                                                                         *\n");
    printf("   * The available commands are:                                             *\n");
    printf("   * 1.change dir   : cd+dir_name          8.create dir  : mkdir+dir_name    *\n");
    printf("   * 2.create file  : mkf+file_name        9.delete dir  : rmdir+dir_name    *\n");
    printf("   * 3.delete file  : rm+file_name         10.read  file : read+file_name    *\n");
    printf("   * 4.open   file  : open+file_name       11.write file : write+file_name   *\n");
    printf("   * 5.close  file  : #                    12.logoff     : quit              *\n");
    printf("   * 6.list   items : ls                   13.this  menu : help              *\n");
    printf("   * 7.format disk  : format               14.attribute  : attrib+file_name  *\n");
    printf("   ***************************************************************************\n");
}

/****************************************   main   ***************************************************/
// argc代表的是参数的数量，至少为1（argv[0]即.exe文件的路径)。argv为指针表示的参数,argv[0]表示第一个参数，argv[1]表示第二个参数，以此类推。
// 命令行参数在程序开始运行的时候传递给程序。
int main(char argc, char **argv)
{
    char command[10], temp[9];
    initialize_memory();
    while (1)
    {
        // 输出当前路径
        printf("%s]#", current_path);
        // 读入命令
        scanf("%s", command);
        if (!strcmp(command, "cd"))
        {
            scanf("%s", temp);
            cd(temp);
        }
        else if (!strcmp(command, "mkdir"))
        {
            scanf("%s", temp);
            mkdir(temp, 2);
        }
        else if (!strcmp(command, "mkf"))
        {
            scanf("%s", temp);
            mkdir(temp, 1);
        }
        else if (!strcmp(command, "rmdir"))
        {
            scanf("%s", temp);
            rmdir(temp);
        }
        else if (!strcmp(command, "rm"))
        {
            scanf("%s", temp);
            del(temp);
        }
        else if (!strcmp(command, "open"))
        {
            scanf("%s", temp);
            open_file(temp);
        }
        else if (!strcmp(command, "close"))
        {
            scanf("%s", temp);
            close_file(temp);
        }
        else if (!strcmp(command, "read"))
        {
            scanf("%s", temp);
            read_file(temp);
        }
        else if (!strcmp(command, "write"))
        {
            scanf("%s", temp);
            write_file(temp);
        }
        else if (!strcmp(command, "attrib"))
        {
            scanf("%s", temp);
            attrib_file(temp);
        }
        else if (!strcmp(command, "ls"))
            ls();
        else if (!strcmp(command, "format"))
        {
            char tempch;
            printf("Format will erase all the data in the Disk\n");
            printf("Are you sure?y/n:\n");
            scanf(" %c", &tempch);
            if (tempch == 'Y' || tempch == 'y')
            {
                fclose(fp);
                initialize_disk();
            }
            else
                printf("Format Disk canceled\n");
        }

        else if (!strcmp(command, "help"))
            help();
        else if (!strcmp(command, "quit"))
            break;
        else
            printf("No this Command,Please check!\n");
    }
    return 0;
}
