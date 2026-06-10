# 第九章 调试与测试

一个编译器的正确性至关重要——一个错误的代码生成可能会导致程序在运行时产生难以追踪的 bug。TinyCC 通过两套机制来保证质量：调试信息生成（使得用户可以用 GDB 等调试器调试 TCC 编译的程序）和全面的测试套件（覆盖语言特性的方方面面）。本章将深入分析 TCC 的调试信息生成机制和测试基础设施。

---

## 9.1 STAB 调试格式

STAB（Symbol Table Debugger）是一种较老的调试信息格式，最初由 BSD 系统引入。TCC 历史上首先支持的就是 STAB 格式，这也是默认的调试格式（除非使用 `-gdwarf` 选项）。

### 9.1.1 STAB 基础

STAB 信息存储在 ELF 文件的 `.stab` 和 `.stabstr` 段中。每条 STAB 记录（`Stab_Sym` 结构）包含：

```c
/* stab.h */
typedef struct {
    unsigned long n_strx;    /* 字符串表偏移 */
    unsigned char n_type;    /* 符号类型 */
    unsigned char n_other;   /* 杂项信息 */
    unsigned short n_desc;   /* 描述字段 */
    unsigned long n_value;   /* 值（地址或常量） */
} Stab_Sym;
```

主要的 STAB 类型：

| 类型 | 含义 |
|------|------|
| `N_SLINE` (0x44) | 源代码行号映射 |
| `N_SO` (0x64) | 源文件名 |
| `N_FUN` (0x24) | 函数定义 |
| `N_LSYM` (0x80) | 局部类型定义 |
| `N_GSYM` (0x20) | 全局符号 |
| `N_PSYM` (0xa0) | 函数参数 |
| `N_LBRAC` (0xc0) | 左花括号（块开始） |
| `N_RBRAC` (0xe0) | 右花括号（块结束） |

### 9.1.2 类型编码

STAB 使用字符串来编码类型信息，遵循 GCC 的 stabstring 格式。TCC 在 `tccdbg.c` 中通过 `default_debug` 数组预定义了基本类型：

```c
/* tccdbg.c - default_debug 数组 */
static const struct {
    int type;
    int size;
    int encoding;
    const char *name;
} default_debug[] = {
    { VT_INT,  4, DW_ATE_signed,
      "int:t1=r1;-2147483648;2147483647;" },
    { VT_BYTE, 1, DW_ATE_signed_char,
      "char:t2=r2;0;127;" },
    { VT_LLONG | VT_LONG, 8, DW_ATE_signed,
      "long int:t3=r3;-9223372036854775808;9223372036854775807;" },
    { VT_INT | VT_UNSIGNED, 4, DW_ATE_unsigned,
      "unsigned int:t4=r4;0;037777777777;" },
    /* ... 更多类型 ... */
    { VT_FLOAT, 4, DW_ATE_float,
      "float:t14=r1;4;0;" },
    { VT_DOUBLE, 8, DW_ATE_float,
      "double:t15=r1;8;0;" },
    { VT_LDOUBLE, 16, DW_ATE_float,
      "long double:t16=r1;16;0;" },
    { VT_BOOL, 1, DW_ATE_boolean,
      "bool:t26=r26;0;255;" },
    { VT_VOID, 1, DW_ATE_unsigned_char,
      "void:t27=27" },
};
```

STAB 类型编码语法：
- `name:tN`：定义类型编号 N，名称为 name
- `rN;low;high;`：范围类型（整数），类型 N，范围 [low, high]
- `*T`：指向类型 T 的指针
- `arT;low;high;element_type`：数组类型
- `sN`：结构体，大小 N 字节
- `uN`：联合体，大小 N 字节

### 9.1.3 STAB 的局限

STAB 格式有几个显著局限：
- 不支持类型间的引用关系（如递归结构体）
- 字符串编码空间效率低
- 不支持复杂的类型操作（如模板、命名空间）
- 大多数现代调试器更偏好 DWARF

因此，STAB 主要用于兼容性场景和简单的调试需求。

---

## 9.2 DWARF 调试格式

DWARF 是现代 Unix/Linux 系统的标准调试信息格式。TCC 从较新版本开始支持 DWARF（通过 `-gdwarf` 选项或在某些平台上默认启用）。

### 9.2.1 DWARF 段结构

TCC 生成以下 DWARF 段：

| 段名 | 内容 |
|------|------|
| `.debug_info` | 类型信息、变量、函数 |
| `.debug_abbrev` | abbreviation 表（DIE 格式定义） |
| `.debug_line` | 行号信息（源码到机器码映射） |
| `.debug_str` | 字符串表 |
| `.debug_line_str` | 行号段专用字符串表 |
| `.debug_aranges` | 地址范围表（加速查找） |

### 9.2.2 Abbreviation 表

DWARF 的 abbreviation 表定义了调试信息条目（DIE，Debug Information Entry）的格式。TCC 在 `tccdbg.c` 中静态定义了所有需要的 abbreviation：

```c
/* tccdbg.c - DWARF abbreviation 定义 */
#define DWARF_ABBREV_COMPILE_UNIT       1
#define DWARF_ABBREV_BASE_TYPE          2
#define DWARF_ABBREV_VARIABLE_EXTERNAL  3
#define DWARF_ABBREV_VARIABLE_STATIC    4
#define DWARF_ABBREV_VARIABLE_LOCAL     5
#define DWARF_ABBREV_FORMAL_PARAMETER   6
#define DWARF_ABBREV_POINTER            7
#define DWARF_ABBREV_ARRAY_TYPE         8
#define DWARF_ABBREV_SUBRANGE_TYPE      9
#define DWARF_ABBREV_TYPEDEF           10
#define DWARF_ABBREV_ENUMERATOR_SIGNED  11
#define DWARF_ABBREV_ENUMERATION_TYPE   13
#define DWARF_ABBREV_MEMBER            14
#define DWARF_ABBREV_MEMBER_BF         15
#define DWARF_ABBREV_STRUCTURE_TYPE     16
#define DWARF_ABBREV_UNION_TYPE        18
#define DWARF_ABBREV_SUBPROGRAM_EXTERNAL 20
#define DWARF_ABBREV_SUBPROGRAM_STATIC  21
#define DWARF_ABBREV_LEXICAL_BLOCK      22
#define DWARF_ABBREV_SUBROUTINE_TYPE    24
/* ... */
```

每个 abbreviation 条目定义了：
1. **标签**（如 `DW_TAG_compile_unit`）
2. **是否有子节点**
3. **属性列表**（`DW_AT_*` 类型和 `DW_FORM_*` 格式）

例如，编译单元的 abbreviation：

```c
/* tccdbg.c */
DWARF_ABBREV_COMPILE_UNIT, DW_TAG_compile_unit, 1,  /* 有子节点 */
    DW_AT_producer,  DW_FORM_strp,      /* 编译器名称 */
    DW_AT_language,  DW_FORM_data1,      /* 语言: C */
    DW_AT_name,      DW_FORM_line_strp,  /* 源文件名 */
    DW_AT_comp_dir,  DW_FORM_line_strp,  /* 编译目录 */
    DW_AT_low_pc,    DW_FORM_addr,       /* 代码起始地址 */
    DW_AT_high_pc,   DW_FORM_data8,      /* 代码大小 */
    DW_AT_stmt_list, DW_FORM_sec_offset, /* 行号段偏移 */
    0, 0,
```

### 9.2.3 行号状态机

DWARF 使用一个**行号状态机**来紧凑地编码源代码行号与机器码地址之间的映射。状态机的状态包括：

```c
/* DWARF 行号状态机状态 */
typedef struct {
    unsigned long address;   /* 当前指令地址 */
    unsigned int file;       /* 源文件编号 */
    unsigned int line;       /* 当前行号 */
    unsigned int column;     /* 当前列号 */
    unsigned int is_stmt;    /* 是否为语句开始 */
    unsigned int end_sequence; /* 序列结束标志 */
} dwarf_line_state;
```

TCC 在 `tccdbg.c` 中定义了行号状态机的参数：

```c
/* tccdbg.c */
#define DWARF_LINE_BASE    -5    /* line_increment 的最小值 */
#define DWARF_LINE_RANGE   14    /* line_increment 的范围 */
#define DWARF_OPCODE_BASE  13    /* 第一个特殊操作码 */

#if defined TCC_TARGET_ARM64
#define DWARF_MIN_INSTR_LEN  4   /* ARM64: 4 字节指令 */
#elif defined TCC_TARGET_ARM
#define DWARF_MIN_INSTR_LEN  2   /* ARM: 2 字节指令 */
#else
#define DWARF_MIN_INSTR_LEN  1   /* x86: 1 字节指令 */
#endif
```

行号状态机的操作码分为三类：

1. **标准操作码**（1-12）：
   - `DW_LNS_copy` (1)：发出当前行号记录
   - `DW_LNS_advance_pc` (2)：推进地址
   - `DW_LNS_advance_line` (3)：推进行号
   - `DW_LNS_set_file` (4)：设置文件编号
   - `DW_LNS_set_column` (5)：设置列号
   - `DW_LNS_negate_stmt` (10)：翻转 is_stmt
   - `DW_LNS_set_basic_block` (11)：标记基本块开始

2. **特殊操作码**（13-255）：
   - 编码为 `opcode = opcode_base + (line_increment - line_base) + line_range * address_increment`
   - 一条特殊操作码同时推进地址和行号

3. **扩展操作码**（opcode=0）：
   - `DW_LNE_end_sequence`：标记序列结束
   - `DW_LNE_set_address`：设置绝对地址
   - `DW_LNE_define_file`：定义文件

### 9.2.4 类型 DIE

DWARF 使用 DIE（Debug Information Entry）树来描述类型信息。TCC 为每种 C 类型生成对应的 DIE：

**基本类型**（`DW_TAG_base_type`）：

```c
/* DWARF_ABBREV_BASE_TYPE */
/* 属性: byte_size, encoding, name */
```

编码值（`DW_ATE_*`）：

| 编码 | 含义 |
|------|------|
| `DW_ATE_signed` (5) | 有符号整数 |
| `DW_ATE_unsigned` (7) | 无符号整数 |
| `DW_ATE_signed_char` (6) | 有符号字符 |
| `DW_ATE_unsigned_char` (8) | 无符号字符 |
| `DW_ATE_float` (4) | 浮点数 |
| `DW_ATE_boolean` (2) | 布尔值 |

**结构体类型**（`DW_TAG_structure_type`）：

```c
/* DWARF_ABBREV_STRUCTURE_TYPE */
/* 有子节点 */
/* 属性: name, byte_size, decl_file, decl_line, sibling */
/* 子节点: member (DW_TAG_member) */
/*   属性: name, decl_file, decl_line, type, data_member_location */
```

**指针类型**（`DW_TAG_pointer_type`）：

```c
/* DWARF_ABBREV_POINTER */
/* 属性: byte_size, type */
```

**函数类型**（`DW_TAG_subroutine_type`）：

```c
/* DWARF_ABBREV_SUBROUTINE_TYPE */
/* 有子节点 */
/* 属性: type (返回类型), sibling */
/* 子节点: formal_parameter (DW_TAG_formal_parameter) */
```

---

## 9.3 调试信息生成时机

调试信息的生成贯穿 TCC 编译的各个阶段。`tccdbg.c` 提供了一组函数来协调这一过程：

### 9.3.1 tcc_debug_start

在编译一个文件开始时调用，初始化调试段：

```c
/* tccdbg.c - 伪代码 */
ST_FUNC void tcc_debug_start(TCCState *s1)
{
    if (s1->do_debug) {
        if (s1->dwarf) {
            /* 初始化 DWARF 段 */
            s1->dwarf_info_section = find_section(s1, ".debug_info");
            s1->dwarf_abbrev_section = find_section(s1, ".debug_abbrev");
            s1->dwarf_line_section = find_section(s1, ".debug_line");
            s1->dwarf_str_section = find_section(s1, ".debug_str");
            /* 写入 abbreviation 表 */
            put_abbrevs(s1);
            /* 初始化行号状态机 */
            init_line_state(s1);
        } else {
            /* 初始化 STAB 段 */
            s1->stab_section = find_section(s1, ".stab");
            /* 写入默认类型信息 */
            put_stabs(s1, default_debug);
        }
    }
}
```

### 9.3.2 tcc_debug_line

每当编译器处理到一个新的源代码行时调用，更新行号映射：

```c
/* tccdbg.c - 伪代码 */
ST_FUNC void tcc_debug_line(TCCState *s1)
{
    if (s1->do_debug && !nocode_wanted) {
        /* 获取当前源文件和行号 */
        int file = get_debug_file_index(file->filename);
        int line = file->line_num;

        if (s1->dwarf) {
            /* 使用行号状态机编码 */
            emit_dwarf_line(s1, file, line, ind);
        } else {
            /* STAB: 直接写入 N_SLINE 记录 */
            put_stabn(s1, N_SLINE, line, ind - cur_text_section->sh_addr);
        }
    }
}
```

### 9.3.3 tcc_debug_funcstart / tcc_debug_funcend

在函数编译开始和结束时调用，生成函数范围和局部变量信息：

```c
/* tccdbg.c - 伪代码 */
ST_FUNC void tcc_debug_funcstart(TCCState *s1, Sym *sym)
{
    if (s1->do_debug) {
        if (s1->dwarf) {
            /* 写入 DW_TAG_subprogram DIE */
            begin_dwarf_func(s1, sym);
        } else {
            /* STAB: 写入 N_FUN 记录 */
            put_stabs_r(s1, sym->v, N_FUN, 0, 0, ind, sym->type.t);
        }
    }
}

ST_FUNC void tcc_debug_funcend(TCCState *s1, int size)
{
    if (s1->do_debug) {
        if (s1->dwarf) {
            /* 结束 DW_TAG_subprogram DIE */
            end_dwarf_func(s1, size);
        } else {
            /* STAB: 写入 N_FUN 结束记录 */
            put_stabn(s1, N_FUN, 0, size, "");
        }
    }
}
```

### 9.3.4 块作用域

进入和离开代码块（`{`...`}`）时，调试器需要知道变量的作用域范围：

```c
/* STAB 模式 */
/* N_LBRAC: 块开始 */
/* N_RBRAC: 块结束 */

/* DWARF 模式 */
/* DW_TAG_lexical_block DIE */
/* 包含 low_pc 和 high_pc 属性 */
```

---

## 9.4 tccdbg.c 内部结构

### 9.4.1 _tccdbg 状态结构

TCC 的调试信息生成器维护一个独立的状态结构 `_tccdbg`，通过 `TCCState->dState` 指针访问：

```c
/* tccdbg.c */
struct _tccdbg {
    /* DWARF 状态 */
    struct {
        int *hash;        /* 调试信息哈希表（用于类型去重） */
        int nb_hash;
        /* 字符串池 */
        CString debug_str;
        CString debug_line_str;
        /* 当前 DIE 的子节点计数 */
        int dwarf_info_child_count;
        /* 行号状态机当前状态 */
        unsigned int dwarf_line_state[/*...*/];
    } dw;

    /* STAB 状态 */
    struct {
        /* 类型编号映射 */
        int *type_offsets;
        int nb_types;
    } stab;

    /* 通用 */
    int last_line_num;     /* 上一次发出的行号 */
    int last_file_num;     /* 上一次发出的文件编号 */
};
```

### 9.4.2 调试哈希表

DWARF 要求相同的类型只出现一次。TCC 使用哈希表来检测重复类型：

```c
/* tccdbg.c */
static int debug_type_hash(Sym *s)
{
    /* 基于类型的 hash 值 */
    int h = s->type.t;
    if (s->type.ref)
        h += (uintptr_t)s->type.ref;
    return h & (s1->dState->dw.nb_hash - 1);
}

static int debug_find_type(Sym *s)
{
    int h = debug_type_hash(s);
    int *ph = &s1->dState->dw.hash[h];
    /* 在哈希链中查找匹配的类型 */
    /* ... */
}
```

### 9.4.3 DWARF 字符串池

DWARF 使用两种字符串表：
- `.debug_str`：通过 `DW_FORM_strp` 引用，可以被多个段共享
- `.debug_line_str`：通过 `DW_FORM_line_strp` 引用，行号段专用

TCC 在 `_tccdbg` 结构中维护两个 `CString` 来累积这些字符串。

### 9.4.4 STAB 的 N_DEFAULT_DEBUG

STAB 模式下，`default_debug` 数组中的所有基本类型在文件编译开始时就被写入 `.stab` 段。这确保了类型编号 1-29 始终可用：

```c
/* tccdbg.c */
#define N_DEFAULT_DEBUG (sizeof(default_debug) / sizeof(default_debug[0]))
/* N_DEFAULT_DEBUG ≈ 29 个基本类型 */
```

复杂类型（结构体、指针、数组等）在遇到时按需生成，并分配更高的类型编号。

---

## 9.5 使用 GDB 调试 TCC 编译的程序

### 9.5.1 基本流程

```bash
# 步骤 1: 使用 -g 选项编译
tcc -g -o program program.c

# 步骤 2: 使用 GDB 调试
gdb ./program
```

在 GDB 中可以使用标准的调试命令：

```
(gdb) break main          # 设置断点
(gdb) run                 # 运行程序
(gdb) list                # 查看源代码
(gdb) print variable      # 打印变量值
(gdb) step                # 单步执行（进入函数）
(gdb) next                # 单步执行（不进入函数）
(gdb) backtrace           # 查看调用栈
(gdb) info locals         # 查看局部变量
```

### 9.5.2 DWARF 模式调试

使用 DWARF 格式可以获得更好的调试体验：

```bash
tcc -gdwarf -o program program.c
gdb ./program
```

DWARF 提供了更精确的类型信息和更高效的行号查找。

### 9.5.3 调试 libtcc 编译的代码

当使用 libtcc API 时，内存中的代码不会有文件系统路径。GDB 可以通过以下方式调试：

```c
/* 方法 1: 使用 tcc_set_options 启用调试 */
tcc_set_options(s, "-g");
tcc_compile_string(s, source_code);
tcc_relocate(s);

/* 方法 2: 使用 #line 指令指定虚拟文件名 */
tcc_compile_string(s,
    "#line 1 \"script.c\"\n"
    "int main() { return 42; }\n");
```

对于 JIT 编译的代码，GDB 的 JIT 接口（`__jit_debug_register_code`）可以用来注册代码映射。TCC 的 `-run` 模式自动处理了运行时调试信息的注册。

### 9.5.4 运行时回溯

TCC 支持运行时栈回溯（通过 `-bt` 选项），即使没有 GDB 也能获得基本的错误定位：

```bash
tcc -bt -o program program.c
./program
# 如果程序崩溃，会显示类似:
# program.c:10: at main() Division by zero
```

回溯功能的实现定义在 `tccrun.c` 中，使用 `rt_context` 结构来跟踪调试信息：

```c
/* tccrun.c */
typedef struct rt_context {
    /* STAB 信息 */
    Stab_Sym *stab_sym, *stab_sym_end;
    char *stab_str;
    /* 或 DWARF 信息 */
    unsigned char *dwarf_line, *dwarf_line_end;
    unsigned char *dwarf_line_str;
    /* ELF 符号表 */
    ElfW(Sym) *esym_start, *esym_end;
    char *elf_str;
    /* 运行时状态 */
    addr_t prog_base;
    void *bounds_start;
    void *top_func;
    int num_callers;
    int dwarf;
} rt_context;
```

回溯函数通过检查栈帧链（frame pointer chain）和程序计数器（PC）值，利用 STAB 或 DWARF 信息将地址翻译为源文件名和行号。

---

## 9.6 TCC 测试套件

TCC 有全面的测试套件来验证编译器的正确性。测试文件位于 `tests/` 目录下。

### 9.6.1 测试结构概览

```
tests/
├── tcctest.c          # 主要的 C 语言特性测试（4500+ 行）
├── tcctest.h          # 测试辅助宏
├── boundtest.c        # 边界检查测试
├── libtcc_test.c      # libtcc API 测试
├── libtcc_test_mt.c   # libtcc 多线程测试
├── abitest.c          # ABI/调用约定测试
├── vla_test.c         # 变长数组测试
├── testfp.c           # 浮点测试
├── Makefile           # 测试构建脚本
├── pp/                # 预处理器测试
│   ├── 01_hash.c
│   ├── 02_hashif.c
│   └── ...
└── tests2/            # 扩展测试集
    ├── 00_assignment.c / .expect
    ├── 01_comment.c / .expect
    ├── 02_printf.c / .expect
    └── ...（约 130 个测试用例）
```

### 9.6.2 tcctest.c

`tcctest.c` 是 TCC 最核心的测试文件，涵盖了几乎所有 C 语言特性。它的测试策略是：编译时同时用参考编译器（如 GCC）和 TCC 编译，然后比较两个程序的输出。

测试覆盖的特性包括：

```c
/* tcctest.c 中测试的特性（摘选） */
void integer_ops(void)       /* 整数运算 */
void float_ops(void)         /* 浮点运算 */
void pointer_ops(void)       /* 指针操作 */
void struct_ops(void)        /* 结构体/联合体 */
void enum_ops(void)          /* 枚举 */
void array_ops(void)         /* 数组 */
void string_ops(void)        /* 字符串操作 */
void cast_ops(void)          /* 类型转换 */
void control_flow(void)      /* 控制流 */
void loop_ops(void)          /* 循环 */
void function_ops(void)      /* 函数调用 */
void varargs_ops(void)       /* 可变参数 */
void preprocessor_ops(void)  /* 预处理器 */
void bitfield_ops(void)      /* 位域 */
void special_ops(void)       /* 特殊操作（sizeof, typeof 等） */
```

### 9.6.3 tests2/ 扩展测试集

`tests2/` 目录包含约 130 个独立的测试文件，每个文件测试一个特定的 C 语言特性。每个测试文件都有一个对应的 `.expect` 文件，包含预期的输出。

命名约定：
```
XX_name.c       # 测试文件
XX_name.expect  # 预期输出
```

例如：
- `00_assignment.c`：赋值操作
- `01_comment.c`：注释处理
- `02_printf.c`：printf 格式化
- `03_struct.c`：结构体
- `124_atomic_counter.c`：原子操作
- `127_asm_goto.c`：asm goto
- `132_bound_test.c`：边界检查

### 9.6.4 预处理器测试

`tests/pp/` 目录专门测试预处理器的正确性：

```
tests/pp/
├── 01_hash.c          # 宏定义
├── 02_hashif.c        # #if 条件编译
├── 03_hashelif.c      # #elif
├── 04_*.c             # 更多预处理器特性
└── ...
```

### 9.6.5 测试执行

测试通过 `tests/Makefile` 执行：

```bash
cd tests
make test          # 运行所有测试
make test-tcc      # 仅运行 tcctest
make test2         # 运行 tests2 扩展测试
make test-pp       # 运行预处理器测试
make test-bound    # 运行边界检查测试
make test-asm      # 运行汇编器测试
```

典型的测试流程：

```bash
# 1. 用 TCC 编译并运行测试
tcc -o tcctest_tcc tcctest.c && ./tcctest_tcc > output_tcc

# 2. 用参考编译器编译并运行
gcc -o tcctest_gcc tcctest.c && ./tcctest_gcc > output_gcc

# 3. 比较输出
diff output_tcc output_gcc
```

### 9.6.6 交叉测试

TCC 还支持交叉测试——用一个平台的 TCC 编译面向另一个平台的测试程序：

```bash
# tests/Makefile 中的交叉测试目标
make test-arm      # ARM 交叉测试
make test-arm64    # ARM64 交叉测试
make test-riscv64  # RISC-V 交叉测试
```

---

## 9.7 添加新测试

### 9.7.1 为 tests2/ 添加测试

添加一个新的测试用例到 `tests2/` 非常简单：

**步骤 1**：创建测试文件，选择下一个可用的编号

```c
/* tests/tests2/135_my_feature.c */
#include <stdio.h>

int main(void)
{
    int x = 42;
    int *p = &x;
    
    /* 测试你的特性 */
    printf("x = %d\n", x);
    printf("*p = %d\n", *p);
    printf("&x = %p\n", (void*)&x);
    
    return 0;
}
```

**步骤 2**：生成预期输出

```bash
# 使用参考编译器生成预期输出
gcc -o test 135_my_feature.c && ./test > 135_my_feature.expect
```

**步骤 3**：验证 TCC 的输出

```bash
tcc -o test 135_my_feature.c && ./test | diff - 135_my_feature.expect
```

### 9.7.2 测试编写最佳实践

1. **可移植性**：避免依赖特定平台的输出格式
2. **确定性**：不依赖未初始化的值、随机数或时间
3. **完整性**：覆盖正常路径和边界条件
4. **简洁性**：每个测试专注于一个特性
5. **可比较的输出**：使用 `printf` 输出关键值，便于 diff 比较

```c
/* 好的测试模式 */
#include <stdio.h>

int main(void)
{
    /* 测试赋值 */
    int a = 10;
    printf("a = %d\n", a);

    /* 测试指针 */
    int *p = &a;
    *p = 20;
    printf("a = %d\n", a);

    /* 测试数组 */
    int arr[3] = {1, 2, 3};
    printf("arr = %d %d %d\n", arr[0], arr[1], arr[2]);

    return 0;
}
```

### 9.7.3 边界条件测试

```c
/* 测试整数边界 */
#include <stdio.h>
#include <limits.h>

int main(void)
{
    printf("INT_MAX = %d\n", INT_MAX);
    printf("INT_MIN = %d\n", INT_MIN);
    printf("UINT_MAX = %u\n", UINT_MAX);
    
    /* 溢出行为 */
    int x = INT_MAX;
    x = x + 1;
    printf("INT_MAX + 1 = %d\n", x);
    
    /* 除法边界 */
    int a = -7, b = 2;
    printf("-7 / 2 = %d\n", a / b);
    printf("-7 %% 2 = %d\n", a % b);
    
    return 0;
}
```

---

## 9.8 本章小结与练习

### 小结

本章介绍了 TinyCC 的调试和测试体系：

1. **STAB 调试格式**：较老但兼容性好的格式，通过 `.stab` 和 `.stabstr` 段存储调试信息。基本类型在 `default_debug` 数组中预定义，复杂类型按需生成。

2. **DWARF 调试格式**：现代标准格式，使用 abbreviation 表定义 DIE 格式，行号状态机紧凑地编码源码到机器码的映射。支持更精确的类型信息和更好的调试器兼容性。

3. **调试信息生成时机**：贯穿编译的各个阶段——`tcc_debug_start` 初始化、`tcc_debug_line` 行号映射、`tcc_debug_funcstart`/`funcend` 函数范围、块作用域追踪。

4. **tccdbg.c 内部结构**：使用 `_tccdbg` 状态结构管理 DWARF 字符串池、调试哈希表和行号状态机。

5. **GDB 调试**：使用 `-g`（STAB）或 `-gdwarf`（DWARF）选项编译，然后用 GDB 标准流程调试。

6. **测试套件**：`tcctest.c`（4500+ 行）覆盖核心 C 特性，`tests2/`（130+ 个文件）覆盖具体特性，`pp/` 专门测试预处理器。所有测试通过输出比较来验证正确性。

### 练习 1: 调试信息实验

1. 分别使用 `-g`（STAB）和 `-gdwarf`（DWARF）编译同一个程序
2. 使用 `readelf -S` 查看两种模式下生成的段
3. 使用 `objdump --stabs` 和 `objdump --dwarf=info` 查看调试信息内容
4. 用 GDB 在两种模式下分别调试，比较体验差异

### 练习 2: 编写和运行测试

1. 为 `tests2/` 添加一个新测试，覆盖以下特性之一：
   - `_Generic` 选择表达式（C11）
   - `_Static_assert` 静态断言（C11）
   - 指定初始化器（designated initializers）
   - 复合字面量（compound literals）

2. 运行完整测试套件并报告结果：
   ```bash
   cd tests && make test
   ```

3. 尝试在不同平台上运行测试，记录差异
