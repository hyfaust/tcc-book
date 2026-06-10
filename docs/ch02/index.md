# 第二章 词法分析器（Lexer）

> "词法分析是编译器的第一道工序。它将字符流转换为记号流，为后续的语法分析奠定基础。"
> —— Alfred V. Aho, *Compilers: Principles, Techniques, and Tools*

本章深入剖析 TinyCC（以下简称 tcc）词法分析器的完整实现。词法分析器（lexer 或 scanner）位于 `tccpp.c` 文件中，是整个编译器中最底层、调用最频繁的模块。我们将从理论基础出发，逐步深入到 tcc 的具体实现细节。

---

## 2.1 词法分析的理论基础

### 2.1.1 正则语言与正则表达式

词法分析处理的语言是**正则语言**（regular language），它是乔姆斯基谱系中最简单的一类。正则语言可以用三种等价的形式描述：

1. **正则表达式**（Regular Expression）：用代数符号描述字符串模式
2. **有限自动机**（Finite Automaton）：用状态转移图描述识别过程
3. **正则文法**（Regular Grammar）：用产生式规则描述语法结构

在 C 语言中，各类词法单元（token）的模式可以用正则表达式描述：

```
标识符   →  [a-zA-Z_][a-zA-Z_0-9]*
十进制数 →  [1-9][0-9]*
八进制数 →  0[0-7]*
十六进制 →  0[xX][0-9a-fA-F]+
浮点数   →  [0-9]+\.[0-9]*([eE][+-]?[0-9]+)?
字符串   →  "([^"\\]|\\.)*"
字符常量 →  '([^'\\]|\\.)*'
```

### 2.1.2 有限自动机

有限自动机分为两类：

- **确定性有限自动机**（DFA）：每个状态对每个输入符号恰好有一个转移
- **非确定性有限自动机**（NFA）：每个状态对同一个输入符号可以有零个、一个或多个转移

tcc 的词法分析器本质上是一个**手写的 DFA**。与自动生成的词法分析器（如 lex/flex）不同，tcc 使用一个巨大的 `switch` 语句来实现状态转移。这种手写方式的优点是：

1. **更高的执行效率**：避免了查表和间接跳转的开销
2. **更灵活的控制流**：可以方便地处理上下文相关的词法问题（如预处理指令只在行首出现）
3. **更小的代码体积**：不需要生成巨大的转移表

### 2.1.3 Token 的分类

在 tcc 中，一个 token 由两部分信息组成：

1. **token 编号**（token number）：一个整数，标识 token 的类型
2. **token 值**（token value）：一个 `CValue` 联合体，存储 token 的附加数据（如常量的数值、字符串的内容）

token 编号的分配遵循精心设计的编码方案，这是下一节的主题。

---

## 2.2 tcc 的 Token 类型系统

tcc 的 token 编号系统是一套紧凑而高效的编码方案。所有 token 编号被划分为若干区间，每个区间有明确的语义。理解这套编码方案是阅读 tcc 预处理器和解析器代码的基础。

### 2.2.1 总览

```
┌─────────────────────────────────────────────────────────┐
│                    Token 编号空间                        │
├──────────────┬──────────────────────────────────────────┤
│  -1          │  TOK_EOF（文件结束）                      │
│   0          │  空 token（宏展开结束标记）                │
│   10         │  TOK_LINEFEED（换行符）                   │
│  0x20-0x7F   │  单字符 token（ASCII 字符本身）           │
│  0x80-0xAF   │  内部操作符和多字符操作符                  │
│  0xB0-0xB9   │  复合赋值操作符                           │
│  0xC0-0xCF   │  带值常量 token                           │
│  >= 256      │  标识符和关键字                            │
└──────────────┴──────────────────────────────────────────┘
```

### 2.2.2 单字符 Token（0x20-0x7F）

对于 ASCII 值在 0x20 到 0x7F 之间的单字符 token，tcc 直接使用字符的 ASCII 值作为 token 编号。这是一种极其高效的设计——不需要额外的编号分配。

```c
/* 源码位置: tccpp.c next_nomacro() */
case '(':  case ')':
case '[':  case ']':
case '{':  case '}':
case ',':
case ';':
case ':':
case '?':
case '~':
parse_simple:
    tok = c;    /* 直接使用 ASCII 值作为 token 编号 */
    p++;
    break;
```

这意味着 `(` 的 token 编号就是 40（ASCII 值），`{` 就是 123，以此类推。在解析器中，可以直接用字符常量来匹配：

```c
/* 解析器中的典型用法 */
if (tok == '(') {
    /* 处理左括号 */
}
```

**单字符 token 完整列表：**

| 字符 | ASCII (十六进制) | 语义 |
|------|------------------|------|
| ` `  | 0x20 | 空格（当 `PARSE_FLAG_SPACES` 启用时返回） |
| `!`  | 0x21 | 逻辑非 / `!=` 的起始 |
| `%`  | 0x25 | 取模 / `%=` 的起始 |
| `&`  | 0x26 | 按位与 / `&&` / `&=` 的起始 |
| `(`  | 0x28 | 左圆括号 |
| `)`  | 0x29 | 右圆括号 |
| `*`  | 0x2A | 乘法 / `*=` 的起始 |
| `+`  | 0x2B | 加法 / `++` / `+=` 的起始 |
| `,`  | 0x2C | 逗号 |
| `-`  | 0x2D | 减法 / `--` / `->` / `-=` 的起始 |
| `.`  | 0x2E | 点 / `...` / `..` 的起始 |
| `/`  | 0x2F | 除法 / 注释 / `/=` 的起始 |
| `:`  | 0x3A | 冒号 |
| `;`  | 0x3B | 分号 |
| `<`  | 0x3C | 小于 / `<=` / `<<` 的起始 |
| `=`  | 0x3D | 赋值 / `==` 的起始 |
| `>`  | 0x3E | 大于 / `>=` / `>>` 的起始 |
| `?`  | 0x3F | 问号 |
| `[`  | 0x5B | 左方括号 |
| `]`  | 0x5D | 右方括号 |
| `^`  | 0x5E | 按位异或 / `^=` 的起始 |
| `{`  | 0x7B | 左花括号 |
| `\|` | 0x7C | 按位或 / `\|\|` / `\|=` 的起始 |
| `}`  | 0x7D | 右花括号 |
| `~`  | 0x7E | 按位取反 |

### 2.2.3 内部操作符（0x80-0xAF）

这个区间包含两种类型的操作符：**一元/二元操作符**和**多字符操作符**。

**一元和内部操作符（0x80-0x8F）：**

| Token | 编号 | 说明 |
|-------|------|------|
| `TOK_DEC` | 0x80 | `--`（自减） |
| `TOK_MID` | 0x81 | 增减操作中间值，也用于一元负号 (`TOK_NEG`) |
| `TOK_INC` | 0x82 | `++`（自增） |
| `TOK_UDIV` | 0x83 | 无符号除法（内部使用） |
| `TOK_UMOD` | 0x84 | 无符号取模（内部使用） |
| `TOK_PDIV` | 0x85 | 指针除法（内部使用） |
| `TOK_UMULL` | 0x86 | 无符号 32x32->64 乘法（内部使用） |
| `TOK_ADDC1` | 0x87 | 带进位加法（生成进位） |
| `TOK_ADDC2` | 0x88 | 带进位加法（使用进位） |
| `TOK_SUBC1` | 0x89 | 带借位减法（生成借位） |
| `TOK_SUBC2` | 0x8A | 带借位减法（使用借位） |
| `TOK_SHR`  | 0x8B | 无符号右移（内部使用） |

注意：`TOK_SHL` 和 `TOK_SAR` 被特殊处理——它们分别被定义为 `'<'` 和 `'>'`（即 0x3C 和 0x3E），这是 tcc 的一个巧妙设计，使得移位操作可以复用比较操作的某些代码路径。

**条件和比较操作符（0x90-0x9F）：**

| Token | 编号 | 说明 |
|-------|------|------|
| `TOK_LAND` | 0x90 | `&&`（逻辑与） |
| `TOK_LOR`  | 0x91 | `\|\|`（逻辑或） |
| `TOK_ULT`  | 0x92 | 无符号小于（内部） |
| `TOK_UGE`  | 0x93 | 无符号大于等于（内部） |
| `TOK_EQ`   | 0x94 | `==`（相等） |
| `TOK_NE`   | 0x95 | `!=`（不等） |
| `TOK_ULE`  | 0x96 | 无符号小于等于（内部） |
| `TOK_UGT`  | 0x97 | 无符号大于（内部） |
| `TOK_Nset` | 0x98 | 位测试置位（内部） |
| `TOK_Nclear`| 0x99 | 位测试清除（内部） |
| `TOK_LT`   | 0x9C | `<`（小于） |
| `TOK_GE`   | 0x9D | `>=`（大于等于） |
| `TOK_LE`   | 0x9E | `<=`（小于等于） |
| `TOK_GT`   | 0x9F | `>`（大于） |

> **设计洞察**：0x90-0x9F 区间同时包含**源码级操作符**（如 `==`、`!=`）和**内部操作符**（如 `TOK_ULT`、`TOK_UGE`）。内部操作符只在代码生成阶段出现，它们用于区分有符号和无符号比较——这在 x86 汇编中对应不同的条件跳转指令。`TOK_ISCOND(t)` 宏用于判断一个 token 是否属于条件操作符。

**其他多字符操作符（0xA0-0xAF）：**

| Token | 编号 | 说明 |
|-------|------|------|
| `TOK_ARROW`    | 0xA0 | `->`（结构体成员访问） |
| `TOK_DOTS`     | 0xA1 | `...`（可变参数） |
| `TOK_TWODOTS`  | 0xA2 | `..`（C++ 兼容） |
| `TOK_TWOSHARPS`| 0xA3 | `##`（预处理记号拼接） |
| `TOK_PLCHLDR`  | 0xA4 | 占位符 token（C99） |
| `TOK_PPJOIN`   | 0xA3\|SYM_FIELD | 宏展开中的 `##` 拼接标记 |
| `TOK_SOTYPE`   | 0xA7 | `sizeof(type)` 中 `(` 的别名 |

### 2.2.4 复合赋值操作符（0xB0-0xB9）

| Token | 编号 | 说明 |
|-------|------|------|
| `TOK_A_ADD` | 0xB0 | `+=` |
| `TOK_A_SUB` | 0xB1 | `-=` |
| `TOK_A_MUL` | 0xB2 | `*=` |
| `TOK_A_DIV` | 0xB3 | `/=` |
| `TOK_A_MOD` | 0xB4 | `%=` |
| `TOK_A_AND` | 0xB5 | `&=` |
| `TOK_A_OR`  | 0xB6 | `\|=` |
| `TOK_A_XOR` | 0xB7 | `^=` |
| `TOK_A_SHL` | 0xB8 | `<<=` |
| `TOK_A_SAR` | 0xB9 | `>>=` |

赋值操作符的设计非常紧凑。`TOK_ASSIGN(t)` 宏通过范围检查判断一个 token 是否是赋值操作符：

```c
#define TOK_ASSIGN(t) (t >= TOK_A_ADD && t <= TOK_A_SAR)
```

`TOK_ASSIGN_OP(t)` 宏则从赋值操作符反推出对应的二元操作符字符：

```c
#define TOK_ASSIGN_OP(t) ("+-*/%&|^<>"[t - TOK_A_ADD])
```

这个查找表利用了赋值操作符编号的连续性，将 `0xB0-0xB9` 映射到 `"+-*/%&|^<>"` 中的对应字符。

### 2.2.5 带值常量 Token（0xC0-0xCF）

这些 token 在 `tokc`（类型为 `CValue`）中携带附加的值信息。`TOK_HAS_VALUE(t)` 宏用于判断一个 token 是否需要额外的值解析。

| Token | 编号 | 值存储位置 | 说明 |
|-------|------|-----------|------|
| `TOK_CCHAR`   | 0xC0 | `tokc.i` | 字符常量 `'a'` |
| `TOK_LCHAR`   | 0xC1 | `tokc.i` | 宽字符常量 `L'a'` |
| `TOK_CINT`     | 0xC2 | `tokc.i` | 整数常量 |
| `TOK_CUINT`    | 0xC3 | `tokc.i` | 无符号整数常量 |
| `TOK_CLLONG`   | 0xC4 | `tokc.i` | long long 常量 |
| `TOK_CULLONG`  | 0xC5 | `tokc.i` | unsigned long long 常量 |
| `TOK_CLONG`    | 0xC6 | `tokc.i` | long 常量 |
| `TOK_CULONG`   | 0xC7 | `tokc.i` | unsigned long 常量 |
| `TOK_STR`      | 0xC8 | `tokc.str` | 字符串常量 |
| `TOK_LSTR`     | 0xC9 | `tokc.str` | 宽字符串常量 |
| `TOK_CFLOAT`   | 0xCA | `tokc.f` | float 常量 |
| `TOK_CDOUBLE`  | 0xCB | `tokc.d` | double 常量 |
| `TOK_CLDOUBLE` | 0xCC | `tokc.ld` | long double 常量 |
| `TOK_PPNUM`    | 0xCD | `tokc.str` | 预处理数字（未解析） |
| `TOK_PPSTR`    | 0xCE | `tokc.str` | 预处理字符串（未解析） |
| `TOK_LINENUM`  | 0xCF | `tokc.i` | 行号信息（宏展开内部使用） |

> **关键区分**：`TOK_PPNUM` 和 `TOK_PPSTR` 是预处理器阶段使用的"原始" token。在预处理阶段，数字和字符串以原始文本形式存储在 `tokc.str` 中。只有当 `PARSE_FLAG_TOK_NUM` 或 `PARSE_FLAG_TOK_STR` 标志启用时，`next()` 函数才会将它们转换为具体的 `TOK_CINT`、`TOK_STR` 等类型。

### 2.2.6 标识符和关键字（>= 256）

所有标识符和关键字的 token 编号都 >= `TOK_IDENT`（256）。它们通过 `table_ident` 数组管理，每个标识符对应一个 `TokenSym` 结构体。

关键字的编号从 `TOK_IDENT` 之后开始，通过 `tcctok.h` 中的 `DEF` 宏按顺序分配：

```c
/* tcc.h */
enum tcc_token {
    TOK_LAST = TOK_IDENT - 1
#define DEF(id, str) ,id
#include "tcctok.h"
#undef DEF
};
```

这意味着 `tcctok.h` 中第一个 `DEF` 的编号是 256，第二个是 257，以此类推。关键字和标识符共享同一个编号空间，区分它们的方法是检查编号是否在 `[TOK_IDENT, TOK_UIDENT)` 范围内：

```c
#define TOK_UIDENT TOK_DEFINE  /* 第一个非关键字标识符的编号 */
```

如果 `tok >= TOK_IDENT && tok < TOK_UIDENT`，则 `tok` 是关键字；否则是用户标识符。

---

## 2.3 tcctok.h 关键字定义

### 2.3.1 DEF 宏的巧妙设计

`tcctok.h` 不是一个普通的头文件——它是一个被多次包含的"X-macro"文件。每次包含时，`DEF` 宏的定义不同，从而产生不同的效果：

**第一次包含**：生成关键字字符串表

```c
static const char tcc_keywords[] =
#define DEF(id, str) str "\0"
#include "tcctok.h"
#undef DEF
;
```

这将所有关键字字符串连接成一个以 `\0` 分隔的大字符串：`"if\0else\0while\0for\0..."`

**第二次包含**：生成 token 编号枚举

```c
enum tcc_token {
    TOK_LAST = TOK_IDENT - 1
#define DEF(id, str) ,id
#include "tcctok.h"
#undef DEF
};
```

这将 `TOK_IF`、`TOK_ELSE` 等依次定义为枚举常量，从 `TOK_IDENT`(256) 开始递增。

**第三次包含**：在 `tccpp_new()` 中注册关键字

```c
p = tcc_keywords;
while (*p) {
    r = p;
    for(;;) {
        c = *r++;
        if (c == '\0')
            break;
    }
    tok_alloc(p, r - p - 1);  /* 注册关键字到哈希表 */
    p = r;
}
```

### 2.3.2 关键字分类

`tcctok.h` 中的条目按功能分为以下几大类：

**（1）C 语言控制流关键字**

```
if, else, while, for, do, continue, break, return, goto,
switch, case, default
```

**（2）asm 关键字（三种变体）**

```
asm, __asm, __asm__          /* TOK_ASM1, TOK_ASM2, TOK_ASM3 */
```

tcc 支持三种 asm 语法变体以兼容不同的 C 方言和 GCC 扩展。

**（3）存储类和类型修饰符**

```
extern, static, auto, register, typedef
const, __const, __const__         /* 三种变体 */
volatile, __volatile, __volatile__
signed, __signed, __signed__
unsigned
inline, __inline, __inline__
restrict, __restrict, __restrict__
__extension__
_Atomic, _Thread_local, __thread
```

> **设计说明**：几乎所有 GCC 风格的关键字都有双下划线变体。`__const` 和 `__const__` 都是 `const` 的同义词——这是 GCC 的惯例，允许在系统头文件中使用 `__const` 避免与用户定义的宏冲突。

**（4）类型关键字**

```
void, char, int, float, double
_Bool, _Complex                 /* C99/C11 类型 */
short, long
struct, union, enum
sizeof, _Alignof, __alignof, __alignof__
_Alignas
typeof, __typeof, __typeof__   /* GCC 扩展 */
__attribute, __attribute__
__label__
_Generic, _Static_assert       /* C11 特性 */
```

**（5）预处理器关键字**

这些不是 C 语言的关键字，而是预处理器指令中使用的标识符：

```
define, include, include_next, ifdef, ifndef, elif,
endif, defined, undef, error, warning, line, pragma
```

以及预定义宏：

```
__LINE__, __FILE__, __DATE__, __TIME__,
__FUNCTION__, __VA_ARGS__, __COUNTER__,
__has_include, __has_include_next
```

**（6）属性标识符**

GCC 的 `__attribute__` 机制需要识别大量的属性名称：

```
section, __section__           /* 函数/变量段 */
aligned, __aligned__           /* 对齐 */
packed, __packed__             /* 紧凑结构 */
weak, __weak__                 /* 弱符号 */
alias, __alias__               /* 符号别名 */
used, __used__                 /* 防止未使用警告 */
unused, __unused__             /* 标记未使用 */
format, __format__             /* printf/scanf 格式检查 */
cdecl, __cdecl, __cdecl__     /* 调用约定 */
stdcall, __stdcall, __stdcall__
fastcall, __fastcall, __fastcall__
noreturn, __noreturn__, _Noreturn
visibility, __visibility__
constructor, __constructor__
destructor, __destructor__
always_inline, __always_inline__
cleanup, __cleanup__
```

**（7）内建函数标识符**

```
__builtin_types_compatible_p
__builtin_choose_expr
__builtin_constant_p
__builtin_frame_address
__builtin_return_address
__builtin_expect
__builtin_unreachable
```

以及平台相关的内建函数（如 `__builtin_va_start`、`__builtin_va_arg` 等）。

**（8）原子操作标识符**

```
__atomic_store, __atomic_load, __atomic_exchange,
__atomic_compare_exchange,
__atomic_fetch_add, __atomic_fetch_sub,
__atomic_fetch_or, __atomic_fetch_xor,
__atomic_fetch_and, __atomic_fetch_nand,
__atomic_add_fetch, __atomic_sub_fetch,
__atomic_or_fetch, __atomic_xor_fetch,
__atomic_and_fetch, __atomic_nand_fetch
```

这些通过 `DEF_ATOMIC` 宏批量定义：

```c
#define DEF_ATOMIC(ID) DEF(TOK_##__##ID, "__"#ID)
```

**（9）汇编器指令**

`tcctok.h` 的末尾还包含汇编器（Tiny Assembler）使用的指令和关键字，通过 `DEF_ASM` 和 `DEF_ASMDIR` 宏定义：

```c
#define DEF_ASM(x) DEF(TOK_ASM_ ## x, #x)
#define DEF_ASMDIR(x) DEF(TOK_ASMDIR_ ## x, "." #x)
```

汇编指令包括 `.byte`、`.word`、`.align`、`.text`、`.data`、`.bss` 等。

此外，各目标架构还有自己的关键字文件（如 `i386-tok.h`、`arm-tok.h`、`arm64-tok.h`），通过条件 `#include` 加入。

---

## 2.4 核心数据结构

### 2.4.1 TokenSym —— 标识符符号表条目

```c
typedef struct TokenSym {
    struct TokenSym *hash_next;    /* 哈希链表的下一个节点（解决冲突） */
    struct Sym *sym_define;        /* 指向 #define 定义 */
    struct Sym *sym_label;         /* 指向标号定义 */
    struct Sym *sym_struct;        /* 指向结构体/联合体/枚举定义 */
    struct Sym *sym_identifier;    /* 指向标识符定义（变量/函数/typedef） */
    int tok;                       /* token 编号 */
    int len;                       /* 标识符名称长度 */
    char str[1];                   /* 标识符名称（柔性数组成员） */
} TokenSym;
```

`TokenSym` 是 tcc 符号表的核心结构。每个标识符（包括关键字）在 `table_ident` 数组中都有一个对应的 `TokenSym` 条目。它同时扮演两个角色：

1. **词法符号表**：通过 `tok` 编号和 `str` 名称标识一个词法单元
2. **语义符号表入口**：通过四个 `Sym*` 指针直接链接到各种语义定义

这种"一个结构体同时服务于词法分析和语义分析"的设计避免了额外的查找开销。当解析器遇到一个标识符时，可以通过 `TokenSym` 直接访问它的所有定义信息，无需再次哈希查找。

**柔性数组成员** `str[1]` 是 C 语言的一个技巧。`TokenSym` 实际分配的内存大小为 `sizeof(TokenSym) + len`，`str` 字段的地址就是字符串的起始位置，避免了额外的指针间接访问。

### 2.4.2 CValue —— 常量值联合体

```c
typedef union CValue {
    long double ld;                /* long double 常量 */
    double d;                      /* double 常量 */
    float f;                       /* float 常量 */
    uint64_t i;                    /* 整数常量 */
    struct {
        char *data;                /* 字符串数据指针 */
        int size;                  /* 字符串大小（含尾部 \0） */
    } str;                         /* 字符串/预处理数字 */
    int tab[LDOUBLE_WORDS];        /* 按 int 数组访问（用于序列化） */
} CValue;
```

`CValue` 是一个联合体，用于存储 token 的附加值。它的设计考虑了以下几个因素：

- **类型多样性**：需要存储整数、浮点数、字符串等不同类型
- **平台一致性**：`LDOUBLE_WORDS` 根据 `sizeof(long double)` 动态计算，确保在不同平台上正确工作
- **序列化支持**：`tab` 成员允许按 `int` 数组访问整个联合体，用于 token 字符串的序列化（`tok_get` 函数）

使用示例：

```c
/* 存储整数常量 42 */
tokc.i = 42;        /* tok = TOK_CINT */

/* 存储浮点常量 3.14 */
tokc.d = 3.14;      /* tok = TOK_CDOUBLE */

/* 存储字符串 "hello" */
tokc.str.data = "hello";
tokc.str.size = 6;  /* 含 \0 */
/* tok = TOK_STR */
```

### 2.4.3 BufferedFile —— 文件缓冲区

```c
typedef struct BufferedFile {
    uint8_t *buf_ptr;              /* 当前读取位置 */
    uint8_t *buf_end;              /* 缓冲区有效数据的末尾 */
    int fd;                        /* 文件描述符 */
    struct BufferedFile *prev;     /* include 栈中的上一个文件 */
    int line_num;                  /* 当前行号 */
    int line_ref;                  /* tcc -E: 上次输出的行号 */
    int ifndef_macro;              /* #ifndef 宏 / #endif 搜索 */
    int ifndef_macro_saved;        /* 保存的 ifndef_macro */
    int *ifdef_stack_ptr;          /* 文件开始时的 ifdef_stack 值 */
    int include_next_index;        /* 下一个搜索路径 */
    int prev_tok_flags;            /* 保存的 tok_flags */
    char filename[1024];           /* 文件名 */
    char *true_filename;           /* 未被 #line 修改的真实文件名 */
    unsigned char unget[4];        /* 回退字符缓冲区 */
    unsigned char buffer[1];       /* I/O 缓冲区（柔性数组成员） */
} BufferedFile;
```

`BufferedFile` 是 tcc 文件 I/O 的核心结构。它将文件读取缓冲和文件元信息（行号、include 栈等）整合在一个结构体中。

**关键字段详解：**

- `buf_ptr` / `buf_end`：这对指针定义了缓冲区中有效的数据范围。`buf_ptr` 指向下一个要读取的字节，`buf_end` 指向有效数据的末尾。当 `buf_ptr == buf_end` 时，需要从文件重新读取。

- `unget[4]`：这是一个小型的"回退缓冲区"。当词法分析器需要"放回"一个字符时（例如，它多读了一个字符但发现不属于当前 token），它通过将 `buf_ptr` 递减来实现。`unget` 数组确保即使 `buf_ptr` 回退到 `buffer` 之前的位置，也有合法的内存可以写入。

- `buffer[1]`：柔性数组成员。实际分配时，`BufferedFile` 的大小为 `sizeof(BufferedFile) + IO_BUF_SIZE`，其中 `IO_BUF_SIZE = 8192`。

- `prev`：形成一个栈结构。当处理 `#include` 时，新的 `BufferedFile` 被压入栈；当 include 文件结束时，从栈中弹出。

- `ifndef_macro` / `ifndef_macro_saved`：用于 `#ifndef` 保护头文件的优化。如果一个头文件以 `#ifndef _GUARD_H` 开头并以 `#endif` 结尾，tcc 可以缓存这个结果，避免重复包含时的文件 I/O 和解析开销。

### 2.4.4 TokenString —— Token 序列

```c
typedef struct TokenString {
    int *str;                      /* token 序列数据 */
    int len;                       /* 当前长度（以 int 为单位） */
    int need_spc;                  /* 是否需要空格分隔 */
    int allocated_len;             /* 已分配的长度 */
    int last_line_num;             /* 最后一行号 */
    int save_line_num;             /* 保存的行号（用于宏展开） */
    struct TokenString *prev;      /* 链接到上一个 TokenString */
    const int *prev_ptr;           /* 上一个 macro_ptr 值 */
    char alloc;                    /* 分配类型：0=静态, 1=动态, 2=不释放 */
} TokenString;
```

`TokenString` 用于存储 token 序列，主要服务于以下场景：

1. **宏定义存储**：`#define` 的替换体被存储为 `TokenString`
2. **宏展开结果**：宏展开的输出 token 序列
3. **unget 缓冲**：`unget_tok()` 函数将 token 推回

`str` 数组中的每个元素是一个 `int`，但带值的 token（`TOK_HAS_VALUE` 为真的 token）会占用多个 `int`：第一个是 token 编号，后续的是 `CValue` 的序列化形式。`tok_get()` 函数负责从 `TokenString` 中读取一个完整的 token（编号 + 值）。

### 2.4.5 isidnum_table —— 字符分类表

```c
static unsigned char isidnum_table[256 - CH_EOF];
```

这是一个 257 字节的查找表（因为 `CH_EOF = -1`，所以索引范围是 `-1` 到 `255`，即 `c - CH_EOF` 的范围是 `0` 到 `256`）。每个字节的位标志定义了对应字符的类别：

| 标志位 | 值 | 含义 |
|--------|-----|------|
| `IS_SPC` | 0x01 | 空白字符（空格、制表符等） |
| `IS_ID`  | 0x02 | 可以出现在标识符中的字符（字母、下划线、高位字符） |
| `IS_NUM` | 0x04 | 数字字符 |

初始化代码：

```c
/* tccpp_new() */
for(i = CH_EOF; i < 128; i++)
    set_idnum(i,
        is_space(i) ? IS_SPC
        : isid(i)   ? IS_ID
        : isnum(i)  ? IS_NUM
        : 0);

for(i = 128; i < 256; i++)
    set_idnum(i, IS_ID);  /* 高位字节视为标识符字符（UTF-8 支持） */
```

> **性能关键**：在 `next_nomacro()` 的标识符扫描循环中，`isidnum_table` 查找是内层循环的主要操作。使用表查找而非条件判断（`if (isalpha(c) || isdigit(c) || c == '_')`）可以显著提高性能，因为它将多次比较转化为一次内存访问。

`set_idnum()` 函数允许运行时修改字符分类，例如：
- 当 `dollars_in_identifiers` 启用时，`$` 被标记为 `IS_ID`
- 在汇编模式下，`.` 被标记为 `IS_ID`

---

## 2.5 文件 I/O 与缓冲

### 2.5.1 缓冲区结构

tcc 使用固定大小的缓冲区（`IO_BUF_SIZE = 8192` 字节）来读取源文件。缓冲区的结构如下：

```
BufferedFile.buffer:
┌──────────────────────────────────────────┬──────┐
│          有效数据 (0 ~ 8191)              │ CH_EOB│
└──────────────────────────────────────────┴──────┘
                                    ↑       ↑
                                  buf_ptr  buf_end
```

缓冲区末尾始终放置一个 `CH_EOB`（值为 `'\\'`，即 0x5C）哨兵字符。这个设计的精妙之处在于：

1. `CH_EOB` 的值恰好是反斜杠 `\`，这意味着正常情况下缓冲区末尾不会出现这个值（除非源文件中真的有 `\`）
2. 哨兵字符使得词法分析器的内层循环不需要在每次迭代时检查 `buf_ptr < buf_end`

### 2.5.2 handle_eob() —— 缓冲区重填

当 `buf_ptr` 到达 `buf_end` 时，`handle_eob()` 被调用：

```c
static int handle_eob(void)
{
    BufferedFile *bf = file;
    int len;

    if (bf->buf_ptr >= bf->buf_end) {
        if (bf->fd >= 0) {
            len = read(bf->fd, bf->buffer, IO_BUF_SIZE);
            if (len < 0)
                len = 0;
        } else {
            len = 0;
        }
        total_bytes += len;
        bf->buf_ptr = bf->buffer;
        bf->buf_end = bf->buffer + len;
        *bf->buf_end = CH_EOB;  /* 放置哨兵 */
    }
    if (bf->buf_ptr < bf->buf_end) {
        return bf->buf_ptr[0];
    } else {
        bf->buf_ptr = bf->buf_end;
        return CH_EOF;
    }
}
```

**工作流程：**

1. 检查 `buf_ptr >= buf_end`（真正到达缓冲区末尾）
2. 调用 `read()` 从文件描述符读取 `IO_BUF_SIZE` 字节
3. 重置 `buf_ptr` 到 `buffer` 起始位置
4. 设置 `buf_end` 到实际读取的数据末尾
5. 在 `buf_end` 位置放置 `CH_EOB` 哨兵
6. 如果读取了数据，返回第一个字节；否则返回 `CH_EOF`

### 2.5.3 next_c() —— 读取下一个字符

```c
static int next_c(void)
{
    int ch = *++file->buf_ptr;
    if (ch == CH_EOB && file->buf_ptr >= file->buf_end)
        ch = handle_eob();
    return ch;
}
```

`next_c()` 是最底层的字符读取函数。它的设计非常精巧：

1. **先递增再读取**：`*++file->buf_ptr` 先将指针前移，再读取。这意味着 `buf_ptr` 始终指向"刚读取的字符"，而不是"下一个要读取的字符"。这是为了与 `next_nomacro()` 中的 `PEEKC` 宏配合。

2. **哨兵检查**：只有当读取到 `CH_EOB` **且** `buf_ptr >= buf_end` 时，才调用 `handle_eob()`。如果 `CH_EOB` 出现在缓冲区中间（源文件中真的有 `\`），则不会触发重填。

### 2.5.4 行拼接（Line Splicing）

C 语言允许用 `\` 结尾的行来续行：

```c
int a = 1 + \
        2 + 3;
```

tcc 在 `handle_stray_noerror()` 函数中处理行拼接：

```c
static int handle_stray_noerror(int err)
{
    int ch;
    while ((ch = next_c()) == '\\') {
        ch = next_c();
        if (ch == '\n') {
    newl:
            file->line_num++;     /* 续行：跳过 \ 和 \n */
        } else {
            if (ch == '\r') {     /* 处理 \r\n 行尾 */
                ch = next_c();
                if (ch == '\n')
                    goto newl;
                *--file->buf_ptr = '\r';
            }
            if (err)
                tcc_error("stray '\\' in program");
            return *--file->buf_ptr = '\\';
        }
    }
    return ch;
}
```

这个函数通过 `while` 循环处理连续的续行（`\\\n\\\n\\\n...`）。当遇到 `\` 后面跟着 `\n` 时，跳过这两个字符并递增行号；否则，将 `\` 放回缓冲区并返回。

> **注意 `\r\n` 的处理**：Windows 风格的行尾 `\r\n` 也被正确处理。当 `\` 后面是 `\r` 时，再读取一个字符检查是否是 `\n`。

---

## 2.6 next_nomacro_spc() 底层扫描器

`next_nomacro()` 是 tcc 词法分析器的核心函数。它从输入缓冲区读取字符并识别出下一个 token，但**不进行宏展开**。函数实现为一个巨大的 `switch` 语句，每个 `case` 处理一种类型的 token。

### 2.6.1 函数签名和入口

```c
static void next_nomacro(void)
{
    int t, c, is_long, len;
    TokenSym *ts;
    uint8_t *p, *p1;
    unsigned int h;

    p = file->buf_ptr;
 redo_no_start:
    c = *p;
    switch(c) {
    /* ... */
    }
    tok_flags = 0;
keep_tok_flags:
    file->buf_ptr = p;
}
```

注意函数使用本地指针 `p` 来遍历缓冲区，而不是每次都通过 `file->buf_ptr` 间接访问。这是一个重要的性能优化——编译器可以将 `p` 保持在寄存器中。函数结束时才将 `p` 写回 `file->buf_ptr`。

`redo_no_start` 标签用于在不重置 `tok_flags` 的情况下重新开始扫描（例如跳过空白后）。

### 2.6.2 空白和换行处理

```c
case ' ':
case '\t':
    tok = c;
    p++;
maybe_space:
    if (parse_flags & PARSE_FLAG_SPACES)
        goto keep_tok_flags;     /* -E 模式：保留空格 token */
    while (isidnum_table[*p - CH_EOF] & IS_SPC)
        ++p;
    goto redo_no_start;          /* 跳过所有空白，重新开始 */

case '\f':
case '\v':
case '\r':
    p++;
    goto redo_no_start;          /* 无条件跳过这些空白字符 */

case '\n':
    file->line_num++;
    p++;
maybe_newline:
    tok_flags |= TOK_FLAG_BOL;  /* 标记行首 */
    if (0 == (parse_flags & PARSE_FLAG_LINEFEED))
        goto redo_no_start;      /* 默认不返回换行 token */
    tok = TOK_LINEFEED;
    goto keep_tok_flags;
```

**设计要点：**

- 空格和制表符在默认模式下被跳过；只有当 `PARSE_FLAG_SPACES` 启用时（`tcc -E` 预处理输出模式），它们才作为 token 返回
- 换行符总是递增行号，但只有当 `PARSE_FLAG_LINEFEED` 启用时才返回 `TOK_LINEFEED` token
- `TOK_FLAG_BOL` 标记用于预处理器——`#` 指令只在行首有效

### 2.6.3 标识符的快速路径

这是 `next_nomacro()` 中最热的代码路径之一：

```c
case 'a': case 'b': case 'c': case 'd':
case 'e': case 'f': case 'g': case 'h':
case 'i': case 'j': case 'k': case 'l':
case 'm': case 'n': case 'o': case 'p':
case 'q': case 'r': case 's': case 't':
case 'u': case 'v': case 'w': case 'x':
case 'y': case 'z':
case 'A': case 'B': case 'C': case 'D':
case 'E': case 'F': case 'G': case 'H':
case 'I': case 'J': case 'K':
case 'M': case 'N': case 'O': case 'P':
case 'Q': case 'R': case 'S': case 'T':
case 'U': case 'V': case 'W': case 'X':
case 'Y': case 'Z':
case '_':
parse_ident_fast:
    p1 = p;
    h = TOK_HASH_INIT;                    /* h = 1 */
    h = TOK_HASH_FUNC(h, c);             /* 计算第一个字符的哈希 */
    while (c = *++p, isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
        h = TOK_HASH_FUNC(h, c);         /* 在扫描的同时计算哈希 */
    len = p - p1;
    if (c != '\\') {
        /* 快速路径：没有续行符，直接在哈希表中查找 */
        TokenSym **pts;
        h &= (TOK_HASH_SIZE - 1);        /* 取模（TOK_HASH_SIZE 是 2 的幂） */
        pts = &hash_ident[h];
        for(;;) {
            ts = *pts;
            if (!ts)
                break;
            if (ts->len == len && !memcmp(ts->str, p1, len))
                goto token_found;
            pts = &(ts->hash_next);
        }
        ts = tok_alloc_new(pts, (char *) p1, len);
    token_found: ;
    } else {
        /* 慢速路径：有续行符，需要处理行拼接 */
        cstr_reset(&tokcstr);
        cstr_cat(&tokcstr, (char *) p1, len);
        p--;
        PEEKC(c, p);
        while (isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
        {
            cstr_ccat(&tokcstr, c);
            PEEKC(c, p);
        }
        ts = tok_alloc(tokcstr.data, tokcstr.size);
    }
    tok = ts->tok;
    break;
```

**快速路径的关键优化：**

1. **哈希计算与扫描同步**：在逐字符读取标识符的同时计算哈希值，避免了单独的哈希计算遍历
2. **直接内存比较**：使用 `memcmp(ts->str, p1, len)` 比较标识符，而不是逐字符比较
3. **位运算取模**：`h &= (TOK_HASH_SIZE - 1)` 代替 `h % TOK_HASH_SIZE`，因为 `TOK_HASH_SIZE` 是 2 的幂（16384）
4. **避免续行检查**：在常见情况下（没有 `\`），跳过续行处理

> **注意 `case 'L'` 的特殊处理**：字母 `L` 单独处理，因为它可能是宽字符/字符串的前缀（`L'x'` 或 `L"string"`）。如果不是宽字符串前缀，则跳转到 `parse_ident_fast`。

### 2.6.4 数字扫描

```c
case '0': case '1': case '2': case '3':
case '4': case '5': case '6': case '7':
case '8': case '9':
    t = c;
    PEEKC(c, p);
parse_num:
    cstr_reset(&tokcstr);
    for(;;) {
        cstr_ccat(&tokcstr, t);
        if (!((isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
              || c == '.'
              || ((c == '+' || c == '-')
                  && (((t == 'e' || t == 'E')
                        && !(parse_flags & PARSE_FLAG_ASM_FILE
                            && ((char*)tokcstr.data)[0] == '0'
                            && toup(((char*)tokcstr.data)[1]) == 'X'))
                      || t == 'p' || t == 'P'))))
            break;
        t = c;
        PEEKC(c, p);
    }
    cstr_ccat(&tokcstr, '\0');
    tokc.str.size = tokcstr.size;
    tokc.str.data = tokcstr.data;
    tok = TOK_PPNUM;
    break;
```

在预处理阶段，数字被识别为 `TOK_PPNUM`——一个"未解析"的数字 token。原始的数字文本被保存在 `tokc.str` 中。这种"延迟解析"的设计有以下好处：

1. 预处理器不需要理解数字的语义（如进制、类型后缀）
2. `#if` 表达式中的数字可能有不同的解析规则
3. 简化了 `next_nomacro()` 的逻辑

数字的真正解析（进制判断、类型推断等）在 `parse_number()` 中完成，由 `next()` 在需要时调用。

**数字扫描的边界条件：**

数字 token 的扫描需要处理许多边界情况：
- `0x1e+2` 在汇编模式下是三个 token（`0x1e`、`+`、`2`），而在 C 模式下是一个浮点数
- `1.23e-4` 中的 `-` 是指数的一部分，不是减法操作符
- `.` 可以是数字的开始（如 `.5`）

### 2.6.5 字符串扫描

```c
case '\'':
case '\"':
    is_long = 0;
str_const:
    cstr_reset(&tokcstr);
    if (is_long)
        cstr_ccat(&tokcstr, 'L');
    cstr_ccat(&tokcstr, c);           /* 保存引号 */
    p = parse_pp_string(p, c, &tokcstr);  /* 扫描字符串内容 */
    cstr_ccat(&tokcstr, c);           /* 保存闭合引号 */
    cstr_ccat(&tokcstr, '\0');
    tokc.str.size = tokcstr.size;
    tokc.str.data = tokcstr.data;
    tok = TOK_PPSTR;
    break;
```

与数字类似，字符串在预处理阶段也以原始形式（`TOK_PPSTR`）保存。`parse_pp_string()` 负责扫描字符串内容，处理转义序列和续行，但不解析转义序列的值。

### 2.6.6 多字符操作符

tcc 使用两种机制识别多字符操作符：

**机制一：`tok_two_chars` 查找表**

```c
static const unsigned char tok_two_chars[] = {
    '<','=', TOK_LE,
    '>','=', TOK_GE,
    '!','=', TOK_NE,
    '&','&', TOK_LAND,
    '|','|', TOK_LOR,
    '+','+', TOK_INC,
    '-','-', TOK_DEC,
    '=','=', TOK_EQ,
    '<','<', TOK_SHL,
    '>','>', TOK_SAR,
    '+','=', TOK_A_ADD,
    '-','=', TOK_A_SUB,
    '*','=', TOK_A_MUL,
    '/','=', TOK_A_DIV,
    '%','=', TOK_A_MOD,
    '&','=', TOK_A_AND,
    '^','=', TOK_A_XOR,
    '|','=', TOK_A_OR,
    '-','>', TOK_ARROW,
    '.','.', TOK_TWODOTS,
    '#','#', TOK_TWOSHARPS,
    0
};
```

这个表在 `get_tok_str()` 中用于将 token 编号转换回字符串表示。

**机制二：手动的 `switch`/`if` 链**

实际的扫描使用手动编码的判断逻辑，以处理三字符操作符（如 `<<=`）和上下文相关的歧义：

```c
case '<':
    PEEKC(c, p);
    if (c == '=') {
        p++;
        tok = TOK_LE;        /* <= */
    } else if (c == '<') {
        PEEKC(c, p);
        if (c == '=') {
            p++;
            tok = TOK_A_SHL;  /* <<= */
        } else {
            tok = TOK_SHL;    /* << */
        }
    } else {
        tok = TOK_LT;         /* < */
    }
    break;
```

同样，`=` 的处理需要区分 `=`（赋值）和 `==`（比较），`!` 需要区分 `!`（逻辑非）和 `!=`（不等）等。

tcc 还使用了一个宏 `PARSE2` 来简化两字符操作符的处理：

```c
#define PARSE2(c1, tok1, c2, tok2)    \
    case c1:                           \
        PEEKC(c, p);                   \
        if (c == c2) {                 \
            p++;                       \
            tok = tok2;                \
        } else {                       \
            tok = tok1;                \
        }                              \
        break;
```

使用示例：

```c
PARSE2('!', '!', '=', TOK_NE)    /* ! 或 != */
PARSE2('=', '=', '=', TOK_EQ)    /* = 或 == */
PARSE2('*', '*', '=', TOK_A_MUL) /* * 或 *= */
```

### 2.6.7 注释处理

```c
case '/':
    PEEKC(c, p);
    if (c == '*') {
        p = parse_comment(p);       /* C 风格注释 */
        tok = ' ';
        goto maybe_space;
    } else if (c == '/') {
        p = parse_line_comment(p);  /* C++ 风格注释 */
        tok = ' ';
        goto maybe_space;
    } else if (c == '=') {
        p++;
        tok = TOK_A_DIV;           /* /= */
    } else {
        tok = '/';                  /* 除法 */
    }
    break;
```

注释被替换为空格 token，然后跳转到 `maybe_space` 继续跳过后续的空白。这确保了注释不会意外地连接两个 token（例如 `a/* comment */b` 不会变成 `ab`）。

`parse_comment()` 函数处理 `/* ... */` 风格的注释，内部也使用了快速跳过循环和行拼接处理。

### 2.6.8 PEEKC 宏

```c
#define PEEKC(c, p)                    \
{                                      \
    c = *++p;                          \
    if (c == '\\')                     \
        c = handle_stray(&p);          \
}
```

`PEEKC` 是一个"预读"宏。它读取下一个字符，如果遇到 `\` 则调用 `handle_stray()` 处理可能的行拼接。这个宏在多字符操作符和数字扫描中广泛使用。

---

## 2.7 next() 带宏展开的扫描器

`next()` 是 tcc 对外提供的主要扫描接口。它在 `next_nomacro()` 的基础上增加了**宏展开**功能。

### 2.7.1 宏展开栈

tcc 使用一个全局变量 `macro_ptr` 和一个栈结构 `macro_stack` 来管理宏展开：

```c
ST_DATA const int *macro_ptr;    /* 当前正在读取的 TokenString 位置 */
static TokenString *macro_stack; /* 宏展开栈 */
```

当一个宏被展开时，展开结果被压入栈中，`macro_ptr` 指向展开结果的起始位置。`next()` 优先从 `macro_ptr` 读取 token，只有当 `macro_ptr == NULL`（栈为空）时才调用 `next_nomacro()` 从文件读取。

### 2.7.2 next() 的完整逻辑

```c
ST_FUNC void next(void)
{
    int t;
    while (macro_ptr) {
redo:
        t = *macro_ptr;
        if (TOK_HAS_VALUE(t)) {
            tok_get(&tok, &macro_ptr, &tokc);
            if (t == TOK_LINENUM) {
                file->line_num = tokc.i;
                goto redo;
            }
            goto convert;
        } else if (t == 0) {
            end_macro();          /* 宏展开结束 */
            continue;
        } else if (t == TOK_EOF) {
            /* 什么都不做 */
        } else {
            ++macro_ptr;
            t &= ~SYM_FIELD;     /* 移除 nosubst 标记 */
            if (t == '\\') {
                if (!(parse_flags & PARSE_FLAG_ACCEPT_STRAYS))
                    tcc_error("stray '\\' in program");
            }
        }
        tok = t;
        return;
    }

    /* 宏栈为空，从文件读取 */
    next_nomacro();
    t = tok;
    if (t >= TOK_IDENT && (parse_flags & PARSE_FLAG_PREPROCESS)) {
        Sym *s = define_find(t);  /* 查找是否有宏定义 */
        if (s) {
            Sym *nested_list = NULL;
            macro_subst_tok(&tokstr_buf, &nested_list, s);
            tok_str_add(&tokstr_buf, 0);
            begin_macro(&tokstr_buf, 0);  /* 将展开结果压入栈 */
            goto redo;                     /* 重新从栈中读取 */
        }
        return;
    }

convert:
    /* 将预处理 token 转换为 C token */
    if (t == TOK_PPNUM) {
        if (parse_flags & PARSE_FLAG_TOK_NUM)
            parse_number(tokc.str.data);
    } else if (t == TOK_PPSTR) {
        if (parse_flags & PARSE_FLAG_TOK_STR)
            parse_string(tokc.str.data, tokc.str.size - 1);
    }
}
```

### 2.7.3 TOK_LINENUM 机制

`TOK_LINENUM` 是一个特殊的内部 token。在宏展开过程中，tcc 会在展开结果中插入 `TOK_LINENUM` token 来记录行号变化。当 `next()` 遇到 `TOK_LINENUM` 时，它更新 `file->line_num` 但不返回这个 token——而是继续读取下一个真正的 token。

这确保了编译器错误信息中的行号始终是正确的。

### 2.7.4 begin_macro() 和 end_macro()

```c
ST_FUNC void begin_macro(TokenString *str, int alloc)
{
    str->alloc = alloc;
    str->prev = macro_stack;
    str->prev_ptr = macro_ptr;
    str->save_line_num = file->line_num;
    macro_ptr = str->str;
    macro_stack = str;
}

ST_FUNC void end_macro(void)
{
    TokenString *str = macro_stack;
    macro_stack = str->prev;
    macro_ptr = str->prev_ptr;
    file->line_num = str->save_line_num;
    if (str->alloc == 0) {
        str->len = str->need_spc = 0;
    } else {
        if (str->alloc == 2)
            str->str = NULL;
        tok_str_free(str);
    }
}
```

`begin_macro()` 将一个 `TokenString` 压入宏展开栈。`end_macro()` 弹出栈顶并恢复之前的上下文（`macro_ptr` 和行号）。

### 2.7.5 define_find() —— 宏查找

```c
ST_INLN Sym *define_find(int v)
{
    Sym *s;
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    s = table_ident[v]->sym_define;
    return s;
}
```

`define_find()` 通过 `TokenSym` 的 `sym_define` 指针直接查找宏定义。这是一个 O(1) 操作——不需要遍历符号表。

### 2.7.6 Token 转换

`next()` 的最后一步是将预处理 token 转换为 C 语言 token：

- `TOK_PPNUM` -> `parse_number()` -> `TOK_CINT`/`TOK_CUINT`/`TOK_CFLOAT`/`TOK_CDOUBLE` 等
- `TOK_PPSTR` -> `parse_string()` -> `TOK_CCHAR`/`TOK_STR`/`TOK_LSTR` 等

这个转换只在 `PARSE_FLAG_TOK_NUM` 和 `PARSE_FLAG_TOK_STR` 标志启用时发生。在纯预处理模式下（`tcc -E`），token 保持为 `TOK_PPNUM` 和 `TOK_PPSTR`。

---

## 2.8 标识符哈希与查找

### 2.8.1 哈希函数

tcc 使用的哈希函数定义如下：

```c
#define TOK_HASH_INIT 1
#define TOK_HASH_FUNC(h, c) ((h) + ((h) << 5) + ((h) >> 27) + (c))
```

这是一个**乘法哈希**的变体。`h + (h << 5) + (h >> 27)` 等价于 `h * 33 + (h >> 27)`，其中 `h >> 27` 提供了额外的混合。乘以 33 是一个经典的字符串哈希技巧（与 DJB2 哈希函数类似）。

> **为什么选择 33？** 这个乘数在实践中表现良好，它是一个奇数且不是 2 的幂，能够有效地将输入的模式扩散到哈希值的各个位。Paul Larson 在 1988 年的研究中证明了乘以小奇数的哈希函数在字符串哈希中具有良好的分布特性。

### 2.8.2 哈希表结构

```c
#define TOK_HASH_SIZE 16384  /* 必须是 2 的幂 */
static TokenSym *hash_ident[TOK_HASH_SIZE];
```

哈希表使用链地址法解决冲突。每个桶是一个 `TokenSym` 链表，通过 `hash_next` 指针连接。

```
hash_ident[0]  -> TokenSym -> TokenSym -> NULL
hash_ident[1]  -> NULL
hash_ident[2]  -> TokenSym -> NULL
...
hash_ident[16383] -> TokenSym -> TokenSym -> TokenSym -> NULL
```

### 2.8.3 tok_alloc() —— 标识符查找与注册

```c
ST_FUNC TokenSym *tok_alloc(const char *str, int len)
{
    TokenSym *ts, **pts;
    int i;
    unsigned int h;

    h = TOK_HASH_INIT;
    for(i = 0; i < len; i++)
        h = TOK_HASH_FUNC(h, ((unsigned char *)str)[i]);
    h &= (TOK_HASH_SIZE - 1);

    pts = &hash_ident[h];
    for(;;) {
        ts = *pts;
        if (!ts)
            break;
        if (ts->len == len && !memcmp(ts->str, str, len))
            return ts;
        pts = &(ts->hash_next);
    }
    return tok_alloc_new(pts, str, len);
}
```

**工作流程：**

1. 计算字符串的哈希值
2. 在哈希桶中线性搜索
3. 如果找到匹配的 `TokenSym`（长度和内容都相同），返回它
4. 如果没找到，调用 `tok_alloc_new()` 创建新的条目

### 2.8.4 tok_alloc_new() —— 创建新条目

```c
static TokenSym *tok_alloc_new(TokenSym **pts, const char *str, int len)
{
    TokenSym *ts;
    ts = tal_realloc(&toksym_alloc, NULL,
                     sizeof(TokenSym) + len);
    ts->tok = tok_ident++;       /* 分配新的 token 编号 */
    *table_ident_ptr(ts) = ts;   /* 添加到 table_ident 数组 */
    ts->sym_define = NULL;
    ts->sym_label = NULL;
    ts->sym_struct = NULL;
    ts->sym_identifier = NULL;
    ts->len = len;
    ts->hash_next = NULL;
    memcpy(ts->str, str, len);
    ts->str[len] = '\0';
    *pts = ts;
    return ts;
}
```

新条目的 token 编号从 `TOK_IDENT`（256）开始递增分配。使用 `tal_realloc` 进行内存分配（来自 tcc 的自定义分配器），分配大小为 `sizeof(TokenSym) + len`。

### 2.8.5 table_ident 数组

```c
ST_DATA TokenSym **table_ident;
```

`table_ident` 是一个动态数组，将 token 编号映射到 `TokenSym` 指针。给定 token 编号 `tok`（>= `TOK_IDENT`），对应的 `TokenSym` 为 `table_ident[tok - TOK_IDENT]`。

---

## 2.9 数字和字符串解析

### 2.9.1 parse_number() —— 数字解析

`parse_number()` 将 `TOK_PPNUM` 的原始文本解析为具体的数字 token。它处理以下情况：

**（1）进制判断**

```c
b = 10;
if (t == '0') {
    if (ch == 'x' || ch == 'X') {
        b = 16;      /* 十六进制: 0x... */
    } else if (tcc_state->tcc_ext && (ch == 'b' || ch == 'B')) {
        b = 2;       /* 二进制 (GCC 扩展): 0b... */
    }
}
```

如果没有 `0x` 或 `0b` 前缀，且数字以 `0` 开头，则在后续的整数解析阶段将基数从 10 改为 8（八进制）。

**（2）浮点数检测**

```c
if (ch == '.' ||
    ((ch == 'e' || ch == 'E') && b == 10) ||
    ((ch == 'p' || ch == 'P') && (b == 16 || b == 2))) {
    /* 浮点数解析 */
}
```

浮点数的判定条件：
- 包含小数点 `.`
- 十进制数包含 `e`/`E` 指数
- 十六进制或二进制数包含 `p`/`P` 指数

**（3）十六进制和二进制浮点数**

tcc 手动实现十六进制和二进制浮点数的解析，使用 128 位大数运算（`BN_SIZE = 4`）来避免精度损失：

```c
frac_bits = 0;
bn_zero(bn);
q = token_buf;
while (1) {
    t = *q++;
    /* ... 将字符转换为数值 ... */
    frac_bits -= bn_lshift(bn, shift, t);
}
/* 计算浮点值 */
d = (long double)bn[3] * 79228162514264337593543950336.0L +
    (long double)bn[2] * 18446744073709551616.0L +
    (long double)bn[1] * 4294967296.0L +
    (long double)bn[0];
d = ldexpl(d, exp_val - frac_bits);
```

**（4）类型后缀解析**

```c
/* 浮点后缀 */
if (t == 'F')      tok = TOK_CFLOAT;    /* float */
else if (t == 'L') tok = TOK_CLDOUBLE;  /* long double */
else               tok = TOK_CDOUBLE;   /* double (默认) */

/* 整数后缀 */
/* 解析 l/ll/u 的组合 */
lcount = ucount = 0;
for(;;) {
    t = toup(ch);
    if (t == 'L')      lcount++;
    else if (t == 'U') ucount++;
    else break;
    ch = *p++;
}

/* 根据后缀确定类型 */
tok = TOK_CINT;
if (lcount) {
    tok = TOK_CLONG;
    if (lcount == 2) tok = TOK_CLLONG;
}
if (ucount) ++tok;  /* TOK_CINT->TOK_CUINT, TOK_CLONG->TOK_CULONG, ... */
```

> **巧妙的 `++tok` 设计**：tcc 将有符号和无符号类型的 token 编号安排为连续的奇偶对：
> - `TOK_CINT` (0xC2) / `TOK_CUINT` (0xC3)
> - `TOK_CLLONG` (0xC4) / `TOK_CULLONG` (0xC5)
> - `TOK_CLONG` (0xC6) / `TOK_CULONG` (0xC7)
>
> 因此 `++tok` 就能将有符号类型转换为对应的无符号类型。

### 2.9.2 parse_string() —— 字符串解析

`parse_string()` 将 `TOK_PPSTR` 的原始文本解析为 `TOK_CCHAR`、`TOK_STR` 或 `TOK_LSTR`：

```c
static void parse_string(const char *s, int len)
{
    uint8_t buf[1000], *p = buf;
    int is_long, sep;

    if ((is_long = *s == 'L'))
        ++s, --len;
    sep = *s++;                  /* 引号字符 (' 或 ") */
    len -= 2;                    /* 去掉两端的引号 */
    /* ... */
    parse_escape_string(&tokcstr, p, is_long);

    if (sep == '\'') {
        tok = is_long ? TOK_LCHAR : TOK_CCHAR;
        /* 将字符值存入 tokc.i */
    } else {
        tok = is_long ? TOK_LSTR : TOK_STR;
        /* 将字符串数据存入 tokc.str */
    }
}
```

### 2.9.3 parse_escape_string() —— 转义序列解析

`parse_escape_string()` 负责解析字符串中的转义序列：

| 转义序列 | 解析方式 | 结果 |
|----------|---------|------|
| `\0` - `\7` | 最多 3 位八进制数 | 字符值 |
| `\xHH` | 十六进制数（任意位数） | 字符值 |
| `\uHHHH` | 4 位十六进制 Unicode | UTF-8 编码 |
| `\UHHHHHHHH` | 8 位十六进制 Unicode | UTF-8 编码 |
| `\a` | 固定值 0x07 | 响铃 |
| `\b` | 固定值 0x08 | 退格 |
| `\f` | 固定值 0x0C | 换页 |
| `\n` | 固定值 0x0A | 换行 |
| `\r` | 固定值 0x0D | 回车 |
| `\t` | 固定值 0x09 | 制表符 |
| `\v` | 固定值 0x0B | 垂直制表 |
| `\e` | 固定值 27（仅 GCC 扩展） | ESC |
| `\\`, `\'`, `\"`, `\?` | 保持原样 | 对应字符 |

Unicode 转义（`\u` 和 `\U`）的处理特别值得注意。对于非宽字符串，Unicode 码点被转换为 UTF-8 编码存入字符串；对于宽字符串，直接存储码点值。

```c
case 'x': i = 0; goto parse_hex_or_ucn;
case 'u': i = 4; goto parse_hex_or_ucn;
case 'U': i = 8; goto parse_hex_or_ucn;
parse_hex_or_ucn:
    p++;
    n = 0;
    do {
        c = *p;
        /* ... 将十六进制字符转换为数值 ... */
        n = (unsigned) n * 16 + c;
        p++;
    } while (--i);
    if (is_long) {
        c = n;
        goto add_char_nonext;     /* 宽字符串：直接存储码点 */
    }
    cstr_u8cat(outstr, n);        /* 普通字符串：UTF-8 编码 */
    continue;
```

---

## 2.10 性能优化技巧

tcc 的词法分析器虽然代码量不大，但包含了大量精心设计的性能优化。

### 2.10.1 缓冲区内的快速路径

最内层的循环（如标识符扫描的 `while` 循环）直接通过指针遍历缓冲区，不需要在每次迭代时检查缓冲区边界：

```c
while (c = *++p, isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
    h = TOK_HASH_FUNC(h, c);
```

这得益于缓冲区末尾的 `CH_EOB` 哨兵。如果标识符跨越缓冲区边界，哨兵字符 `\\` 不满足 `IS_ID|IS_NUM` 条件，循环自然终止。随后的 `handle_stray()` 调用会检测到这是缓冲区边界而非真正的 `\`，触发缓冲区重填。

### 2.10.2 哈希计算与扫描同步

在大多数编译器实现中，标识符的扫描和哈希计算是两个独立的步骤。tcc 将它们合并为一步：

```c
p1 = p;
h = TOK_HASH_INIT;
h = TOK_HASH_FUNC(h, c);
while (c = *++p, isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
    h = TOK_HASH_FUNC(h, c);
/* 此时 h 已经计算完成，可以直接用于查找 */
```

这避免了对标识符字符串的第二次遍历，对于长标识符（如 `__builtin_types_compatible_p`）效果尤为明显。

### 2.10.3 isidnum_table 查找优化

使用位标志的查找表代替条件判断：

```c
/* 条件判断方式（慢） */
if (isalpha(c) || isdigit(c) || c == '_')

/* 表查找方式（快） */
if (isidnum_table[c - CH_EOF] & (IS_ID|IS_NUM))
```

表查找方式只需要一次内存访问和一次位与操作，而条件判断方式需要多次函数调用和逻辑或操作。

### 2.10.4 直接使用 ASCII 值作为 Token 编号

对于单字符 token，直接使用 ASCII 值作为 token 编号，避免了额外的查找或映射：

```c
tok = c;  /* 直接使用字符值 */
```

这意味着在解析器中匹配这些 token 也不需要额外的开销：

```c
if (tok == '(')  /* 直接比较，不需要查表 */
```

### 2.10.5 TokenSym 的直接指针访问

`TokenSym` 中的四个 `Sym*` 指针（`sym_define`、`sym_label`、`sym_struct`、`sym_identifier`）使得语义查找成为 O(1) 操作。大多数编译器需要在符号表中进行哈希查找来获取这些信息。

### 2.10.6 自定义内存分配器

tcc 使用 `TinyAlloc` 分配器来管理 `TokenSym` 和 `TokenString` 的内存。`TinyAlloc` 是一个简单的块分配器：

```c
#define TOKSYM_TAL_SIZE (256 * 1024)  /* 256KB 块 */
#define TOKSTR_TAL_SIZE (256 * 1024)
```

它一次性分配大块内存，然后在块内线性分配小对象。这避免了 `malloc`/`free` 的系统调用开销和内存碎片。

### 2.10.7 PEEKC 宏的延迟检查

`PEEKC` 宏只在遇到 `\` 时才调用 `handle_stray()`：

```c
#define PEEKC(c, p)                    \
{                                      \
    c = *++p;                          \
    if (c == '\\')                     \
        c = handle_stray(&p);          \
}
```

在大多数情况下（没有续行），这只是一次指针递增和一次条件分支（不跳转），开销极小。

---

## 2.11 本章小结与练习

### 本章小结

本章详细分析了 tcc 词法分析器的实现。以下是关键要点：

1. **Token 编码方案**：tcc 使用精心设计的编号空间，将 0x00-0x7F 分配给单字符 token，0x80-0xCF 分配给内部操作符和常量，>=256 分配给标识符和关键字。这种设计使得大部分 token 的处理可以简化为整数比较。

2. **缓冲区 I/O**：使用固定大小的缓冲区（8192 字节）和 `CH_EOB` 哨兵字符，使得内层循环不需要边界检查。`BufferedFile` 结构体整合了缓冲区和文件元信息。

3. **两阶段扫描**：`next_nomacro()` 负责底层扫描，`next()` 在此基础上添加宏展开。预处理阶段数字和字符串以原始形式（`TOK_PPNUM`/`TOK_PPSTR`）保存，在需要时才解析。

4. **哈希标识符查找**：在扫描标识符的同时计算哈希值，避免了额外的遍历。使用 16384 桶的哈希表和链地址法。

5. **性能优化**：包括缓冲区内快速路径、哈希与扫描同步、位标志字符分类表、直接 ASCII 值映射、直接指针语义查找、自定义内存分配器等多层次优化。

6. **X-macro 模式**：`tcctok.h` 通过不同的 `DEF` 宏定义，一次编写多次使用，生成字符串表、枚举常量和符号注册代码。

### 练习

**练习 2.1**：给定一段 C 代码，识别其中的所有 token 及其编号。参见 `exercises/ex1_tokens.md`。

**练习 2.2**：手算 `TOK_HASH_FUNC` 对字符串 `"main"` 的哈希值。参见 `exercises/ex2_hash.md`。

**练习 2.3**：修改 tcc 源码，添加一个新的关键字。参见 `exercises/ex3_modify.md`。

### 延伸阅读

1. Aho, A. V., Lam, M. S., Sethi, R., & Ullman, J. D. (2006). *Compilers: Principles, Techniques, and Tools* (2nd ed.). Chapter 3: Lexical Analysis.
2. Bellard, F. (2002). "TCC: The Smallest ANSI C Compiler." *Dr. Dobb's Journal*.
3. Kernighan, B. W., & Ritchie, D. M. (1988). *The C Programming Language* (2nd ed.). Appendix A: C Reference Manual.
4. Larson, P. (1988). "Dynamic Hash Tables." *Communications of the ACM*, 31(4), 446-457.
