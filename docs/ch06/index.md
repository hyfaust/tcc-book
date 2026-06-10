# 第六章 链接器与 ELF 处理

链接器是编译器工具链中最关键的组件之一。它的职责是将多个独立编译的目标文件（object files）合并为一个可执行程序或共享库。TinyCC 的链接器实现在 `tccelf.c` 中，针对 x86-64 平台的重定位处理在 `x86_64-link.c` 中，而 JIT 运行时支持则在 `tccrun.c` 中。本章将逐层剖析 TinyCC 链接器的设计与实现。

---

## 6.1 ELF 文件格式详解

ELF（Executable and Linkable Format）是 UNIX 系统上可执行文件、目标文件和共享库的标准格式。理解 ELF 格式是理解链接器的前提。ELF 文件由三个核心部分组成：ELF 头、程序头表（Program Header Table）和节头表（Section Header Table）。

### 6.1.1 ELF 头（ELF Header）

ELF 头位于文件的最开始，大小固定为 64 字节（64 位格式）。在 TinyCC 中，其定义位于 `elf.h`：

```c
typedef struct {
  unsigned char e_ident[EI_NIDENT]; /* 魔数和其他信息 */
  Elf64_Half    e_type;             /* 目标文件类型 */
  Elf64_Half    e_machine;          /* 体系结构 */
  Elf64_Word    e_version;          /* 目标文件版本 */
  Elf64_Addr    e_entry;            /* 入口点虚拟地址 */
  Elf64_Off     e_phoff;            /* 程序头表文件偏移 */
  Elf64_Off     e_shoff;            /* 节头表文件偏移 */
  Elf64_Word    e_flags;            /* 处理器特定标志 */
  Elf64_Half    e_ehsize;           /* ELF 头大小（字节） */
  Elf64_Half    e_phentsize;        /* 程序头表条目大小 */
  Elf64_Half    e_phnum;            /* 程序头表条目数量 */
  Elf64_Half    e_shentsize;        /* 节头表条目大小 */
  Elf64_Half    e_shnum;            /* 节头表条目数量 */
  Elf64_Half    e_shstrndx;         /* 节头字符串表索引 */
} Elf64_Ehdr;
```

**字节布局（x86-64，Little-endian）：**

```
偏移  大小  字段            说明
0x00  16    e_ident         7f 45 4c 46 02 01 01 00 ... (魔数 + 类别 + 编码 + 版本 + OS/ABI)
0x10  2     e_type          01 00 = ET_REL (可重定位), 02 00 = ET_EXEC, 03 00 = ET_DYN
0x12  2     e_machine       3e 00 = EM_X86_64 (62)
0x14  4     e_version       01 00 00 00 = EV_CURRENT
0x18  8     e_entry         入口点地址（.o 文件为 0）
0x20  8     e_phoff         程序头表偏移（.o 文件通常为 0）
0x28  8     e_shoff         节头表偏移
0x30  4     e_flags         处理器标志
0x34  2     e_ehsize        40 00 = 64 字节
0x36  2     e_phentsize     38 00 = 56 字节
0x38  2     e_phnum         程序头数量
0x3a  2     e_shentsize     40 00 = 64 字节
0x3c  2     e_shnum         节头数量
0x3e  2     e_shstrndx      节名字符串表的索引
```

`e_ident` 数组的前 4 字节是魔数 `\x7fELF`，用于标识 ELF 文件。第 5 字节（`EI_CLASS`）区分 32 位（`ELFCLASS32=1`）和 64 位（`ELFCLASS64=2`）。第 6 字节（`EI_DATA`）指定字节序：`ELFDATA2LSB=1`（小端）、`ELFDATA2MSB=2`（大端）。

在 TinyCC 中，通过宏 `ELFCLASSW` 根据目标平台的指针大小自动选择 ELF32 或 ELF64：

```c
#if PTR_SIZE == 8
# define ELFCLASSW ELFCLASS64
# define ElfW(type) Elf##64##_##type
#else
# define ELFCLASSW ELFCLASS32
# define ElfW(type) Elf##32##_##type
#endif
```

### 6.1.2 程序头（Program Header）

程序头描述了一个段（Segment），告诉操作系统如何将文件映射到内存。程序头仅在可执行文件和共享库中有意义，在可重定位目标文件（`.o`）中通常为空。

```c
typedef struct {
  Elf64_Word  p_type;    /* 段类型 */
  Elf64_Word  p_flags;   /* 段标志 */
  Elf64_Off   p_offset;  /* 文件偏移 */
  Elf64_Addr  p_vaddr;   /* 虚拟地址 */
  Elf64_Addr  p_paddr;   /* 物理地址 */
  Elf64_Xword p_filesz;  /* 文件中大小 */
  Elf64_Xword p_memsz;   /* 内存中大小 */
  Elf64_Xword p_align;   /* 对齐 */
} Elf64_Phdr;
```

常见的段类型包括：

| 类型 | 值 | 说明 |
|------|-----|------|
| `PT_NULL` | 0 | 未使用 |
| `PT_LOAD` | 1 | 可加载段 |
| `PT_DYNAMIC` | 2 | 动态链接信息 |
| `PT_INTERP` | 3 | 程序解释器路径 |
| `PT_GNU_RELRO` | 0x6474e552 | 重定位后只读 |
| `PT_GNU_STACK` | 0x6474e551 | 栈可执行性标记 |

### 6.1.3 节头（Section Header）

节头描述文件中的一个节（Section）。与段面向运行时不同，节面向链接过程。

```c
typedef struct {
  Elf64_Word  sh_name;      /* 节名（字符串表索引） */
  Elf64_Word  sh_type;      /* 节类型 */
  Elf64_Xword sh_flags;     /* 节标志 */
  Elf64_Addr  sh_addr;      /* 虚拟地址 */
  Elf64_Off   sh_offset;    /* 文件偏移 */
  Elf64_Xword sh_size;      /* 节大小 */
  Elf64_Word  sh_link;      /* 关联节索引 */
  Elf64_Word  sh_info;      /* 附加信息 */
  Elf64_Xword sh_addralign; /* 对齐 */
  Elf64_Xword sh_entsize;   /* 条目大小 */
} Elf64_Shdr;
```

**关键节类型（`sh_type`）：**

| 类型 | 值 | 说明 |
|------|-----|------|
| `SHT_NULL` | 0 | 未使用 |
| `SHT_PROGBITS` | 1 | 程序数据（代码、数据） |
| `SHT_SYMTAB` | 2 | 符号表 |
| `SHT_STRTAB` | 3 | 字符串表 |
| `SHT_RELA` | 4 | 含 addend 的重定位表 |
| `SHT_HASH` | 5 | 符号哈希表 |
| `SHT_DYNAMIC` | 6 | 动态链接信息 |
| `SHT_NOBITS` | 8 | BSS 节（不占文件空间） |
| `SHT_REL` | 9 | 不含 addend 的重定位表 |
| `SHT_DYNSYM` | 11 | 动态符号表 |

**关键节标志（`sh_flags`）：**

| 标志 | 值 | 说明 |
|------|-----|------|
| `SHF_WRITE` | 0x1 | 可写 |
| `SHF_ALLOC` | 0x2 | 运行时占用内存 |
| `SHF_EXECINSTR` | 0x4 | 可执行 |
| `SHF_TLS` | 0x400 | 线程局部存储 |

TinyCC 还定义了两个内部标志，不出现在 ELF 文件中：

```c
#define SHF_PRIVATE  0x80000000  /* 不参与链接输出的私有节 */
#define SHF_DYNSYM   0x40000000  /* 标记节为动态符号表 */
```

### 6.1.4 段与节的关系

一个 ELF 文件中，多个节可以映射到同一个段。典型的映射关系：

```
PT_LOAD (RX):  .interp .dynsym .dynstr .hash .gnu.hash .rela.plt .plt .text
PT_LOAD (RW):  .dynamic .got .got.plt .data .bss
PT_DYNAMIC:    .dynamic
PT_GNU_RELRO:  .dynamic .got
```

TinyCC 的 `sort_sections()` 函数（6.10 节详解）负责决定节的排列顺序，`layout_sections()` 函数根据排列结果生成程序头。

---

## 6.2 Section 管理

TinyCC 在内存中用 `Section` 结构体表示 ELF 节。这个结构体是链接器的核心数据结构。

### 6.2.1 Section 结构体

定义在 `tcc.h` 第 561 行：

```c
typedef struct Section {
    unsigned long data_offset;     /* 当前数据偏移 */
    unsigned char *data;           /* 节数据 */
    unsigned long data_allocated;  /* 已分配空间（用于 realloc） */
    TCCState *s1;                  /* 所属编译状态 */
    int sh_name;                   /* ELF 节名（仅输出时使用） */
    int sh_num;                    /* ELF 节编号 */
    int sh_type;                   /* ELF 节类型 */
    int sh_flags;                  /* ELF 节标志 */
    int sh_info;                   /* ELF 节信息 */
    int sh_addralign;              /* ELF 节对齐 */
    int sh_entsize;                /* 条目大小 */
    unsigned long sh_size;         /* 节大小（仅输出时使用） */
    addr_t sh_addr;                /* 重定位后的虚拟地址 */
    unsigned long sh_offset;       /* 文件偏移 */
    int nb_hashed_syms;            /* 哈希表中的符号数 */
    struct Section *link;          /* 关联节（如符号表关联字符串表） */
    struct Section *reloc;         /* 对应的重定位节 */
    struct Section *hash;          /* 符号哈希表节 */
    struct Section *prev;          /* 节栈上的前一个节 */
    char name[1];                  /* 节名（柔性数组） */
} Section;
```

注意 `name[1]` 是 C 语言中常用的"柔性数组"技巧：`Section` 结构体在分配时会额外分配 `strlen(name)` 字节，将节名直接存储在结构体末尾，避免额外的指针间接访问。

### 6.2.2 创建新节：`new_section()`

`new_section()` 是创建节的核心函数，位于 `tccelf.c` 第 229 行：

```c
ST_FUNC Section *new_section(TCCState *s1, const char *name,
                             int sh_type, int sh_flags)
{
    Section *sec;
    sec = tcc_mallocz(sizeof(Section) + strlen(name));
    sec->s1 = s1;
    strcpy(sec->name, name);
    sec->sh_type = sh_type;
    sec->sh_flags = sh_flags;
    switch(sh_type) {
    case SHT_GNU_versym:
        sec->sh_addralign = 2;
        break;
    case SHT_HASH: case SHT_GNU_HASH:
    case SHT_REL:  case SHT_RELA:
    case SHT_DYNSYM: case SHT_SYMTAB:
    case SHT_DYNAMIC: case SHT_GNU_verneed: case SHT_GNU_verdef:
        sec->sh_addralign = PTR_SIZE;
        break;
    case SHT_STRTAB:
        sec->sh_addralign = 1;
        break;
    default:
        sec->sh_addralign = PTR_SIZE;
        break;
    }
    if (sh_flags & SHF_PRIVATE) {
        dynarray_add(&s1->priv_sections, &s1->nb_priv_sections, sec);
    } else {
        sec->sh_num = s1->nb_sections;
        dynarray_add(&s1->sections, &s1->nb_sections, sec);
    }
    return sec;
}
```

关键设计点：

1. **私有节 vs 公共节**：带 `SHF_PRIVATE` 标志的节不会输出到 ELF 文件，仅用于编译器内部的临时数据（如私有符号表 `.dynsymtab`）。
2. **自动对齐**：根据节类型自动设置默认对齐，符号表和哈希表按指针大小对齐，字符串表按 1 字节对齐。
3. **零初始化**：`tcc_mallocz` 确保所有字段初始为零。

### 6.2.3 节数据操作

TinyCC 提供三个函数操作节中的数据：

**`section_add()`**（第 312 行）——按对齐预留空间：

```c
ST_FUNC size_t section_add(Section *sec, addr_t size, int align)
{
    size_t offset, offset1;
    offset = (sec->data_offset + align - 1) & -align;
    offset1 = offset + size;
    if (sec->sh_type != SHT_NOBITS && offset1 > sec->data_allocated)
        section_realloc(sec, offset1);
    sec->data_offset = offset1;
    if (align > sec->sh_addralign)
        sec->sh_addralign = align;
    return offset;
}
```

对齐计算 `(sec->data_offset + align - 1) & -align` 是经典的向上对齐公式。对于 `SHT_NOBITS` 类型的节（如 `.bss`），只更新偏移不分配内存，因为 BSS 节在文件中不占空间。

**`section_ptr_add()`**（第 325 行）——预留空间并返回指针：

```c
ST_FUNC void *section_ptr_add(Section *sec, addr_t size)
{
    size_t offset = section_add(sec, size, 1);
    return sec->data + offset;
}
```

**`section_realloc()`**（第 298 行）——按指数增长策略扩容：

```c
ST_FUNC void section_realloc(Section *sec, unsigned long new_size)
{
    unsigned long size;
    unsigned char *data;
    size = sec->data_allocated;
    if (size == 0) size = 1;
    while (size < new_size) size = size * 2;
    data = tcc_realloc(sec->data, size);
    memset(data + sec->data_allocated, 0, size - sec->data_allocated);
    sec->data = data;
    sec->data_allocated = size;
}
```

指数增长（每次翻倍）确保了摊还 O(1) 的插入时间复杂度，与标准库 `std::vector` 的策略相同。

### 6.2.4 标准节的创建

在 `tccelf_new()`（第 62 行）中，TinyCC 创建了一组标准节：

```c
text_section    = new_section(s, ".text",    SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
data_section    = new_section(s, ".data",    SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
rodata_section  = new_section(s, ".data.ro", SHT_PROGBITS, SHF_ALLOC);
bss_section     = new_section(s, ".bss",     SHT_NOBITS,   SHF_ALLOC | SHF_WRITE);
tdata_section   = new_section(s, ".tdata",   SHT_PROGBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS);
tbss_section    = new_section(s, ".tbss",    SHT_NOBITS,   SHF_ALLOC | SHF_WRITE | SHF_TLS);
common_section  = new_section(s, ".common",  SHT_NOBITS,   SHF_PRIVATE);
common_section->sh_num = SHN_COMMON;
```

注意：
- `.data.ro` 是 TinyCC 对只读数据节的命名（区别于标准的 `.rodata`），使用 `SHF_ALLOC` 但不含 `SHF_WRITE`。
- `.common` 是一个私有节，用于收集 COMMON 符号，其 `sh_num` 被设为 `SHN_COMMON`（0xfff2）。
- TLS 节（`.tdata`/`.tbss`）具有 `SHF_TLS` 标志。

---

## 6.3 ELF 符号

符号（Symbol）是链接的基本单位。函数名、全局变量名、节名等都通过符号表来表示。

### 6.3.1 Elf64_Sym 结构体

```c
typedef struct {
  Elf64_Word    st_name;   /* 符号名（字符串表索引） */
  unsigned char st_info;   /* 符号类型和绑定 */
  unsigned char st_other;  /* 符号可见性 */
  Elf64_Section st_shndx;  /* 节索引 */
  Elf64_Addr    st_value;  /* 符号值 */
  Elf64_Xword   st_size;   /* 符号大小 */
} Elf64_Sym;
```

注意 64 位版本与 32 位版本的字段顺序不同——64 位版本将 `st_info` 和 `st_other` 提前，以保证 `st_value` 的 8 字节对齐。

`st_info` 字段编码了两个信息：

```c
#define ELF64_ST_BIND(val)     ((val) >> 4)         /* 高 4 位：绑定 */
#define ELF64_ST_TYPE(val)     ((val) & 0xf)        /* 低 4 位：类型 */
#define ELF64_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xf))
```

### 6.3.2 符号绑定（Binding）

| 绑定 | 值 | 说明 |
|------|-----|------|
| `STB_LOCAL` | 0 | 局部符号，仅在本文件可见 |
| `STB_GLOBAL` | 1 | 全局符号，所有文件可见 |
| `STB_WEAK` | 2 | 弱符号，可被全局符号覆盖 |

TinyCC 在 `set_elf_sym()`（第 700 行）中处理符号冲突的规则：

- **GLOBAL 覆盖 WEAK**：如果新定义是 `STB_GLOBAL` 而已有定义是 `STB_WEAK`，则新定义覆盖旧定义。
- **WEAK 不覆盖 GLOBAL**：反之则忽略。
- **两个 WEAK**：保留先定义的。
- **数据覆盖 COMMON/BSS**：如果已有符号在 `SHN_COMMON` 或 BSS 节，而新符号在其他节，则新定义优先。
- **两个 GLOBAL**：报错 "defined twice"。

### 6.3.3 符号类型（Type）

| 类型 | 值 | 说明 |
|------|-----|------|
| `STT_NOTYPE` | 0 | 未指定类型 |
| `STT_OBJECT` | 1 | 数据对象（变量） |
| `STT_FUNC` | 2 | 函数 |
| `STT_SECTION` | 3 | 与节关联的符号 |
| `STT_FILE` | 4 | 源文件名 |
| `STT_COMMON` | 5 | COMMON 数据 |
| `STT_TLS` | 6 | 线程局部存储 |

### 6.3.4 特殊节索引

| 索引 | 值 | 说明 |
|------|-----|------|
| `SHN_UNDEF` | 0 | 未定义（外部引用） |
| `SHN_ABS` | 0xfff1 | 绝对值，不参与重定位 |
| `SHN_COMMON` | 0xfff2 | COMMON 符号 |

### 6.3.5 符号可见性（Visibility）

`st_other` 的低 2 位编码可见性：

```c
#define STV_DEFAULT   0  /* 默认规则 */
#define STV_INTERNAL  1  /* 处理器特定的隐藏类 */
#define STV_HIDDEN    2  /* 其他模块不可见 */
#define STV_PROTECTED 3  /* 不可抢占，但导出 */
```

可见性的传播规则在 `set_elf_sym()` 中实现：取两者中更严格的可见性。

### 6.3.6 符号表操作

**`put_elf_sym()`**——插入新符号到符号表：

```c
ST_FUNC int put_elf_sym(Section *s, addr_t value, unsigned long size,
    int info, int other, int shndx, const char *name)
```

它将符号追加到符号表节的数据中，并更新关联的哈希表。哈希表在负载因子超过 2 时自动重建（`rebuild_hash()`）。

**`find_elf_sym()`**——按名称查找符号：

```c
ST_FUNC int find_elf_sym(Section *s, const char *name)
```

通过 ELF 哈希函数定位桶，然后遍历链表查找。哈希函数定义在 `tccelf.c` 第 389 行：

```c
static ElfW(Word) elf_hash(const unsigned char *name)
{
    ElfW(Word) h = 0, g;
    while (*name) {
        h = (h << 4) + *name++;
        g = h & 0xf0000000;
        if (g) h ^= g >> 24;
        h &= ~g;
    }
    return h;
}
```

**`set_elf_sym()`**——添加或更新符号（处理冲突）：

```c
ST_FUNC int set_elf_sym(Section *s, addr_t value, unsigned long size,
                       int info, int other, int shndx, const char *name)
```

这是最高层的符号操作函数，实现了 6.3.2 节描述的冲突解决规则。

---

## 6.4 重定位

重定位（Relocation）是链接器的核心功能。当编译器生成目标文件时，它无法知道外部符号或跨节引用的最终地址，因此在指令中留下"占位符"，并生成重定位条目告诉链接器如何修补这些地址。

### 6.4.1 重定位条目结构

x86-64 使用带 addend 的 `Elf64_Rela`：

```c
typedef struct {
  Elf64_Addr   r_offset;  /* 需要修补的位置 */
  Elf64_Xword  r_info;    /* 符号索引和重定位类型 */
  Elf64_Sxword r_addend;  /* 加数 */
} Elf64_Rela;
```

`r_info` 的编码：

```c
#define ELF64_R_SYM(i)       ((i) >> 32)          /* 高 32 位：符号索引 */
#define ELF64_R_TYPE(i)      ((i) & 0xffffffff)   /* 低 32 位：重定位类型 */
#define ELF64_R_INFO(sym,type) ((((Elf64_Xword)(sym)) << 32) + (type))
```

TinyCC 通过宏统一处理 32/64 位：

```c
#if PTR_SIZE == 8
# define ElfW_Rel ElfW(Rela)
# define SHT_RELX SHT_RELA
#else
# define ElfW_Rel ElfW(Rel)
# define SHT_RELX SHT_REL
#endif
```

### 6.4.2 x86-64 重定位类型

在 `x86_64-link.c` 中定义了关键的重定位类型常量：

```c
#define R_DATA_32   R_X86_64_32S    /* 32 位有符号数据重定位 */
#define R_DATA_PTR  R_X86_64_64     /* 64 位指针重定位 */
#define R_JMP_SLOT  R_X86_64_JUMP_SLOT
#define R_GLOB_DAT  R_X86_64_GLOB_DAT
#define R_COPY      R_X86_64_COPY
#define R_RELATIVE  R_X86_64_RELATIVE
```

**常用重定位类型的语义：**

| 类型 | 值 | 计算公式 | 用途 |
|------|-----|---------|------|
| `R_X86_64_64` | 1 | `S + A` | 绝对 64 位地址（数据引用） |
| `R_X86_64_PC32` | 2 | `S + A - P` | PC 相对 32 位（本地调用） |
| `R_X86_64_32` | 10 | `S + A` | 绝对 32 位无符号 |
| `R_X86_64_32S` | 11 | `S + A` | 绝对 32 位有符号 |
| `R_X86_64_GOTPCREL` | 9 | `G + GOT + A - P` | PC 相对 GOT 条目 |
| `R_X86_64_PLT32` | 4 | `L + A - P` | PC 相对 PLT 条目 |
| `R_X86_64_GLOB_DAT` | 6 | `S` | GOT 条目初始化 |
| `R_X86_64_JUMP_SLOT` | 7 | `S` | PLT 条目初始化 |
| `R_X86_64_RELATIVE` | 8 | `B + A` | 基址相对（动态） |
| `R_X86_64_COPY` | 5 | — | 数据复制重定位 |

其中 `S` = 符号值，`A` = addend，`P` = 重定位位置，`G` = GOT 偏移，`L` = PLT 条目地址，`B` = 基地址。

### 6.4.3 `relocate()` 函数

`x86_64-link.c` 第 201 行的 `relocate()` 函数是重定位的核心实现。它根据重定位类型对目标位置进行不同的修补：

```c
ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type,
                      unsigned char *ptr, addr_t addr, addr_t val)
{
    switch (type) {
    case R_X86_64_64:
        /* 对于 DLL 输出：生成 R_RELATIVE 或保留动态重定位 */
        if (s1->output_type & TCC_OUTPUT_DYN) { ... }
        add64le(ptr, val);           /* *ptr += val (64位) */
        break;
    case R_X86_64_32:
    case R_X86_64_32S:
        /* 溢出检查：32S 要求 val == (int)val */
        add32le(ptr, val);           /* *ptr += val (32位) */
        break;
    case R_X86_64_PC32:
    case R_X86_64_PLT32:
        diff = (long long)val - addr;
        add32le(ptr, diff);          /* *ptr += (val - addr) */
        break;
    case R_X86_64_GOTPCREL:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_REX_GOTPCRELX:
        add32le(ptr, s1->got->sh_addr - addr +
                     get_sym_attr(s1, sym_index, 0)->got_offset - 4);
        break;
    case R_X86_64_GLOB_DAT:
    case R_X86_64_JUMP_SLOT:
        write64le(ptr, val - rel->r_addend);
        break;
    /* ... TLS 重定位省略 ... */
    }
}
```

**关键设计**：
- 对于 `R_X86_64_64` 在 DLL 输出中，TinyCC 不直接写入绝对地址，而是生成 `R_RELATIVE` 动态重定位条目，让动态链接器在加载时修补。
- `R_X86_64_GOTPCREL` 的计算涉及 GOT 表基址和符号的 GOT 偏移，实现对 GOT 的 PC 相对引用。
- TLS 重定位（`R_X86_64_TLSGD`、`R_X86_64_TLSLD`）包含指令序列的模式匹配和替换优化。

### 6.4.4 重定位表的创建

`put_elf_reloca()`（第 794 行）负责向节添加重定位条目：

```c
ST_FUNC void put_elf_reloca(Section *symtab, Section *s, unsigned long offset,
                            int type, int symbol, addr_t addend)
{
    Section *sr = s->reloc;
    if (!sr) {
        char buf[256];
        snprintf(buf, sizeof(buf), REL_SECTION_FMT, s->name);
        sr = new_section(s->s1, buf, SHT_RELX, symtab->sh_flags);
        sr->sh_entsize = sizeof(ElfW_Rel);
        sr->link = symtab;
        sr->sh_info = s->sh_num;
        s->reloc = sr;
    }
    ElfW_Rel *rel = section_ptr_add(sr, sizeof(ElfW_Rel));
    rel->r_offset = offset;
    rel->r_info = ELFW(R_INFO)(symbol, type);
    rel->r_addend = addend;
}
```

注意重定位节的命名约定：对于 64 位是 `.rela.text`（`REL_SECTION_FMT` = `".rela%s"`），对于 32 位是 `.rel.text`。重定位节通过 `sh_info` 字段关联到被重定位的节。

---

## 6.5 目标文件加载

`tcc_load_object_file()`（`tccelf.c` 第 3260 行）负责加载 `.o` 文件并将其内容合并到当前编译状态中。这是链接器处理输入文件的核心函数。

### 6.5.1 加载流程

函数的执行分为五个阶段：

**阶段 1：验证 ELF 头**

```c
lseek(fd, file_offset, SEEK_SET);
if (tcc_object_type(fd, &ehdr) != AFF_BINTYPE_REL)
    goto invalid;
if (ehdr.e_ident[5] != ELFDATA2LSB ||
    ehdr.e_machine != EM_TCC_TARGET) {
    return tcc_error_noabort("invalid object file");
}
```

验证文件是可重定位目标文件（`ET_REL`），字节序为小端，机器类型与目标平台匹配。

**阶段 2：加载节头和符号表**

```c
shdr = load_data(fd, file_offset + ehdr.e_shoff,
                 sizeof(ElfW(Shdr)) * ehdr.e_shnum);
strsec = load_data(fd, file_offset + sh->sh_offset, sh->sh_size);

for(i = 1; i < ehdr.e_shnum; i++) {
    sh = &shdr[i];
    if (sh->sh_type == SHT_SYMTAB) {
        nb_syms = sh->sh_size / sizeof(ElfW(Sym));
        symtab = load_data(fd, file_offset + sh->sh_offset, sh->sh_size);
        sh = &shdr[sh->sh_link];
        strtab = load_data(fd, file_offset + sh->sh_offset, sh->sh_size);
    }
}
```

**阶段 3：合并节**

对输入文件中的每个节，在当前编译状态中查找同名节。如果找到，将数据追加到现有节；如果未找到，创建新节：

```c
for(j = 1; j < s1->nb_sections; j++) {
    s = s1->sections[j];
    if (strcmp(s->name, sh_name)) continue;
    /* linkonce 节不重复添加 */
    if (!strncmp(sh_name, ".gnu.linkonce", 13)) {
        sm_table[i].link_once = 1;
        goto next;
    }
    goto found;
}
s = new_section(s1, sh_name, sh->sh_type, sh->sh_flags & ~SHF_GROUP);
found:
    offset = section_add(s, size, sh->sh_addralign);
    sm_table[i].offset = offset;
    sm_table[i].s = s;
    if (sh->sh_type != SHT_NOBITS && size) {
        lseek(fd, file_offset + sh->sh_offset, SEEK_SET);
        full_read(fd, s->data + offset, size);
    }
```

**阶段 4：重映射符号**

将输入文件的符号索引映射到合并后的符号表：

```c
for(i = 0, sym = symtab; i < nb_syms; i++, sym++) {
    name = strtab + sym->st_name;
    if (sym->st_shndx == SHN_UNDEF) {
        sym_index = set_elf_sym(symtab_section, 0, 0,
            sym->st_info, other, SHN_UNDEF, name);
    } else if (sym->st_shndx < SHN_LORESERVE) {
        /* 映射节索引并调整符号值 */
        s = sm_table[sym->st_shndx].s;
        sym_index = set_elf_sym(symtab_section,
            sym->st_value + sm_table[sym->st_shndx].offset,
            sym->st_size, sym->st_info, other, s->sh_num, name);
    }
    old_to_new_syms[i] = sym_index;
}
```

**阶段 5：处理重定位**

更新重定位条目中的符号索引，使其指向合并后的符号表：

```c
for_each_elem(sr, 0, rel, ElfW_Rel) {
    sym_index = ELFW(R_SYM)(rel->r_info);
    type = ELFW(R_TYPE)(rel->r_info);
    sym_index = old_to_new_syms[sym_index];
    rel->r_info = ELFW(R_INFO)(sym_index, type);
    rel->r_offset += sm_table[sh->sh_info].offset;
}
```

注意 `rel->r_offset` 需要加上节合并时的偏移量，因为节数据被追加到了现有节的末尾。

---

## 6.6 静态库 .a 加载

静态库（archive）是多个目标文件的打包格式，以 `.a` 为后缀。

### 6.6.1 ar 格式

ar 格式的结构非常简单：

```
!<arch>\n                    # 魔数（8 字节）
ar_header[1]                 # 第一个成员头
member_data[1]               # 第一个成员数据
ar_header[2]                 # 第二个成员头
member_data[2]               # ...
...
```

每个 ar_header 固定 60 字节：

```
偏移  大小  字段
0     16    ar_name   （空格填充，以 '/' 结尾）
16    12    ar_date
28    6     ar_uid
34    6     ar_gid
40    8     ar_mode
48    10    ar_size
58    2     ar_fmag    "`\n"
```

### 6.6.2 `tcc_load_archive()` 函数

`tccelf.c` 第 3656 行：

```c
ST_FUNC int tcc_load_archive(TCCState *s1, int fd, int alacarte)
{
    ArchiveHeader hdr;
    int size, len;
    unsigned long file_offset;
    ElfW(Ehdr) ehdr;

    file_offset = sizeof ARMAG - 1;  /* 跳过魔数 "!<arch>\n" */

    for(;;) {
        len = read_ar_header(fd, file_offset, &hdr);
        if (len == 0) return 0;
        if (len < 0) return tcc_error_noabort("invalid archive");
        file_offset += len;
        size = strtol(hdr.ar_size, NULL, 0);
        if (alacarte) {
            /* COFF 符号表：选择性加载 */
            if (!strcmp(hdr.ar_name, "/"))
                return tcc_load_alacarte(s1, fd, size, 4);
            if (!strcmp(hdr.ar_name, "/SYM64/"))
                return tcc_load_alacarte(s1, fd, size, 8);
        } else if (tcc_object_type(fd, &ehdr) == AFF_BINTYPE_REL) {
            /* 顺序加载：加载每个 .o 成员 */
            if (tcc_load_object_file(s1, fd, file_offset) < 0)
                return -1;
        }
        file_offset = (file_offset + size + 1) & ~1; /* 对齐到偶数 */
    }
}
```

### 6.6.3 选择性加载

ar 格式有两种加载模式：

1. **全量加载（`alacarte=0`）**：按顺序加载库中的所有 `.o` 文件。这是 TinyCC 的默认行为。
2. **选择性加载（`alacarte=1`）**：利用库中的符号表（`/` 或 `/SYM64/` 成员），只加载包含未定义符号引用的 `.o` 文件。`tcc_load_alacarte()` 函数实现了这个逻辑——它读取符号表，检查哪些符号在当前编译状态中是未定义的，然后只加载包含这些符号定义的成员。

选择性加载能显著减少链接时间，尤其是对于大型库。但 TinyCC 的 `alacarte` 参数默认为 0，这意味着它采用全量加载策略，依赖后续的符号解析阶段来处理冲突。

---

## 6.7 共享库 .so 加载

`tcc_load_dll()`（`tccelf.c` 第 3828 行）负责加载共享库（`.so` 文件），提取其中的动态符号信息。

### 6.7.1 加载流程

```c
ST_FUNC int tcc_load_dll(TCCState *s1, int fd,
                         const char *filename, int level)
{
    /* 1. 读取并验证 ELF 头 */
    full_read(fd, &ehdr, sizeof(ehdr));
    if (ehdr.e_ident[5] != ELFDATA2LSB ||
        ehdr.e_machine != EM_TCC_TARGET)
        return tcc_error_noabort("bad architecture");

    /* 2. 加载节头，提取 SHT_DYNAMIC 和 SHT_DYNSYM */
    shdr = load_data(fd, ehdr.e_shoff, ...);
    for(i = 0, sh = shdr; i < ehdr.e_shnum; i++, sh++) {
        switch(sh->sh_type) {
        case SHT_DYNAMIC:
            dynamic = load_data(fd, sh->sh_offset, sh->sh_size);
            break;
        case SHT_DYNSYM:
            dynsym = load_data(fd, sh->sh_offset, sh->sh_size);
            dynstr = load_data(fd, sh1->sh_offset, sh1->sh_size);
            break;
        case SHT_GNU_verdef:  /* 版本定义 */
        case SHT_GNU_verneed: /* 版本需求 */
        case SHT_GNU_versym:  /* 版本符号 */
            ...
        }
    }

    /* 3. 提取 SONAME */
    soname = tcc_basename(filename);
    for(i = 0, dt = dynamic; i < nb_dts; i++, dt++)
        if (dt->d_tag == DT_SONAME)
            soname = dynstr + dt->d_un.d_val;

    /* 4. 检查是否已加载 */
    if (tcc_add_dllref(s1, soname, level)->found)
        goto ret_success;

    /* 5. 导出动态符号到编译状态 */
    for(i = 1, sym = dynsym + 1; i < nb_syms; i++, sym++) {
        if (ELFW(ST_BIND)(sym->st_info) == STB_LOCAL)
            continue;
        name = dynstr + sym->st_name;
        sym_index = set_elf_sym(s1->dynsymtab_section,
            sym->st_value, sym->st_size,
            sym->st_info, sym->st_other, sym->st_shndx, name);
    }
}
```

### 6.7.2 动态段（Dynamic Section）

动态段由一系列 `Elf64_Dyn` 条目组成：

```c
typedef struct {
  Elf64_Sxword d_tag;   /* 条目类型 */
  union {
      Elf64_Xword d_val;
      Elf64_Addr  d_ptr;
  } d_un;
} Elf64_Dyn;
```

常见标签：

| 标签 | 说明 |
|------|------|
| `DT_NEEDED` | 需要的共享库名称 |
| `DT_SONAME` | 本库的名称 |
| `DT_SYMTAB` | 动态符号表地址 |
| `DT_STRTAB` | 字符串表地址 |
| `DT_STRSZ` | 字符串表大小 |
| `DT_HASH` | 符号哈希表地址 |
| `DT_GNU_HASH` | GNU 哈希表地址 |

### 6.7.3 版本符号信息

TinyCC 支持处理 `.gnu.version`、`.gnu.version_r` 和 `.gnu.version_d` 节。这些节用于支持符号版本化（symbol versioning），允许同一符号在不同版本的库中有不同的定义。

`store_version()` 函数解析版本信息并存储在 `sym_versions` 数组中，后续在生成输出文件时会创建对应的 `.gnu.version` 和 `.gnu.version_r` 节。

### 6.7.4 `level` 参数

`level` 参数控制库的引用层级：
- `level = 0`：用户直接指定的库（如 `-lfoo`），需要在输出文件的 `DT_NEEDED` 中记录。
- `level > 0`：被其他库间接引用的库。

---

## 6.8 符号解析

符号解析是将未定义的符号引用与已定义的符号进行匹配的过程。

### 6.8.1 `relocate_syms()` 函数

`relocate_syms()`（`tccelf.c` 第 1079 行）遍历符号表，解析未定义符号：

```c
ST_FUNC void relocate_syms(TCCState *s1, Section *symtab, int do_resolve)
{
    for_each_elem(symtab, 1, sym, ElfW(Sym)) {
        sh_num = sym->st_shndx;
        if (sh_num == SHN_UNDEF) {
            name = (char *) s1->symtab->link->data + sym->st_name;
            if (do_resolve) {
                /* JIT 模式：使用 dlsym() 解析符号 */
                void *addr = dlsym(RTLD_DEFAULT, name_ud);
                if (addr) {
                    sym->st_value = (addr_t) addr;
                    goto found;
                }
            } else if (s1->dynsym && find_elf_sym(s1->dynsym, name)) {
                goto found; /* 动态符号存在，稍后处理 */
            }
            sym_bind = ELFW(ST_BIND)(sym->st_info);
            if (sym_bind == STB_WEAK)
                sym->st_value = 0;  /* 弱符号允许未定义 */
            else
                tcc_error_noabort("unresolved reference to '%s'", name);
        } else if (sh_num < SHN_LORESERVE) {
            /* 已定义符号：加上节基地址 */
            sym->st_value += s1->sections[sym->st_shndx]->sh_addr;
        }
    }
}
```

**符号解析的 `do_resolve` 参数：**

| 值 | 含义 |
|----|------|
| 0 | 普通链接模式，未定义符号报错或交给动态链接器 |
| 1 | JIT 模式，使用 `dlsym()` 在运行时解析 |
| 2 | 重定位动态符号表（`.dynsym`），跳过未定义符号 |

### 6.8.2 COMMON 符号处理

`resolve_common_syms()`（第 1931 行）将 COMMON 符号分配到 BSS 节：

```c
ST_FUNC void resolve_common_syms(TCCState *s1)
{
    ElfW(Sym) *sym;
    for_each_elem(symtab_section, 1, sym, ElfW(Sym)) {
        if (sym->st_shndx == SHN_COMMON) {
            /* st_value 存储的是对齐要求 */
            sym->st_value = section_add(bss_section,
                                        sym->st_size, sym->st_value);
            sym->st_shndx = bss_section->sh_num;
        }
    }
    tcc_add_linker_symbols(s1);
}
```

COMMON 符号是 C 语言中未初始化的全局变量的特殊表示。在 C 语言的传统模型中，不同编译单元中同名的 COMMON 符号会被合并（Tentative Definition 规则）。TinyCC 通过将所有 COMMON 符号收集到 `.common` 节（`sh_num = SHN_COMMON`），然后在链接时统一分配到 `.bss` 节来实现这一语义。

注意对于 `SHN_COMMON` 符号，`st_value` 字段存储的不是地址而是对齐要求。

### 6.8.3 动态符号绑定

对于可执行文件，`bind_exe_dynsyms()`（第 2030 行）将未定义符号与共享库的导出符号进行匹配：

- 如果匹配到 `STT_FUNC` 类型的符号：创建 PLT 条目
- 如果匹配到 `STT_OBJECT` 类型的符号：在 BSS 中分配空间，创建 `R_COPY` 重定位
- 如果未匹配且非 `STB_WEAK`：报错 "unresolved reference"

---

## 6.9 ELF 文件输出

`elf_output_file()`（`tccelf.c` 第 2978 行）是 TinyCC 生成 ELF 可执行文件或共享库的核心函数。它实现了一个精心设计的多阶段管道。

### 6.9.1 五阶段管道

```
阶段 1: 准备与解析
  ├─ tcc_add_runtime()      — 添加运行时库（libtcc1.a, crt*.o）
  ├─ resolve_common_syms()  — COMMON 符号分配到 BSS
  ├─ 创建 .interp 节        — 指定动态链接器路径
  ├─ 创建 .dynsym/.dynstr   — 动态符号表
  ├─ 创建 .dynamic 节       — 动态链接信息
  └─ build_got()            — 初始化 GOT

阶段 2: 符号绑定
  ├─ bind_exe_dynsyms()     — 匹配未定义符号与共享库导出
  ├─ build_got_entries()    — 为需要的符号创建 GOT/PLT 条目
  ├─ bind_libs_dynsyms()    — 导出可执行文件中库需要的符号
  └─ create_gnu_hash()      — 创建 GNU 哈希表

阶段 3: 布局
  ├─ alloc_sec_names()      — 分配节名字符串
  ├─ sort_sections()        — 确定节排列顺序
  ├─ layout_sections()      — 分配虚拟地址，生成程序头
  └─ set_sec_sizes()        — 设置各节的最终大小

阶段 4: 重定位
  ├─ relocate_plt()         — 修补 PLT 中的地址
  ├─ relocate_syms(dynsym)  — 解析动态符号的最终地址
  ├─ relocate_syms(symtab)  — 解析所有符号的最终地址
  ├─ relocate_sections()    — 对所有节执行重定位
  └─ fill_local_got_entries() — 填充本地 GOT 条目

阶段 5: 输出
  ├─ update_gnu_hash()      — 最终化 GNU 哈希表
  ├─ reorder_sections()     — 按排序结果重排节
  ├─ tcc_eh_frame_hdr()     — 生成异常处理头
  └─ tcc_write_elf_file()   — 写入 ELF 文件
```

### 6.9.2 关键代码片段

```c
static int elf_output_file(TCCState *s1, const char *filename)
{
    /* 阶段 1: 准备 */
    tcc_add_runtime(s1);
    resolve_common_syms(s1);
    /* 创建动态链接所需节 */
    interp = new_section(s1, ".interp", SHT_PROGBITS, SHF_ALLOC);
    s1->dynsym = new_symtab(s1, ".dynsym", SHT_DYNSYM, SHF_ALLOC, ...);
    dynamic = new_section(s1, ".dynamic", SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE);
    got_sym = build_got(s1);

    /* 阶段 2: 绑定 */
    bind_exe_dynsyms(s1, file_type & TCC_OUTPUT_DYN);
    build_got_entries(s1, got_sym);
    bind_libs_dynsyms(s1);
    dyninf.gnu_hash = create_gnu_hash(s1);
    version_add(s1);

    /* 阶段 3: 布局 */
    alloc_sec_names(s1, 0);
    sec_order = tcc_malloc(sizeof(int) * 2 * s1->nb_sections);
    layout_sections(s1, sec_order, &dyninf);

    /* 阶段 4: 重定位 */
    write32le(s1->got->data, dynamic->sh_addr);
    relocate_plt(s1);
    relocate_syms(s1, s1->dynsym, 2);
    relocate_syms(s1, s1->symtab, 0);
    relocate_sections(s1);
    fill_local_got_entries(s1);

    /* 阶段 5: 输出 */
    update_gnu_hash(s1, dyninf.gnu_hash);
    reorder_sections(s1, sec_order);
    ret = tcc_write_elf_file(s1, filename, dyninf.phnum, dyninf.phdr);
}
```

### 6.9.3 目标文件输出

对于 `-c` 选项（仅编译不链接），`elf_output_obj()`（第 3156 行）执行一个简化的流程：只分配节名、计算偏移、写入文件，不做符号解析和重定位。

---

## 6.10 Section 排序与布局

### 6.10.1 `sort_sections()` 算法

`sort_sections()`（第 2213 行）决定节在输出文件和内存映像中的排列顺序。排序的核心思想是：将具有相同权限的节放在一起，以最小化程序头数量。

排序使用一个复合键（主键 + 次键），编码为一个整数：

**主键（高字节）——基于权限：**

| 主键值 | 含义 |
|--------|------|
| 0x100 | 只读 + SHF_ALLOC |
| 0x200 | 可写 + SHF_ALLOC |
| 0x400 | TLS + SHF_ALLOC + SHF_WRITE |
| 0x700 | 非 SHF_ALLOC（调试信息等） |
| 0x900 | 无 sh_name，不输出 |

**次键（低字节）——基于节类型：**

| 次键值 | 节类型 |
|--------|--------|
| 0x00 | .interp |
| 0x10 | 符号表 |
| 0x11 | 字符串表 |
| 0x12 | 哈希表 |
| 0x13 | 版本信息 |
| 0x20 | 重定位表 |
| 0x21 | PLT 重定位 |
| 0x30 | 可执行代码 |
| 0x41-0x43 | init/fini 数组 |
| 0x46 | .dynamic |
| 0x47 | .got（RELRO） |
| 0x50 | 数据 |
| 0x60 | .note |
| 0x70 | BSS |

排序完成后，函数计算需要多少个 `PT_LOAD` 段：每当节的权限标志（读/写/执行/TLS）发生变化时，就需要一个新的 `PT_LOAD` 段。

### 6.10.2 `layout_sections()` 地址分配

`layout_sections()`（第 2328 行）根据排序结果为每个节分配虚拟地址：

```c
addr = ELF_START_ADDR;       /* 默认 0x400000 */
if (s1->output_type & TCC_OUTPUT_DYN)
    addr = 0;                /* 共享库从 0 开始 */

for (每个 PT_LOAD 段) {
    /* 对齐到页边界 */
    addr = (addr + s_align - 1) & -s_align;
    ph->p_vaddr = addr;
    ph->p_offset = file_offset;
    for (段中的每个节) {
        addr = (addr + align - 1) & -align;
        s->sh_addr = addr;
        addr += s->sh_size;
    }
}
```

关键点：
- `ELF_START_ADDR` 在 `x86_64-link.c` 中定义为 `0x400000`，这是 x86-64 Linux 的标准加载地址。
- 每个 `PT_LOAD` 段的虚拟地址对齐到 `ELF_PAGE_SIZE`（0x1000）。
- 文件偏移与虚拟地址在页内必须一致（`p_vaddr % page_size == p_offset % page_size`），以满足 `mmap` 的要求。

---

## 6.11 GOT 和 PLT

GOT（Global Offset Table）和 PLT（Procedure Linkage Table）是实现位置无关代码（PIC）和延迟绑定的核心机制。

### 6.11.1 GOT（全局偏移表）

GOT 是一个指针数组，每个条目存储一个全局符号的绝对地址。代码通过 PC 相对寻址访问 GOT，再通过 GOT 中的指针间接访问目标符号。

```
代码中的 GOTPCREL 引用：
    mov  rax, [rip + offset_to_got_entry]  ; 通过 GOT 加载地址
    call rax                                 ; 调用

GOT 内容：
    .got[0]: _DYNAMIC 地址
    .got[1]: 保留（linker）
    .got[2]: 保留（dynamic linker）
    .got[3]: printf 的地址
    .got[4]: global_var 的地址
    ...
```

### 6.11.2 `build_got()` 函数

```c
static int build_got(TCCState *s1)
{
    s1->got = new_section(s1, ".got", SHT_PROGBITS,
                          SHF_ALLOC | SHF_WRITE);
    s1->got->sh_entsize = 4;
    /* 保留 3 个 PTR_SIZE 空间 */
    section_ptr_add(s1->got, 3 * PTR_SIZE);
    return set_elf_sym(symtab_section, 0, 0,
        ELFW(ST_INFO)(STB_GLOBAL, STT_OBJECT),
        0, s1->got->sh_num, "_GLOBAL_OFFSET_TABLE_");
}
```

GOT 的前 3 个条目是保留的：
- `GOT[0]`：`_DYNAMIC` 段的地址
- `GOT[1]`：链接器保留（用于标识）
- `GOT[2]`：动态链接器的解析函数入口（用于延迟绑定）

### 6.11.3 PLT（过程链接表）

PLT 是实现函数调用延迟绑定的关键。每个通过 PLT 调用的函数在 PLT 中有一个 16 字节的条目。

**PLT[0]（公共入口，16 字节）：**

```asm
push  [got + 8]        ; 压入 link_map 标识
jmp   *[got + 16]      ; 跳转到 _dl_runtime_resolve
```

**PLT[n]（每个函数的入口，16 字节）：**

```asm
jmp   *[got + got_offset]  ; 间接跳转（首次调用时指向下面的 push）
push  reloc_index           ; 压入重定位条目索引
jmp   PLT[0]               ; 跳转到公共入口
```

### 6.11.4 `create_plt_entry()` 函数

在 `x86_64-link.c` 第 137 行：

```c
ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset,
                                  struct sym_attr *attr)
{
    Section *plt = s1->plt;
    uint8_t *p;

    /* 首次调用时创建 PLT[0] */
    if (plt->data_offset == 0) {
        p = section_ptr_add(plt, 16);
        p[0] = 0xff; p[1] = 0x35;   /* push *(got+8) */
        write32le(p + 2, PTR_SIZE);
        p[6] = 0xff; p[7] = 0x25;   /* jmp *(got+16) */
        write32le(p + 8, PTR_SIZE * 2);
    }

    plt_offset = plt->data_offset;
    relofs = s1->plt->reloc ? s1->plt->reloc->data_offset : 0;

    p = section_ptr_add(plt, 16);
    p[0] = 0xff; p[1] = 0x25;       /* jmp *(got + got_offset) */
    write32le(p + 2, got_offset);
    p[6] = 0x68;                      /* push $reloc_index */
    write32le(p + 7, relofs / sizeof(ElfW_Rel) - 1);
    p[11] = 0xe9;                     /* jmp PLT[0] */
    write32le(p + 12, -(plt->data_offset));

    return plt_offset;
}
```

### 6.11.5 `build_got_entries()` 函数

`build_got_entries()`（第 1417 行）是 GOT/PLT 条目创建的主循环。它执行两遍扫描（pass 0 和 pass 1），因为某些 ARM 架构不允许混合 `R_JMP_SLOT` 和 `R_GLOB_DAT` 类型的重定位。

```c
ST_FUNC void build_got_entries(TCCState *s1, int got_sym)
{
    int pass = 0;
redo:
    for(i = 1; i < s1->nb_sections; i++) {
        s = s1->sections[i];
        if (s->sh_type != SHT_RELX) continue;
        if (s->link != symtab_section) continue;
        for_each_elem(s, 0, rel, ElfW_Rel) {
            type = ELFW(R_TYPE)(rel->r_info);
            gotplt_entry = gotplt_entry_type(type);
            /* 根据 gotplt_entry 的值决定是否创建 GOT/PLT 条目 */
            switch (gotplt_entry) {
            case NO_GOTPLT_ENTRY:   break;    /* 不需要 */
            case AUTO_GOTPLT_ENTRY: /* 自动判断 */
                if (sym->st_shndx == SHN_UNDEF) goto jmp_slot;
                break;
            case BUILD_GOT_ONLY:    /* 仅 GOT */
                put_got_entry(s1, R_GLOB_DAT, sym_index);
                break;
            case ALWAYS_GOTPLT_ENTRY: /* 总是创建 */
                if (is_jmp_slot) goto jmp_slot;
                put_got_entry(s1, R_GLOB_DAT, sym_index);
                break;
            jmp_slot:
                put_got_entry(s1, R_JMP_SLOT, sym_index);
                break;
            }
        }
    }
    if (++pass < 2) goto redo;
}
```

### 6.11.6 `put_got_entry()` 函数

`put_got_entry()`（第 1329 行）为单个符号创建 GOT 条目（和可选的 PLT 条目）：

```c
static struct sym_attr *put_got_entry(TCCState *s1, int dyn_reloc_type,
                                      int sym_index)
{
    attr = get_sym_attr(s1, sym_index, 1);
    if (need_plt_entry ? attr->plt_offset : attr->got_offset)
        return attr;  /* 已创建 */

    /* 分配 GOT 条目 */
    got_offset = s1->got->data_offset;
    section_ptr_add(s1->got, PTR_SIZE);

    /* 创建动态重定位 */
    if (s1->dynsym) {
        if (ELFW(ST_BIND)(sym->st_info) == STB_LOCAL) {
            /* 本地符号：标记为 R_RELATIVE，稍后在
               fill_local_got_entries() 中修补 */
            put_elf_reloc(s1->dynsym, s1->got, got_offset,
                          R_RELATIVE, sym_index);
        } else {
            /* 全局符号：创建正式的动态重定位 */
            if (0 == attr->dyn_index)
                attr->dyn_index = set_elf_sym(s1->dynsym, ...);
            put_elf_reloc(s1->dynsym, s_rel, got_offset,
                          dyn_reloc_type, attr->dyn_index);
        }
    }

    if (need_plt_entry) {
        attr->plt_offset = create_plt_entry(s1, got_offset, attr);
        /* 创建 "sym@plt" 符号 */
        put_elf_sym(s1->symtab, attr->plt_offset, 0, ...,
                    s1->plt->sh_num, "sym@plt");
    } else {
        attr->got_offset = got_offset;
    }
}
```

### 6.11.7 `relocate_plt()` 函数

`relocate_plt()`（`x86_64-link.c` 第 178 行）在最终地址确定后修补 PLT 和 GOT 中的偏移：

```c
ST_FUNC void relocate_plt(TCCState *s1)
{
    p = s1->plt->data;
    if (p < p_end) {
        int x = s1->got->sh_addr - s1->plt->sh_addr - 6;
        add32le(p + 2, x);     /* PLT[0] 的 push */
        add32le(p + 8, x - 6); /* PLT[0] 的 jmp */
        p += 16;
        while (p < p_end) {
            add32le(p + 2, x + (s1->plt->data - p)); /* PLT[n] 的 jmp */
            p += 16;
        }
    }
    /* 初始化 GOT 条目：指向 PLT 条目 + 6（即 push 指令） */
    if (s1->plt->reloc) {
        int x = s1->plt->sh_addr + 16 + 6;
        for_each_elem(s1->plt->reloc, 0, rel, ElfW_Rel) {
            write64le(p + rel->r_offset, x);
            x += 16;
        }
    }
}
```

---

## 6.12 动态链接支持

TinyCC 支持生成动态链接的可执行文件和共享库。这涉及多个协作的节。

### 6.12.1 `.dynamic` 节

`.dynamic` 节包含一系列 `Elf64_Dyn` 条目，描述动态链接所需的所有信息。TinyCC 在 `fill_dynamic()` 中填充以下标签：

| 标签 | 说明 |
|------|------|
| `DT_NEEDED` | 需要的共享库（每个加载的 DLL 一个） |
| `DT_SONAME` | 共享库自身的名称 |
| `DT_SYMTAB` | `.dynsym` 地址 |
| `DT_STRTAB` | `.dynstr` 地址 |
| `DT_STRSZ` | 字符串表大小 |
| `DT_HASH` | 传统哈希表地址 |
| `DT_GNU_HASH` | GNU 哈希表地址 |
| `DT_JMPREL` | PLT 重定位表地址 |
| `DT_PLTRELSZ` | PLT 重定位表大小 |
| `DT_PLTGOT` | `.got.plt` 地址 |
| `DT_RELASZ` | 重定位表大小 |
| `DT_RELA` | 重定位表地址 |
| `DT_INIT_ARRAY` | 构造函数数组地址 |
| `DT_FINI_ARRAY` | 析构函数数组地址 |
| `DT_FLAGS` | `DF_BIND_NOW`（立即绑定） |
| `DT_FLAGS_1` | `DF_1_NOW`、`DF_1_PIE` 等 |

### 6.12.2 `.dynsym` 和 `.dynstr` 节

`.dynsym` 是动态符号表，只包含需要动态链接器处理的符号（全局和弱符号）。`.dynstr` 是对应的字符串表。

TinyCC 维护两个符号表：
- `symtab_section`（`.symtab`）：完整的静态符号表
- `s1->dynsym`（`.dynsym`）：仅用于动态链接的符号表

以及一个临时的 `dynsymtab_section`（`.dynsymtab`），用于在加载 `.so` 文件时收集符号。

### 6.12.3 GNU 哈希表

`create_gnu_hash()`（第 921 行）和 `update_gnu_hash()`（第 961 行）实现了 GNU 扩展哈希表格式，比传统 ELF 哈希表更高效。

GNU 哈希表的结构：

```
nbuckets    : 桶数量
symoffset   : 第一个已定义符号的索引
bloom_size  : Bloom 过滤器大小
bloom_shift : Bloom 过滤器移位量
bloom[]     : Bloom 过滤器（用于快速排除不存在的符号）
buckets[]   : 桶数组
chains[]    : 链数组
```

GNU 哈希使用 Bloom 过滤器实现 O(1) 的"符号不存在"检测，显著加速了动态链接过程。哈希函数使用经典的 DJB 哈希：

```c
static Elf32_Word elf_gnu_hash(const unsigned char *name)
{
    Elf32_Word h = 5381;
    unsigned char c;
    while ((c = *name++))
        h = h * 33 + c;
    return h;
}
```

### 6.12.4 符号排序

`update_gnu_hash()` 中包含一个重要的排序逻辑：它将未定义符号放在符号表的前面，已定义符号按哈希桶分组放在后面。这是因为 GNU 哈希表的 `chains` 数组只为已定义符号分配空间，`symoffset` 标记了已定义符号的起始位置。

### 6.12.5 版本信息

TinyCC 通过 `version_add()` 函数（第 605 行）生成 `.gnu.version` 和 `.gnu.version_r` 节。`.gnu.version` 节是一个 `Elf64_Half` 数组，每个动态符号对应一个版本索引。`.gnu.version_r` 节描述了每个版本属于哪个库及其版本字符串。

---

## 6.13 JIT 运行时 tccrun.c

TinyCC 最独特的特性之一是内置的 JIT（Just-In-Time）执行能力。通过 `tcc -run` 选项，可以将 C 程序直接编译并在内存中执行，无需生成磁盘文件。

### 6.13.1 `tcc_run()` 函数

`tcc_run()`（`tccrun.c` 第 218 行）是 JIT 执行的入口：

```c
LIBTCCAPI int tcc_run(TCCState *s1, int argc, char **argv)
{
    int (*prog_main)(int, char **, char **), ret;
    const char *top_sym;
    jmp_buf main_jb;

    /* 注册退出处理 */
    tcc_add_symbol(s1, "__rt_exit", rt_exit);
    s1->run_main = "_runmain", top_sym = "main";
    if (s1->elf_entryname)
        s1->run_main = top_sym = s1->elf_entryname;
    tcc_add_support(s1, "runmain.o");

    /* 核心：重定位代码到可执行内存 */
    if (tcc_relocate(s1) < 0)
        return -1;

    /* 获取入口函数地址 */
    prog_main = (void*)get_sym_addr(s1, s1->run_main, 1, 1);

    /* 执行 */
    fflush(stdout); fflush(stderr);
    ret = tcc_setjmp(s1, main_jb, tcc_get_symbol(s1, top_sym));
    if (0 == ret)
        ret = prog_main(argc, argv, envp);
    return ret;
}
```

### 6.13.2 `tcc_relocate()` 函数

`tcc_relocate()`（第 151 行）是 JIT 的核心——将编译后的节复制到可执行内存并完成重定位：

```c
LIBTCCAPI int tcc_relocate(TCCState *s1)
{
    int size, ret, ptr_diff;

    /* 第一步：计算所需内存大小 */
    size = tcc_relocate_ex(s1, NULL, 0);
    if (size < 0) return -1;

    /* 第二步：分配可执行内存 */
    ptr_diff = rt_mem(s1, size);
    if (ptr_diff < 0) return -1;

    /* 第三步：复制并重定位 */
    ret = tcc_relocate_ex(s1, s1->run_ptr, ptr_diff);
    if (ret == 0)
        st_link(s1);
    return ret;
}
```

### 6.13.3 内存分配策略

`rt_mem()`（第 105 行）根据平台采用不同的内存分配策略：

**Linux SELinux 模式：**

```c
#ifdef CONFIG_SELINUX
    int fd = mkstemp(tmpfname);
    unlink(tmpfname);
    ftruncate(fd, size);
    /* 代码段：RX */
    ptr = mmap(NULL, size * 2, PROT_READ|PROT_EXEC, MAP_SHARED, fd, 0);
    /* 数据段：RW（与代码段固定距离） */
    prw = mmap((char*)ptr + size, size, PROT_READ|PROT_WRITE,
               MAP_SHARED|MAP_FIXED, fd, 0);
    ptr_diff = (char*)prw - (char*)ptr; /* = size */
#endif
```

这种模式将同一文件映射两次，一次可执行（RX），一次可写（RW），通过 `ptr_diff` 记录两者之间的偏移。

**普通 Linux：**

```c
ptr = tcc_malloc(size += PAGESIZE);  /* 额外一页用于对齐 */
```

分配普通堆内存，之后通过 `mprotect()` 设置页面权限。

**Windows：**

```c
ptr = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
```

### 6.13.4 `tcc_relocate_ex()` 函数

`tcc_relocate_ex()`（第 333 行）是重定位的核心实现，采用三遍策略：

**第一遍（ptr == NULL）：计算大小和地址**

```c
for (k = 0; k < 3; ++k) { /* 0:rx, 1:ro, 2:rw */
    for (i = 1; i < s1->nb_sections; i++) {
        s = s1->sections[i];
        if (权限匹配) {
            offset += 对齐;
            s->sh_addr = mem ? addr + offset : 0;
            offset += length;
        }
    }
}
return PAGEALIGN(offset);  /* 返回所需总大小 */
```

**第二遍（copy == 1）：复制数据**

```c
for (k = 0; k < 3; ++k) {
    for (i = 1; i < s1->nb_sections; i++) {
        if (s->data && s->sh_type != SHT_NOBITS)
            memcpy((void*)s->sh_addr, s->data, length);
        else
            memset((void*)s->sh_addr, 0, length);
    }
}
```

**第三遍（copy == 2）：设置权限**

```c
protect_pages(addr, n, mode);
/* mode: 0=rx, 1=ro, 2=rw, 3=rwx */
```

在两次复制之间，执行符号解析和重定位：

```c
relocate_syms(s1, s1->symtab, 1);  /* do_resolve=1: 使用 dlsym() */
relocate_plt(s1);
relocate_sections(s1);
```

### 6.13.5 `tcc_get_symbol()` 函数

JIT 执行完成后，可以通过 `tcc_get_symbol()` 获取编译后符号的地址：

```c
LIBTCCAPI void *tcc_get_symbol(TCCState *s, const char *name)
{
    addr_t addr = get_sym_addr(s, name, 0, 1);
    return addr == -1 ? NULL : (void*)(uintptr_t)addr;
}
```

这使得 JIT 模式可以用于实现脚本引擎、插件系统等场景——先将 C 代码编译到内存，然后通过函数指针调用。

### 6.13.6 与普通链接的对比

| 方面 | 普通链接 | JIT 模式 |
|------|---------|---------|
| 符号解析 | 链接器静态解析 | `dlsym()` 运行时解析 |
| 输出 | ELF 文件 | 内存中的可执行代码 |
| 地址分配 | `layout_sections()` | `tcc_relocate_ex()` |
| GOT/PLT | 生成并输出 | 生成但不输出到文件 |
| 权限管理 | 由 OS 加载器处理 | `mprotect()` 手动设置 |

---

## 6.14 本章小结与练习

### 本章小结

本章深入分析了 TinyCC 链接器的完整实现。我们从 ELF 文件格式的基础知识出发，逐步覆盖了以下核心主题：

1. **ELF 格式**：ELF 头、节头、程序头的结构和字节布局，是理解链接器工作的基础。
2. **Section 管理**：`Section` 结构体是 TinyCC 链接器的核心数据结构，通过 `new_section()`、`section_add()`、`section_ptr_add()` 管理所有节数据。
3. **符号系统**：`Elf64_Sym` 结构体编码了符号的绑定（LOCAL/GLOBAL/WEAK）、类型（NOTYPE/OBJECT/FUNC）和可见性。`set_elf_sym()` 实现了符号冲突的解决规则。
4. **重定位**：`Elf64_Rela` 结构体描述了如何修补代码和数据中的地址引用。`relocate()` 函数实现了 x86-64 平台上所有重定位类型的处理。
5. **目标文件加载**：`tcc_load_object_file()` 通过五阶段流程将 `.o` 文件合并到编译状态中。
6. **库加载**：静态库（`.a`）通过 `tcc_load_archive()` 按成员遍历加载；共享库（`.so`）通过 `tcc_load_dll()` 提取动态符号。
7. **GOT/PLT**：实现了位置无关代码和延迟绑定的核心机制。
8. **文件输出**：`elf_output_file()` 的五阶段管道（准备、绑定、布局、重定位、输出）是链接器的主干。
9. **JIT 运行时**：`tcc_relocate_ex()` 通过分配 RWX 内存、复制节数据、应用重定位，实现了 C 代码的即时执行。

### 练习

#### 练习 1：ELF 文件分析

使用 `readelf` 和 `objdump` 工具分析 TinyCC 编译输出的 `.o` 文件，理解各个节的含义和重定位条目的工作方式。详见 `exercises/ex1_elf.md`。

#### 练习 2：重定位追踪

编写一个包含外部函数调用的 C 程序，使用 TinyCC 编译为目标文件，然后手动追踪重定位过程。详见 `exercises/ex2_reloc.md`。

#### 练习 3：JIT 行为观察

使用 `tcc -run` 执行 C 程序，通过 `/proc/[pid]/maps` 观察 JIT 内存布局，理解运行时重定位的行为。详见 `exercises/ex3_jit.md`。

---

## 参考文献

1. Tool Interface Standard (TIS) Executable and Linkable Format (ELF) Specification Version 1.2.
2. System V Application Binary Interface - AMD64 Architecture Processor Supplement.
3. Fabrice Bellard, "TCC: Tiny C Compiler", https://bellard.org/tcc/.
4. Ian Lance Taylor, "Linkers and Loaders", 2000. (系列文章)
5. Michael Matz et al., "System V Application Binary Interface", 2023.
