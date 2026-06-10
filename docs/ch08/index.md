# 第八章 跨平台与汇编器

TinyCC 的一大设计特点是其高度模块化的后端架构。通过在编译时选择不同的 `TCC_TARGET_*` 宏，同一套前端代码可以生成针对不同 CPU 架构和操作系统的代码。本章将深入分析 TCC 的后端抽象层、各平台的代码生成器、目标 OS 支持、交叉编译以及内置汇编器。

---

## 8.1 后端接口抽象

TCC 的代码生成器通过一组约定好的函数接口与平台无关的前端（`tccgen.c`）交互。每个目标架构必须实现这些函数，前端在适当的时候调用它们。

### 8.1.1 核心代码生成接口

以下是后端必须实现的主要函数（定义在 `tccgen.c` 中，由各 `<target>-gen.c` 实现）：

**基本数据操作：**

| 函数 | 功能 |
|------|------|
| `load(int r, SValue *sv)` | 将一个 SValue 加载到寄存器 r 中 |
| `store(int r, SValue *sv)` | 将寄存器 r 的值存储到 SValue 指定的位置 |
| `gfunc_start(CType *func_type)` | 开始函数调用参数传递 |
| `gfunc_param(SValue *v)` | 传递一个函数参数 |
| `gfunc_call(int nb_args)` | 发射函数调用指令 |
| `gfunc_prolog(CType *func_type)` | 生成函数序言（保存寄存器、分配栈帧） |
| `gfunc_epilog(void)` | 生成函数尾声（恢复寄存器、释放栈帧） |

**算术与逻辑操作：**

| 函数 | 功能 |
|------|------|
| `gen_opi(int op)` | 对两个整数值执行二元操作 |
| `gen_opf(int op)` | 对两个浮点值执行二元操作 |
| `gen_cvt_itof(int t)` | 整数转浮点 |
| `gen_cvt_ftoi(int t)` | 浮点转整数 |
| `gen_cvt_ftof(int t)` | 浮点格式转换 |

**跳转与分支：**

| 函数 | 功能 |
|------|------|
| `gjmp(int t)` | 生成无条件跳转，返回跳转目标 token |
| `gjmp_true(int t, int c)` | 条件为真时跳转 |
| `gjmp_false(int t, int c)` | 条件为假时跳转 |
| `gtst(int inv, int t)` | 测试条件并生成条件跳转 |

**值操作辅助：**

| 函数 | 功能 |
|------|------|
| `gv(int rc)` | 将栈顶值"具体化"到寄存器中（register class rc） |
| `gv2(int r1, int r2)` | 将栈顶两个值加载到指定寄存器 |
| `vpush(int v)` | 将值压入虚拟栈 |
| `vpop(void)` | 弹出栈顶值 |
| `vrotb(int n)` | 旋转栈顶 n 个元素 |
| `vset(CType *type, int r, int v)` | 设置栈顶值 |
| `save_reg(int r)` | 保存寄存器到栈（如果它被占用） |
| `get_reg(int rc)` | 分配一个空闲寄存器 |

### 8.1.2 TARGET_DEFS_ONLY 模式

每个后端文件（如 `x86_64-gen.c`）都使用 `#ifdef TARGET_DEFS_ONLY` 条件编译。当以定义了 `TARGET_DEFS_ONLY` 的方式包含时，只暴露寄存器定义和常量；当正常编译时，暴露完整的代码生成实现。

```c
/* x86_64-gen.c 的结构 */
#ifdef TARGET_DEFS_ONLY

#define NB_REGS        25
#define RC_INT         0x0001
#define RC_FLOAT       0x0002
#define PTR_SIZE       8
/* ... 寄存器枚举和宏定义 ... */

#else /* !TARGET_DEFS_ONLY */

#include "tcc.h"
/* ... 完整的代码生成实现 ... */

#endif
```

这个设计使得 `tcc.h` 可以通过包含所有后端文件（以 `TARGET_DEFS_ONLY` 模式）来获取所有目标架构的公共定义，而实际编译时只链接一个后端的完整实现。

### 8.1.3 链接器接口

每个后端还需要实现链接相关的函数（在 `<target>-link.c` 中）：

| 函数 | 功能 |
|------|------|
| `relocate_section(Section *s)` | 对一个段进行重定位 |
| `relocate_rel(Section *s)` | 处理 REL 类型的重定位表项 |
| `relocate(TCCState *s1)` | 执行全局重定位 |
| `build_got(TCCState *s1)` | 构建 GOT（全局偏移表） |
| `build_got_entries(TCCState *s1)` | 为需要的符号创建 GOT 条目 |
| `put_got_offset(int index, addr_t off)` | 写入 GOT 偏移 |
| `sym_plt_func(TCCState *s1, int flags)` | 处理 PLT 条目 |
| `create_plt_entry(TCCState *s1, unsigned reloc_type, ...)` | 创建 PLT 条目 |

### 8.1.4 汇编器接口

每个支持内联汇编的后端还需要在 `<target>-asm.c` 中实现汇编指令的解析和编码：

| 函数 | 功能 |
|------|------|
| `asm_parse_instr(void)` | 解析一条汇编指令 |
| `asm_compute_constraints(ASMOperand *operands, ...)` | 计算约束分配 |
| `subst_asm_operands(ASMOperand *operands, ...)` | 替换汇编中的操作数引用 |

---

## 8.2 x86_64 平台

x86-64 是 TCC 支持最完善的平台，也是默认的编译目标（在 64 位 Linux 上）。

### 8.2.1 寄存器分配

TCC 的 x86-64 后端使用 25 个寄存器槽位：

```c
/* x86_64-gen.c */
#define NB_REGS     25
#define NB_ASM_REGS 16

enum {
    TREG_RAX = 0,   /* 返回值寄存器 */
    TREG_RCX = 1,   /* 第4个参数（Windows）/ 临时 */
    TREG_RDX = 2,   /* 第3个参数（Windows）/ 临时 */
    TREG_RSP = 4,   /* 栈指针（不分配给值） */
    TREG_RSI = 6,   /* 第2个参数（System V） */
    TREG_RDI = 7,   /* 第1个参数（System V） */

    TREG_R8  = 8,
    TREG_R9  = 9,
    TREG_R10 = 10,
    TREG_R11 = 11,

    TREG_XMM0 = 16, /* 浮点返回值 */
    TREG_XMM1 = 17,
    /* ... XMM2-XMM7 ... */

    TREG_ST0 = 24,  /* x87 栈顶（long double） */

    TREG_MEM = 0x20 /* 内存位置标记 */
};
```

寄存器分类用于约束分配：

```c
#define RC_INT    0x0001    /* 通用整数寄存器 */
#define RC_FLOAT  0x0002    /* 浮点寄存器 */
#define RC_RAX    0x0004    /* 特定 RAX */
#define RC_RDX    0x0008    /* 特定 RDX */
#define RC_RCX    0x0010    /* 特定 RCX */
#define RC_XMM0   0x1000    /* 特定 XMM0 */
#define RC_IRET   RC_RAX    /* 整数返回寄存器 */
#define RC_FRET   RC_XMM0   /* 浮点返回寄存器 */
```

### 8.2.2 System V ABI 调用约定

在 Linux/macOS 上，x86-64 使用 System V AMD64 ABI：

**整数参数传递**：依次使用 `RDI`、`RSI`、`RDX`、`RCX`、`R8`、`R9`（共 6 个寄存器），超出部分通过栈传递。

**浮点参数传递**：依次使用 `XMM0`-`XMM7`（共 8 个寄存器）。

**返回值**：整数在 `RAX`（和 `RDX` 用于 128 位），浮点在 `XMM0`（和 `XMM1`）。

**调用者保存寄存器**：`RAX`、`RCX`、`RDX`、`RSI`、`RDI`、`R8`-`R11`（可被被调函数自由修改）。

**被调者保存寄存器**：`RBX`、`RBP`、`R12`-`R15`、`RSP`（被调函数必须保存和恢复）。

TCC 在 `gfunc_param` 中实现了这些规则：

```c
/* x86_64-gen.c - 参数传递逻辑（简化） */
static void gfunc_param(SValue *v)
{
    /* 整数参数 */
    if (is_integer_type(vtype)) {
        if (nb_reg_args < 6) {
            /* 使用寄存器传递 */
            reg = arg_regs[nb_reg_args++];
            load_reg(v, reg);
        } else {
            /* 通过栈传递 */
            vpush(v);
            gadd_sp(PTR_SIZE); /* 栈上分配空间 */
        }
    }
    /* 浮点参数 */
    else if (is_float_type(vtype)) {
        if (nb_xmm_args < 8) {
            reg = xmm_regs[nb_xmm_args++];
            load_reg(v, reg);
        } else {
            /* 通过栈传递 */
        }
    }
}
```

### 8.2.3 Windows x64 调用约定

Windows 使用不同的调用约定（Microsoft x64）：

**整数参数**：`RCX`、`RDX`、`R8`、`R9`（仅 4 个）。

**浮点参数**：`XMM0`-`XMM3`（仅 4 个）。

**影子空间**：调用者必须在栈上预留 32 字节的"影子空间"（shadow space）供被调者使用。

**栈对齐**：调用前栈必须 16 字节对齐。

TCC 通过预定义宏区分两种约定：

```c
#ifdef _WIN32
    /* Windows x64 ABI */
    static const int arg_regs[] = { TREG_RCX, TREG_RDX, TREG_R8, TREG_R9 };
#else
    /* System V ABI */
    static const int arg_regs[] = { TREG_RDI, TREG_RSI, TREG_RDX,
                                    TREG_RCX, TREG_R8, TREG_R9 };
#endif
```

### 8.2.4 指令编码

x86-64 的指令编码比其他 RISC 架构复杂得多。TCC 通过一组辅助函数来发射编码后的指令：

```c
/* x86_64-gen.c - 指令编码辅助 */
static void o(unsigned int c)
{
    /* 将一个或多个字节写入当前代码段 */
    int ind1 = ind + 4;
    unsigned char *p;
    if (nocode_wanted) return;
    p = section_ptr_add(cur_text_section, 4);
    write32le(p, c);
    ind = ind1;
}

static void gen_modrm(int mod, int reg, int rm, ...)
{
    /* 生成 ModR/M 字节 */
    /* mod (2 bits): 寻址模式
     * reg (3 bits): 寄存器编号
     * rm  (3 bits): 寄存器/内存操作数 */
    o(0xC0 | (mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

static void gen_rex(int width, int reg, int index, int base)
{
    /* 生成 REX 前缀 */
    /* REX.W (bit 3): 64 位操作数
     * REX.R (bit 2): 扩展 reg 字段
     * REX.X (bit 1): 扩展 SIB index
     * REX.B (bit 0): 扩展 rm/base */
    o(0x40 | (width << 3) | ((reg >> 1) & 4) |
      ((index >> 2) & 2) | ((base >> 3) & 1));
}
```

一个完整的 x86-64 指令编码示例——生成 `mov %eax, %ecx`：

```
REX 前缀:  不需要（32 位操作，无扩展寄存器）
操作码:    0x89 (MOV r/m32, r32)
ModR/M:    0xC1 (mod=11, reg=ECX(001), rm=EAX(000))
编码结果:  89 C1
```

---

## 8.3 ARM64 平台

ARM64（AArch64）是 TCC 支持的第二个主要 64 位平台。

### 8.3.1 寄存器分配

```c
/* arm64-gen.c */
#define NB_REGS 28  /* x0-x18, x30, v0-v7 */

#define TREG_R(x) (x)      /* 通用寄存器: x=0..18 */
#define TREG_R30  19        /* 链接寄存器 */
#define TREG_F(x) (x + 20) /* 浮点寄存器: v0-v7 */

#define RC_INT   (1 << 0)
#define RC_FLOAT (1 << 1)
#define RC_R(x)  (1 << (2 + (x)))  /* 特定整数寄存器 */
#define RC_F(x)  (1 << (22 + (x))) /* 特定浮点寄存器 */

#define REG_IRET (TREG_R(0))  /* x0: 整数返回 */
#define REG_FRET (TREG_F(0))  /* v0: 浮点返回 */
```

ARM64 的寄存器比 x86-64 更规整：31 个通用寄存器（x0-x30）和 32 个浮点/SIMD 寄存器（v0-v31），加上零寄存器 xzr 和栈指针 sp。

TCC 的寄存器分配使用了 x0-x18（19 个参数/临时寄存器）和 x30（链接寄存器），以及 v0-v7（8 个浮点寄存器），共 28 个可分配槽位。

### 8.3.2 AAPCS64 调用约定

ARM64 使用 AAPCS64（ARM Architecture Procedure Call Standard）：

**整数参数**：`x0`-`x7`（8 个寄存器），超出部分通过栈传递。

**浮点参数**：`v0`-`v7`（8 个寄存器）。

**返回值**：整数在 `x0`（和 `x1` 用于 128 位），浮点在 `v0`。

**帧指针**：`x29`（FP），链接寄存器：`x30`（LR）。

TCC 的函数序言生成：

```c
/* arm64-gen.c - gfunc_prolog 简化展示 */
static void gfunc_prolog(CType *func_type)
{
    /* 保存帧指针和链接寄存器 */
    /* stp x29, x30, [sp, #-framesize]! */
    o(0xa9000000 | ...);

    /* 设置帧指针 */
    /* mov x29, sp */
    o(0x910003fd);

    /* 保存被调者保存寄存器 */
    /* 为局部变量分配栈空间 */
    /* sub sp, sp, #locals_size */
}
```

### 8.3.3 指令编码

ARM64 使用固定 32 位指令编码，这比 x86-64 的变长编码简单得多。TCC 通过辅助函数编码指令：

```c
/* arm64-gen.c - 指令编码示例 */
/* ADD Xd, Xn, #imm12 */
static void emit_add_imm(int d, int n, int imm12)
{
    o(0x91000000 | (imm12 << 10) | (n << 5) | d);
}

/* MOV Xd, Xn (编码为 ORR Xd, XZR, Xn) */
static void emit_mov(int d, int n)
{
    o(0xaa0003e0 | (n << 16) | d);
}
```

由于 TCC 的 ARM64 汇编器在自举时可能还未就绪，`alloca.S` 等关键文件中的 ARM64 代码段使用原始机器码（`.int` 指令）而非汇编助记符。

---

## 8.4 ARM 32 位平台

### 8.4.1 寄存器分配

```c
/* arm-gen.c */
#ifdef TCC_ARM_VFP
#define NB_REGS 13
#else
#define NB_REGS 9
#endif

#define RC_INT   0x0001
#define RC_FLOAT 0x0002
#define RC_R0    0x0004
#define RC_R1    0x0008
#define RC_R2    0x0010
#define RC_R3    0x0020
#define RC_R12   0x0040  /* IP: 临时寄存器 */
#define RC_F0    0x0080
/* ... F1-F7（VFP 模式下）... */
```

ARM 32 位有 16 个通用寄存器（r0-r15），其中：
- `r0`-`r3`：参数传递和返回值
- `r4`-`r11`：被调者保存寄存器
- `r12`（IP）：过程间临时寄存器
- `r13`（SP）：栈指针
- `r14`（LR）：链接寄存器
- `r15`（PC）：程序计数器

### 8.4.2 ARM vs Thumb 模式

ARM 处理器支持两种指令集：
- **ARM 模式**：32 位固定宽度指令，功能完整
- **Thumb 模式**：16 位（Thumb）或 16/32 位混合（Thumb-2）指令，代码密度更高

TCC 默认生成 ARM 指令。Thumb 模式的支持取决于 `CONFIG_TCC_CPUVER` 的设置。

### 8.4.3 EABI（嵌入式 ABI）

ARM EABI 是嵌入式系统中广泛使用的 ABI 标准：

**参数传递**：`r0`-`r3` 传递前 4 个参数，超出部分通过栈传递。

**返回值**：`r0`（和 `r1` 用于 64 位）。

**浮点**：在 VFP 模式下，浮点参数使用 `s0`-`s15`（单精度）或 `d0`-`d7`（双精度）。

TCC 通过 `TCC_ARM_EABI`、`TCC_ARM_VFP`、`TCC_ARM_HARDFLOAT` 等宏来控制 ABI 选择：

```c
#ifdef TCC_ARM_HARDFLOAT
    /* 硬浮点: 浮点参数通过 VFP 寄存器传递 */
    s->float_abi = ARM_HARD_FLOAT;
#else
    /* 软浮点: 浮点参数通过整数寄存器传递 */
#endif
```

ARM 平台还定义了额外的运行时辅助函数（在 libtcc1 中），例如 `__aeabi_memcpy` 系列：

```c
/* lib/bcheck.c - ARM EABI 内存操作包装 */
void *__bound___aeabi_memcpy(void *dst, const void *src, size_t size);
void *__bound___aeabi_memmove(void *dst, const void *src, size_t size);
void *__bound___aeabi_memset(void *dst, int c, size_t size);
```

---

## 8.5 RISC-V 64

RISC-V 是 TCC 最新添加的目标架构之一。

### 8.5.1 寄存器分配

```c
/* riscv64-gen.c */
#define NB_REGS 19  /* a0-a7, fa0-fa7, xxx, ra, sp */

#define TREG_R(x) (x)      /* 整数寄存器: x=0..7 (a0-a7) */
#define TREG_F(x) (x + 8)  /* 浮点寄存器: x=0..7 (fa0-fa7) */

#define TREG_RA 17  /* 返回地址 (x1) */
#define TREG_SP 18  /* 栈指针 (x2) */

#define PTR_SIZE 8
#define CHAR_IS_UNSIGNED
```

RISC-V 使用 LP64D ABI：

**整数参数**：`a0`-`a7`（即 `x10`-`x17`，8 个寄存器）。

**浮点参数**：`fa0`-`fa7`（即 `f10`-`f17`，8 个寄存器）。

**返回值**：整数在 `a0`（和 `a1` 用于 128 位），浮点在 `fa0`。

TCC 在 RISC-V 后端还定义了平台特定的预定义宏：

```c
/* riscv64-gen.c */
ST_DATA const char *const target_machine_defs =
    "__riscv\0"
    "__riscv_xlen 64\0"
    "__riscv_flen 64\0"
    "__riscv_div\0"
    "__riscv_mul\0"
    "__riscv_fdiv\0"
    "__riscv_fsqrt\0"
    "__riscv_float_abi_double\0"
    ;
```

### 8.5.2 指令编码

RISC-V 使用固定的 32 位指令编码（基本指令集），具有清晰的格式分类：

| 格式 | 用途 | 字段 |
|------|------|------|
| R-type | 寄存器-寄存器运算 | funct7, rs2, rs1, funct3, rd, opcode |
| I-type | 立即数运算/加载 | imm[11:0], rs1, funct3, rd, opcode |
| S-type | 存储 | imm[11:5], rs2, rs1, funct3, imm[4:0], opcode |
| B-type | 条件分支 | 类似 S-type，12 位偏移 |
| U-type | 长立即数 | imm[31:12], rd, opcode |
| J-type | 跳转 (JAL) | 20 位偏移, rd, opcode |

TCC 的 RISC-V 后端使用辅助函数来编码各类指令：

```c
/* riscv64-gen.c */
static void emit_R(int opcode, int rd, int funct3, int rs1, int rs2, int funct7)
{
    o(funct7 << 25 | rs2 << 20 | rs1 << 15 | funct3 << 12 | rd << 7 | opcode);
}
```

### 8.5.3 PC 相对寻址

RISC-V 使用 PC 相对寻址来加载全局变量地址，这需要两条指令（`auipc` + `addi`）配合完成。TCC 在 `riscv64-link.c` 中使用 `pcrel_hi_entries` 来跟踪高 20 位的重定位信息：

```c
/* tcc.h - RISC-V 特有的状态字段 */
#ifdef TCC_TARGET_RISCV64
    struct pcrel_hi { addr_t addr, val; } **pcrel_hi_entries;
    int nb_pcrel_hi_entries;
#endif
```

---

## 8.6 目标 OS 支持

TCC 不仅支持多种 CPU 架构，还支持多种操作系统。不同 OS 的主要区别在于可执行文件格式、动态链接机制和系统调用约定。

### 8.6.1 Linux ELF

ELF（Executable and Linkable Format）是 Linux 和大多数 Unix 系统的原生可执行格式。TCC 的 ELF 支持在 `tccelf.c` 和各平台的 `<target>-link.c` 中实现。

关键特性：
- 生成标准 ELF 可执行文件、共享库和目标文件
- 支持 GOT/PLT 用于位置无关代码（PIC）
- 支持 DWARF 和 STAB 调试信息
- 支持 `.eh_frame` 异常处理表
- ELF 解释器路径通过 `CONFIG_TCC_ELFINTERP` 配置：

```c
/* tcc.h */
#if defined(TCC_TARGET_X86_64)
# define CONFIG_TCC_ELFINTERP "/lib64/ld-linux-x86-64.so.2"
#elif defined(TCC_TARGET_ARM64)
# define CONFIG_TCC_ELFINTERP "/lib/ld-linux-aarch64.so.1"
#elif defined(TCC_TARGET_RISCV64)
# define CONFIG_TCC_ELFINTERP "/lib/ld-linux-riscv64-lp64d.so.1"
#elif defined(TCC_TARGET_ARM)
# define CONFIG_TCC_ELFINTERP "/lib/ld-linux.so.3"
#else
# define CONFIG_TCC_ELFINTERP "/lib/ld-linux.so.2"
#endif
```

### 8.6.2 Windows PE

Windows 使用 PE（Portable Executable）格式。TCC 的 PE 支持在 `tccpe.c` 中实现，当定义了 `TCC_TARGET_PE` 时启用。

关键特性：
- 生成 PE 可执行文件和 DLL
- 支持 PE 导入表和导出表
- 支持 Windows SEH（结构化异常处理）
- 支持 PE 特有的链接选项：

```c
/* tcc.h - PE 特有状态 */
#ifdef TCC_TARGET_PE
    int pe_subsystem;               /* PE 子系统类型 */
    unsigned pe_characteristics;    /* PE 文件特征 */
    unsigned pe_dll_characteristics;/* DLL 特征 */
    unsigned pe_file_align;         /* 文件对齐 */
    unsigned pe_stack_size;         /* 栈大小 */
    addr_t pe_imagebase;            /* 映像基地址 */
#endif
```

当运行在 Windows 上时，TCC 使用 Windows 调用约定（参见 8.2.3 节），并且 `tcc_run()` 通过 `VirtualAlloc` 和 `VirtualProtect` 管理运行时内存。

### 8.6.3 macOS Mach-O

macOS 使用 Mach-O 格式。TCC 的 Mach-O 支持在 `tccmacho.c` 中实现（通过 `TCC_TARGET_MACHO` 启用），支持新的 Mach-O 代码（`CONFIG_NEW_MACHO`）。

关键特性：
- 生成 Mach-O 可执行文件和动态库（`.dylib`）
- 支持 TBD 文件（Text-Based Definition，Apple 的符号定义格式）
- 支持 macOS SDK 路径自动发现
- 支持 install_name：

```c
/* tcc.h - Mach-O 特有状态 */
#if defined TCC_TARGET_MACHO
    char *install_name;
    uint32_t compatibility_version;
    uint32_t current_version;
#endif
```

### 8.6.4 BSD 系统

TCC 支持多种 BSD 变体（FreeBSD、OpenBSD、NetBSD、DragonFly BSD）。这些系统都使用 ELF 格式，但有一些差异：

```c
/* tcc.h */
#if defined TARGETOS_OpenBSD || defined TARGETOS_FreeBSD \
    || defined TARGETOS_NetBSD || defined TARGETOS_FreeBSD_kernel
# define TARGETOS_BSD 1
#endif
```

OpenBSD 的特殊处理包括动态库版本选择：

```c
/* libtcc.c - OpenBSD 专用的 so 文件 glob */
#if defined TARGETOS_OpenBSD && !defined _WIN32
static int tcc_glob_so(TCCState *s1, const char *pattern, char *buf, int size)
{
    /* 选择最新版本的 libxxx.so.x.y */
    glob_t g;
    /* ... glob 匹配并选择最大版本号 ... */
}
#endif
```

---

## 8.7 构建交叉编译器

交叉编译器是指在一种平台上编译出另一种平台可执行代码的编译器。TCC 的构建系统使得创建交叉编译器非常简单。

### 8.7.1 基本配置选项

```bash
# 构建面向 ARM64 Linux 的交叉编译器（在 x86-64 主机上）
./configure --targetarm64 --enable-cross \
    --cross-prefix=aarch64-linux-gnu-

# 构建面向 RISC-V 64 的交叉编译器
./configure --targetriscv64 --enable-cross \
    --cross-prefix=riscv64-linux-gnu-

# 构建面向 i386（32 位 x86）的交叉编译器
./configure --targeti386 --enable-cross
```

`--enable-cross` 选项告诉构建系统：
1. 不编译 `tccrun.c` 中的本机运行代码（因为目标架构不是主机架构）
2. 使用 `CONFIG_TCC_CROSSPREFIX` 前缀来区分运行时文件
3. 不设置 `TCC_IS_NATIVE`，禁用 `-run` 功能

### 8.7.2 交叉编译的使用

```bash
# 使用 ARM64 交叉编译器
aarch64-tcc -o program program.c

# 使用交叉编译器生成目标文件
aarch64-tcc -c -o program.o program.c

# 指定目标库路径
aarch64-tcc -B/path/to/arm64/sysroot/usr/lib -o program program.c
```

### 8.7.3 多架构支持

TCC 可以在同一个构建中支持多个目标架构。通过 `TCC_TARGET_I386` 和 `TCC_TARGET_X86_64` 同时定义，可以构建支持 32/64 位切换的编译器（通过 `-m32`/`-m64` 选项）：

```c
/* tcc.c 中的多架构切换逻辑 */
#ifdef TCC_TARGET_I386
# ifdef TCC_TARGET_X86_64
    if (m32_flag)
        s->seg_size = 32;  /* 32 位模式 */
# endif
#endif
```

---

## 8.8 内置汇编器

TCC 内置了一个 GAS（GNU Assembler）兼容的汇编器，实现在 `tccasm.c` 中。它支持独立的 `.s`/`.S` 汇编文件以及 C 代码中的内联汇编。

### 8.8.1 汇编器架构

汇编器的核心流程：

```
源文件（.s 或 .S）
  → 预处理器（.S 文件需要预处理）
  → 词法分析器（tccpp.c）
  → 汇编指令解析器（tccasm.c）
  → 指令编码器（<target>-asm.c）
  → 目标代码生成（段数据）
```

### 8.8.2 支持的伪指令

TCC 的汇编器支持标准 GAS 伪指令：

| 伪指令 | 功能 |
|--------|------|
| `.text` / `.data` / `.bss` / `.rodata` | 段切换 |
| `.section name` | 指定自定义段 |
| `.global symbol` / `.globl symbol` | 声明全局符号 |
| `.type symbol, type` | 设置符号类型（function/object） |
| `.size symbol, expr` | 设置符号大小 |
| `.byte` / `.word` / `.long` / `.quad` | 数据定义 |
| `.string` / `.asciz` / `.ascii` | 字符串定义 |
| `.align expr` | 对齐 |
| `.skip size` | 跳过指定字节 |
| `.ident` | 版本标识（被忽略） |
| `.file` / `.loc` | 调试位置信息 |
| `.pushsection` / `.popsection` | 段栈操作 |
| `.previous` | 切换到上一个段 |
| `.incbin file` | 包含二进制文件 |

### 8.8.3 标签处理

汇编器需要处理三种标签：

1. **全局标签**：由 `.global` 声明，在链接时可见
2. **本地标签**：不带 `.global` 的标签，仅在当前编译单元可见
3. **数字标签**：如 `1:`、`2:`，通过 `1f`（forward）和 `1b`（backward）引用

```c
/* tccasm.c - 标签处理 */
static Sym *asm_new_label(TCCState *s1, int label, int is_local)
{
    Sym *sym;
    /* 对于全局标签，在全局符号表中查找或创建 */
    /* 对于本地标签，使用特殊的 L..N 前缀避免冲突 */
    sym = asm_label_push(label);
    /* ... */
    return sym;
}

ST_FUNC int asm_get_local_label_name(TCCState *s1, unsigned int n)
{
    /* 数字标签转为 L..N 形式 */
    char buf[64];
    snprintf(buf, sizeof(buf), "L..%u", n);
    return tok_alloc_const(buf);
}
```

### 8.8.4 表达式求值

汇编器中的地址表达式（如 `symbol + offset`）由 `asm_expr()` 处理。它支持：

- 算术运算：`+`、`-`、`*`、`/`
- 位运算：`&`、`|`、`^`、`~`
- 特殊符号：`.`（当前位置）
- 外部符号引用
- PC 相对表达式

```c
/* tccasm.c - 表达式值结构 */
typedef struct ExprValue {
    uint64_t v;   /* 常量值 */
    Sym *sym;     /* 关联的符号（如果有的话） */
    int pcrel;    /* 是否 PC 相对 */
} ExprValue;
```

---

## 8.9 内联汇编 asm()

TCC 支持 GCC 风格的内联汇编语法，允许在 C 函数中嵌入汇编代码：

```c
asm("汇编模板" : 输出操作数 : 输入操作数 : 破坏的寄存器);
```

### 8.9.1 语法支持

TCC 的内联汇编支持以下特性：

```c
/* 基本内联汇编 */
asm("nop");

/* 带操作数的内联汇编 */
int a = 10, b;
asm("mov %1, %0" : "=r"(b) : "r"(a));

/* 带约束的内联汇编 */
asm volatile(
    "lock add %1, %0"
    : "+m"(*addr)
    : "r"(value)
    : "memory"
);

/* 扩展 asm goto */
asm goto("test %0, %0\n\t"
         "jz %l[label]"
         : : "r"(val) : : label);
```

### 8.9.2 约束处理

约束字符串告诉编译器如何分配操作数。TCC 支持的约束包括：

| 约束 | 含义 |
|------|------|
| `r` | 通用寄存器 |
| `m` | 内存操作数 |
| `i` | 立即数 |
| `g` | 通用（寄存器/内存/立即数） |
| `a`/`b`/`c`/`d` | 特定寄存器（x86: EAX/EBX/ECX/EDX） |
| `S`/`D` | ESI/EDI（x86） |
| `f` | 浮点寄存器 |
| `0`-`9` | 匹配第 N 个操作数的约束 |
| `=` | 只写（输出） |
| `+` | 读写（输入输出） |
| `&` | 早期破坏（early clobber） |
| `~{memory}` | 破坏内存 |
| `~{cc}` | 破坏条件码 |

约束解析在 `asm_compute_constraints()` 中完成：

```c
/* tccasm.c 或 <target>-asm.c */
static void asm_compute_constraints(ASMOperand *operands,
    int nb_operands, int nb_outputs, int *pout_reg)
{
    /* 1. 分配优先级（输出约束优先于输入约束）
     * 2. 处理匹配约束（如 "0" 表示与第 0 个操作数使用同一寄存器）
     * 3. 为每个操作数分配寄存器或标记为内存操作数
     * 4. 处理早期破坏标记 */
}
```

### 8.9.3 操作数替换

`subst_asm_operands()` 将汇编模板中的 `%0`、`%1` 等引用替换为实际的寄存器名或内存引用：

```c
/* <target>-asm.c */
static void subst_asm_operands(ASMOperand *operands,
    int nb_operands, CString *out_str, CString *in_str)
{
    /* 解析汇编模板字符串 */
    /* 将 %N 替换为对应的操作数字符串 */
    /* %% 替换为 %（转义） */
    /* %cN 替换为常量值（不带 $ 前缀） */
    /* %pN 与 %N 类似但用于特定场景 */
}
```

例如，如果操作数约束是 `=r`(out) 和 `r`(in)，且 out 分配到 `%eax`，in 分配到 `%ecx`，则模板 `mov %1, %0` 会被替换为 `mov %ecx, %eax`。

### 8.9.4 各平台差异

不同架构的内联汇编有一些差异：

**x86/x86-64**：操作数从 0 开始编号，使用 `AT&T` 语法（源在前，目标在后）或 Intel 语法。

**ARM/ARM64**：操作数使用 `%0`、`%1` 引用，支持 ARM 特有的约束如 `l`（低寄存器）。

**RISC-V**：操作数使用 `%0`、`%1` 引用，约束相对简单。

---

## 8.10 本章小结与练习

### 小结

本章深入分析了 TinyCC 的跨平台架构：

1. **后端抽象层**：通过 `TARGET_DEFS_ONLY` 机制和一组约定好的函数接口，实现了前端与后端的干净分离。每个目标架构必须实现代码生成、链接和汇编三套接口。

2. **具体平台实现**：详细分析了 x86-64（System V ABI / Windows x64）、ARM64（AAPCS64）、ARM 32（EABI）和 RISC-V 64（LP64D）的寄存器分配、调用约定和指令编码。

3. **OS 支持**：TCC 支持 Linux（ELF）、Windows（PE）、macOS（Mach-O）和多种 BSD 系统，每种 OS 有自己的可执行格式和动态链接机制。

4. **交叉编译**：通过 `--enable-cross` 配置选项可以轻松构建面向任何支持架构的交叉编译器。

5. **内置汇编器**：TCC 内置了一个 GAS 兼容的汇编器，支持独立的汇编文件和 GCC 风格的内联汇编。

### 练习 1：交叉编译实践

使用 TCC 的交叉编译功能完成以下任务：

1. 在 x86-64 主机上构建一个面向 ARM64 的交叉编译器
2. 用交叉编译器编译一个简单的 C 程序
3. 用 `readelf` 或 `objdump` 检查生成的 ELF 文件，验证架构和指令集
4. 比较 TCC 和 GCC 交叉编译器生成代码的差异

### 练习 2：内联汇编实验

编写一个 C 程序，使用内联汇编实现以下功能：

1. **CPUID 查询**（x86-64）：使用 `cpuid` 指令获取 CPU 厂商字符串
2. **原子操作**：使用 `lock cmpxchg` 实现一个无锁自旋锁
3. **系统寄存器读取**（ARM64）：使用 `mrs` 指令读取 `CTR_EL0` 寄存器
4. **计时器读取**（RISC-V）：使用 `rdtime` 指令读取周期计数器

要求：为每个函数编写 `#if defined(__x86_64__)` / `#elif defined(__aarch64__)` 等条件编译，使其能在多个平台上编译。
