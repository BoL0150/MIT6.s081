// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12];
  ushort type;      // 文件类型
  ushort machine;   // 机器名称
  uint version;
  uint64 entry;     // 文件装入内存中的起始地址
  uint64 phoff;     // program header offset 程序头表在文件中的偏移地址
  uint64 shoff;     // section header offset 节头表在文件中的偏移地址
  uint flags;       
  ushort ehsize;    // elf header size 当前的elf头的大小
  ushort phentsize; // program header entry size 程序头表的每个表项的大小
  ushort phnum;     // program header nunber 程序头表一共有多少个表项
  ushort shentsize; // section header entry size 节头表的每个表项的大小
  ushort shnum;     // section header number 节头表一共有多少表项
  ushort shstrndx;  // section header string table index .strtab在节头表中的索引
};

// Program section header
struct proghdr {
  uint32 type;
  uint32 flags;     // 该节加载到内存中的标志位
  uint64 off;       // 在文件中的偏移地址
  uint64 vaddr;     // 加载到内存中的虚拟地址
  uint64 paddr;     
  uint64 filesz;    // 该节在文件中的大小
  uint64 memsz;     // 该节加载到内存后，在内存中的大小
  uint64 align;     // 按照多少字节对齐
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
