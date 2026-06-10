# 第五章 代码生成

## 概述

在前四章中，我们依次讨论了 TinyCC 的词法分析、语法分析与语义分析。本章进入编译器的最后核心阶段——**代码生成（Code Generation）**。与许多现代编译器不同，TinyCC 采用一种极为精简的策略：**不构建中间表示（IR），直接从语法树生成目标机器码**。这一设计决策使得 TinyCC 的代码生成器既短小又高效，但也意味着所有平台相关的优化必须在后端中完成。

本章将系统地剖析 TinyCC 代码生成器的架构。我们首先介绍其独特的"虚拟栈"机制（5.1–5.2），然后讨论后端接口的抽象层（5.3），接着深入分析几个关键的平台无关函数——`gv()`、`gen_op()`、`vstore()`、`gen_cast()`（5.4–5.7），以及条件跳转优化和寄存器分配策略（5.8–5.9）。随后，我们以 x86-64 后端为具体实例，展示平台相关代码如何实现这些接口（5.10）。最后讨论常量折叠（5.11）、代码抑制机制（5.12），并通过一个完整的端到端示例将所有概念串联起来（5.13）。

---

## 5.1 代码生成策略概述

### 5.1.1 直接代码生成

传统编译器通常采用三阶段架构：前端（解析）→ 中间表示（IR）→ 后端（目标代码生成）。GCC 使用 RTL（Register Transfer Language），LLVM 使用 SSA 形式的 IR。这些中间表示为各种优化 pass 提供了统一的操作平台。

TinyCC 的设计目标是**编译速度**而非代码质量，因此它选择了一条激进的捷径：

```
源代码 → 词法/语法分析 → 直接生成目标机器码
```

没有独立的 IR 阶段，没有优化 pass。语法分析器在归约（reduce）产生式的同时，就调用代码生成函数向目标代码段（`cur_text_section`）追加字节。

### 5.1.2 虚拟栈架构

虽然没有 IR，但 TinyCC 并非直接将每个表达式节点翻译为机器指令。它引入了一个精巧的中间层——**虚拟操作数栈（Virtual Value Stack，简称 vstack）**。其核心思想是：

- 每个表达式节点的求值结果被表示为一个 `SValue` 结构体，压入 vstack。
- 运算操作从栈顶弹出操作数，将结果压回栈顶。
- 只有当值真正需要进入物理寄存器时（例如作为函数参数、需要参与不支持的操作），才调用 `gv()` 将其"物化"到寄存器中。

这种设计允许 TinyCC 在表达式求值过程中保持一种**延迟求值**的姿态：常量可以在编译时折叠，寄存器分配被推迟到必要时刻，从而在不进行显式优化 pass 的情况下获得一定的代码质量。

### 5.1.3 与栈式虚拟机的比较

读者可能注意到 vstack 与栈式虚拟机（如 JVM、WebAssembly）的相似性。两者的关键区别在于：

| 特性 | 栈式虚拟机 | TinyCC vstack |
|------|-----------|---------------|
| 值的生命周期 | 运行时 | 编译时 |
| 栈的位置 | 运行时内存 | 编译器内部数组 |
| 目标 | 解释执行 | 生成原生机器码 |
| `SValue.r` 字段 | 无 | 记录值的物理位置 |

vstack 中的每个条目都可能代表一个**尚未物化**的值——一个常量、一个内存地址、甚至一个条件码状态。代码生成器的核心工作就是在正确时机将这些虚拟值转换为物理寄存器中的值。

---

## 5.2 虚拟栈（Value Stack）

### 5.2.1 SValue 结构体

vstack 的核心数据结构定义在 `tcc.h` 中：

```c
/* value on stack */
typedef struct SValue {
    CType type;          /* 类型信息 */
    unsigned short r;    /* 寄存器 + 标志位 */
    unsigned short r2;   /* 第二个寄存器（用于 long long） */
    union {
        struct { int jtrue, jfalse; }; /* 前向跳转链 */
        CValue c;         /* 常量值（当 r 为 VT_CONST 时） */
    };
    union {
        struct { unsigned short cmp_op, cmp_r; }; /* VT_CMP 操作 */
        struct Sym *sym;  /* 符号引用 */
    };
} SValue;
```

**字段详解：**

- **`type`**：CType 结构体，记录该值的 C 类型（基本类型、指针、结构体等）。类型信息对后续的类型转换、算术运算的宽度选择至关重要。

- **`r`**：这是 SValue 中最核心的字段。它编码了**值当前的物理位置**以及若干标志位。低 6 位（`VT_VALMASK = 0x003f`）编码位置，高位编码标志（`VT_LVAL`、`VT_SYM` 等）。

- **`r2`**：对于需要两个寄存器的类型（如 x86-32 上的 `long long`），`r2` 记录高 32 位所在的寄存器。未使用时设为 `VT_CONST`。

- **`c` / `jtrue, jfalse`**：联合体。当值为常量时，`c` 存储常量值（CValue 联合体，可以是 int、float、double、long double）。当值为条件跳转结果时，`jtrue` 和 `jfalse` 记录前向跳转链。

- **`sym` / `cmp_op, cmp_r`**：联合体。当值包含符号引用时（如全局变量地址），`sym` 指向符号表条目。当值存储在条件码中时（`VT_CMP`），`cmp_op` 记录比较操作符，`cmp_r` 记录浮点比较的特殊寄存器状态。

### 5.2.2 `r` 字段编码

`r` 字段的低 6 位（`VT_VALMASK`）编码值的当前位置。以下是所有可能的编码：

```
值          编码    含义
────────────────────────────────────────────────────
0-15        0x00-0x0f   物理寄存器编号（如 TREG_RAX=0, TREG_RCX=1, ...）
VT_CONST    0x0030      值为编译时常量，存储在 c 字段中
VT_LLOCAL   0x0031      左值，地址在栈帧的临时变量区域
VT_LOCAL    0x0032      值在栈帧中，偏移量存储在 c 字段中（相对于 %rbp）
VT_CMP      0x0033      值存储在 CPU 条件标志中（如 ZF, CF）
VT_JMP      0x0034      值是条件跳转"真"分支的结果（偶数）
VT_JMPI     0x0035      值是条件跳转"假"分支的结果（奇数）
```

**高位标志：**

```
标志          位      含义
────────────────────────────────────────────────────
VT_LVAL      0x0100   值是一个左值（内存地址），需要解引用才能使用
VT_SYM       0x0200   值包含符号引用，c 中的常量是相对于符号的偏移
VT_MUSTCAST  0x0C00   值需要延迟类型转换（char/short 存储在 int 寄存器中）
VT_NONCONST  0x1000   虽然当前为常量，但不是 C 标准的整数常量表达式
```

**编码示例：**

假设 `vtop->r = 0x0132`，则：
- 低 6 位 `0x32 = VT_LOCAL`：值在栈帧中
- 位 8（`0x0100`）= `VT_LVAL`：这是一个左值

这意味着该值是一个**栈上的局部变量的左值**——要使用它的值，需要先从栈中加载。

再如 `vtop->r = 0x0000`：
- 低 6 位 `0x00 = TREG_RAX`：值在 rax 寄存器中
- 无高位标志：这是一个右值

### 5.2.3 栈操作函数

vstack 通过全局指针 `vtop` 访问栈顶，底层是一个大小为 `VSTACK_SIZE`（512）的 `SValue` 数组 `_vstack[]`。以下是主要的栈操作函数：

**`vpushv(SValue *v)`**——将一个 SValue 压入栈顶：

```c
ST_FUNC void vpushv(SValue *v)
{
    if (vtop >= vstack + (VSTACK_SIZE - 1))
        tcc_error("memory full (vstack)");
    vtop++;
    *vtop = *v;
}
```

**`vpushi(int v)`**——压入整数常量：

```c
ST_FUNC void vpushi(int v)
{
    vpush64(VT_INT, v);
}
```

内部调用 `vpush64`，后者构造一个 `CValue` 并调用 `vsetc` 将类型设为 `VT_INT`，位置设为 `VT_CONST`。

**`vpop()`**——弹出栈顶值：

```c
ST_FUNC void vpop(void)
{
    int v;
    v = vtop->r & VT_VALMASK;
    if (v == TREG_ST0) {
        o(0xd8dd); /* fstp %st(0) — x87 浮点栈必须显式弹出 */
    } else if (v == VT_CMP) {
        /* 需要将悬空的跳转链解析到当前位置 */
        gsym(vtop->jtrue);
        gsym(vtop->jfalse);
    }
    vtop--;
}
```

注意 `vpop()` 的两个特殊处理：
1. x87 浮点寄存器栈（`TREG_ST0`）需要显式弹出指令。
2. `VT_CMP` 状态需要解析悬挂的跳转标签。

**`vswap()`**——交换栈顶两个元素：

```c
static void vswap(void)
{
    SValue tmp;
    vcheck_cmp();
    tmp = vtop[0];
    vtop[0] = vtop[-1];
    vtop[-1] = tmp;
}
```

调用 `vcheck_cmp()` 是因为如果栈顶下方有 `VT_CMP` 值，必须先将其物化到寄存器中，否则交换后条件码信息可能失效。

**`vrotb(int n)`**——将位置 n-1 的元素旋转到栈顶：

```c
ST_FUNC void vrotb(int n)
{
    SValue tmp;
    if (--n < 1) return;
    vcheck_cmp();
    tmp = vtop[-n];
    memmove(vtop - n, vtop - n + 1, sizeof *vtop * n);
    vtop[0] = tmp;
}
```

**`vrott(int n)`**——将栈顶元素旋转到位置 n-1：

```c
ST_FUNC void vrott(int n)
{
    SValue tmp;
    if (--n < 1) return;
    vcheck_cmp();
    tmp = vtop[0];
    memmove(vtop - n + 1, vtop - n, sizeof *vtop * n);
    vtop[-n] = tmp;
}
```

这两个旋转函数在函数调用参数准备时频繁使用——需要将函数地址旋转到参数之后。

**`vdup()`**——复制栈顶：

```c
static void vdup(void)
{
    vpushv(vtop);
}
```

---

## 5.3 后端接口

TinyCC 的代码生成器分为**平台无关层**（`tccgen.c`）和**平台相关层**（如 `x86_64-gen.c`、`arm64-gen.c`、`i386-gen.c`）。平台无关层通过一组函数指针/宏调用平台相关层的实现。

### 5.3.1 寄存器-内存传输

**`load(int r, SValue *sv)`**——将值 `sv` 加载到寄存器 `r` 中：

这是后端必须实现的核心函数。对于 x86-64，它根据 `sv` 的位置（常量、栈偏移、另一个寄存器）生成相应的 `mov` 指令。关键的分支逻辑：

- `VT_CONST + VT_SYM`：生成 RIP 相对寻址的 `mov` 或 GOT 间接访问
- `VT_CONST`（纯常量）：生成 `mov $imm, %r` 或 `movabs $imm64, %r`
- `VT_LOCAL`：生成 `lea offset(%rbp), %r`
- `VT_LVAL`（需要解引用）：生成 `mov (%base), %r`，使用 ModR/M 编码
- `VT_CMP`：生成 `setcc %al; movzbl %al, %r`
- `VT_JMP / VT_JMPI`：生成 `mov $1, %r; jmp ...; mov $0, %r`

**`store(int r, SValue *v)`**——将寄存器 `r` 中的值存储到 `v` 指定的左值位置：

与 `load()` 类似，根据目标位置生成相应的 `mov` 指令。对于浮点类型使用不同的指令（`movd`、`movq`、`fstpt`）。

### 5.3.2 函数调用约定

**`gfunc_call(int nb_args)`**——生成函数调用代码：

在调用前，函数地址和所有参数已按序压入 vstack。此函数负责：
1. 保存被调用者可能破坏的寄存器（`save_regs`）
2. 将参数移动到正确的寄存器或栈位置
3. 生成 `call` 指令
4. 清理 vstack（弹出参数和函数地址）
5. 将返回值寄存器信息压入 vstack

**`gfunc_prolog(Sym *func_sym)`**——生成函数序言（prologue）：

生成函数入口代码，包括：
1. 保存帧指针（`push %rbp; mov %rsp, %rbp`）
2. 分配局部变量空间（`sub $N, %rsp`）
3. 将传入的寄存器参数保存到栈帧中

**`gfunc_epilog(void)`**——生成函数尾声（epilogue）：

生成函数退出代码，包括：
1. 释放局部变量空间
2. 恢复帧指针（`leave`）
3. 返回（`ret`）

### 5.3.3 算术运算

**`gen_opi(int op)`**——生成整数二元运算：

接收 vstack 顶上的两个整数操作数，生成相应的算术/逻辑/移位指令。结果留在一个寄存器中，弹出一个操作数。

**`gen_opl(int op)`**——生成 long long 运算（在 x86-64 上等同于 `gen_opi`）。

**`gen_opf(int op)`**——生成浮点二元运算：

对于 SSE 浮点类型（float/double），使用 XMM 寄存器和 SSE 指令。对于 long double，使用 x87 FPU 栈。

### 5.3.4 控制流

**`gjmp(int t)`**——生成无条件跳转：

```c
int gjmp(int t)
{
    return gjmp2(0xe9, t);  /* jmp rel32 */
}
```

参数 `t` 是前向跳转链的头部。返回值是新的链头（新生成的跳转指令的待回填位置）。

**`gjmp_cond(int op, int t)`**——生成条件跳转：

根据比较操作符 `op`（如 `TOK_EQ`、`TOK_LT` 等）和条件码状态，生成 `jcc rel32` 指令。

**`gsym(int t)`**——解析前向跳转链：

将跳转链 `t` 中所有跳转指令的目标地址回填为当前位置。这是实现前向跳转的标准技术——跳转指令在生成时目标未知，先链在一起，待目标确定后统一回填。

**`gjmp_addr(int a)`**——生成跳转到固定地址：

尝试使用短跳转（`jmp rel8`，2 字节），如果偏移量超出范围则使用长跳转（`jmp rel32`，5 字节）。

---

## 5.4 `gv()` — 值到寄存器

`gv()` 是 TinyCC 代码生成器中**最关键**的函数。它的职责是：确保 vstack 栈顶的值被"物化"到一个指定类别的物理寄存器中，并返回该寄存器的编号。

### 5.4.1 函数签名与返回值

```c
ST_FUNC int gv(int rc)
```

参数 `rc` 是**寄存器类别**（register class），如 `RC_INT`（任意整数寄存器）、`RC_FLOAT`（任意 SSE 寄存器）、`RC_RAX`（必须是 rax）等。返回值是分配到的寄存器编号。

### 5.4.2 核心逻辑

`gv()` 的处理流程可以概括为以下步骤：

**第一步：处理位域（Bit Field）**

```c
if (vtop->type.t & VT_BITFIELD) {
    bit_pos = BIT_POS(vtop->type.t);
    bit_size = BIT_SIZE(vtop->type.t);
    vtop->type.t &= ~VT_STRUCT_MASK;  /* 移除位域信息避免循环 */
    /* ... 调用 adjust_bf() 或生成移位操作 ... */
    /* 递归调用 gv() 处理去位域化后的值 */
    r = gv(rc);
}
```

位域值需要特殊的处理：先从内存中加载完整的字，然后通过移位和掩码操作提取出位域的值。

**第二步：处理浮点常量**

```c
if (is_float(vtop->type.t) &&
    (vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
    /* CPU 通常不能直接使用浮点常量，需要存储到数据段 */
    offset = section_add(rodata_section, size, align);
    vpush_ref(&vtop->type, rodata_section, offset, size);
    vswap();
    init_putv(&p, &vtop->type, offset);
    vtop->r |= VT_LVAL;
}
```

整数常量可以直接编码到指令中（如 `mov $42, %eax`），但浮点常量不行。TinyCC 将浮点常量放入只读数据段（`.rodata`），然后通过内存引用访问。

**第三步：判断是否需要重新加载**

```c
r = vtop->r & VT_VALMASK;
r_ok = !(vtop->r & VT_LVAL) && (r < VT_CONST) && (reg_classes[r] & rc);
r2_ok = !rc2 || ((vtop->r2 < VT_CONST) && (reg_classes[vtop->r2] & rc2));

if (!r_ok || !r2_ok) {
    /* 需要加载/移动 */
}
```

`r_ok` 为真的条件是：值不是左值、已经在寄存器中（`r < VT_CONST`）、且寄存器属于要求的类别。如果任何一个条件不满足，就需要加载。

**第四步：分配寄存器并加载**

```c
if (!r_ok) {
    if (r < VT_CONST && (reg_classes[r] & rc) && !rc2)
        save_reg_upstack(r, 1);  /* 可以复用，先保存其他引用 */
    else
        r = get_reg(rc);         /* 分配新寄存器 */
}
load(r, vtop);  /* 调用后端 load() */
```

**第五步：更新 vstack**

```c
vtop->r = r;  /* 标记值现在在寄存器 r 中 */
```

### 5.4.3 双字类型处理

对于需要两个寄存器的类型（如 32 位平台上的 `long long`），`gv()` 需要额外处理：

```c
if (rc2) {
    /* 加载低字到 r */
    load(r, vtop);
    vtop->r = r;
    /* 分配第二个寄存器 */
    r2 = get_reg(rc2);
    /* 加载高字到 r2 */
    load(r2, vtop);
    vtop->r2 = r2;
}
```

在 x86-64 上，由于指针和 long 都是 64 位，大部分操作只需要一个寄存器。`r2` 主要用于 `__int128`（`VT_QLONG`）类型。

---

## 5.5 `gen_op()` — 二元操作

`gen_op()` 是处理所有二元运算的入口函数。它负责类型提升、指针算术特殊处理、常量折叠，以及最终调用后端的算术生成函数。

### 5.5.1 函数入口

```c
ST_FUNC void gen_op(int op)
{
    int t1, t2, bt1, bt2, t;
    CType type1, combtype;
    int op_class = op;

    if (op == TOK_SHR || op == TOK_SAR || op == TOK_SHL)
        op_class = SHIFT_OP;
    else if (TOK_ISCOND(op))
        op_class = CMP_OP;
```

操作符被分为三类：普通算术、移位、比较。移位操作的右操作数不需要与左操作数类型匹配；比较操作的结果总是 `int`。

### 5.5.2 函数指针到指针的转换

```c
    if (bt1 == VT_FUNC || bt2 == VT_FUNC) {
        /* 函数名退化为函数指针 */
        if (bt2 == VT_FUNC) {
            mk_pointer(&vtop->type);
            gaddrof();
        }
        /* ... */
        goto redo;
    }
```

### 5.5.3 类型组合

```c
    if (!combine_types(&combtype, vtop - 1, vtop, op_class)) {
        tcc_error("invalid operand types for binary operation");
    }
```

`combine_types()` 实现 C 语言的"usual arithmetic conversions"——找到两个操作数的公共类型。

### 5.5.4 指针算术

当至少一个操作数是指针时，需要特殊处理：

```c
    if (bt1 == VT_PTR || bt2 == VT_PTR) {
        if (op_class == CMP_OP)
            goto std_op;  /* 指针比较直接进行 */

        if (bt1 == VT_PTR && bt2 == VT_PTR) {
            /* 两个指针相减：结果是 ptrdiff_t */
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            vrott(3);
            gen_opic(op);         /* 计算差值 */
            vtop->type.t = VT_PTRDIFF_T;
            vswap();
            gen_op(TOK_PDIV);     /* 除以元素大小 */
        } else {
            /* 指针 ± 整数：乘以元素大小后相加 */
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            gen_op('*');           /* 偏移量 × sizeof(element) */
            gen_opic(op);         /* 指针 + 偏移量 */
            vtop->type = type1;   /* 恢复指针类型 */
        }
    }
```

### 5.5.5 标准算术路径

对于非指针操作：

```c
    t = t2 = combtype.t;
    /* 移位操作的右操作数保持为 int */
    if (op_class == SHIFT_OP)
        t2 = VT_INT;

    /* 无符号操作的特殊处理 */
    if (t & VT_UNSIGNED) {
        if (op == TOK_SAR) op = TOK_SHR;
        if (op == '/') op = TOK_UDIV;
        if (op == '%') op = TOK_UMOD;
        /* ... 比较操作也类似 ... */
    }

    /* 类型转换 */
    vswap();
    gen_cast_s(t);
    vswap();
    gen_cast_s(t2);

    /* 调用常量折叠或后端 */
    if (is_float(t))
        gen_opif(op);    /* 浮点常量折叠 + 后端 */
    else
        gen_opic(op);    /* 整数常量折叠 + 后端 */

    /* 设置结果类型 */
    if (op_class == CMP_OP)
        vtop->type.t = VT_INT;  /* 比较结果总是 int */
    else
        vtop->type.t = t;
```

---

## 5.6 `vstore()` — 存储操作

`vstore()` 将 vstack 栈顶的值存储到次栈顶指定的左值位置。它是赋值操作（`=`、`+=` 等复合赋值的最终步骤）的核心。

### 5.6.1 标量存储

对于基本类型（int、float、指针等），`vstore()` 的核心路径是：

```c
    gv(RC_TYPE(dbt));  /* 将值物化到寄存器 */
    store(r, vtop - 1); /* 调用后端 store() */
```

**延迟类型转换优化**：对于 `char` 和 `short` 类型的目标，`vstore()` 会尝试延迟类型转换：

```c
    if ((dbt == VT_BYTE || dbt == VT_SHORT) && is_integer_btype(sbt)) {
        delayed_cast = 1;
    }
```

这样可以避免先将 `int` 截断为 `char` 再存储（需要额外的 `movzbl` 指令），而是直接存储低字节，让 CPU 的字节/字存储指令自然完成截断。

### 5.6.2 结构体赋值

结构体赋值不能简单地用 `mov` 完成，需要逐字节复制：

```c
    if (sbt == VT_STRUCT) {
        size = type_size(&vtop->type, &align);
        /* 获取目标地址 */
        vpushv(vtop - 1);
        vtop->type.t = VT_PTR;
        gaddrof();
        /* 获取源地址 */
        vswap();
        vtop->type.t = VT_PTR;
        gaddrof();
        /* 生成 memcpy/memmove 调用 */
        vpushi(size);
        vpush_helper_func(TOK_memmove);
        vrott(4);
        gfunc_call(3);
    }
```

注意使用 `memmove` 而非 `memcpy`，因为源和目标可能重叠（如 `a = a`）。

在某些平台上，TinyCC 提供了优化的 `gen_struct_copy()` 路径，直接生成内联的 `rep movsb` 或逐字 `mov` 指令，避免函数调用开销。

### 5.6.3 位域存储

位域存储是最复杂的情况，需要**读-改-写**（read-modify-write）序列：

```c
    if (ft & VT_BITFIELD) {
        bit_pos = BIT_POS(ft);
        bit_size = BIT_SIZE(ft);

        /* 1. 保存左值作为表达式结果（支持链式赋值 s.b = s.a = n;） */
        vdup();
        vtop[-1] = vtop[-2];

        /* 2. 掩码源值 */
        vpushi((1ULL << bit_size) - 1);
        gen_op('&');

        /* 3. 移位到位域位置 */
        vpushi(bit_pos);
        gen_op(TOK_SHL);

        /* 4. 加载目标字 */
        vdup();
        vrott(3);

        /* 5. 清除目标位域区域 */
        vpushi(~((unsigned)mask << bit_pos));
        gen_op('&');

        /* 6. 合并 */
        gen_op('|');

        /* 7. 存储回内存 */
        vstore();
        vpop();
    }
```

这个序列生成的汇编大致如下（假设位域在 bit 4-7）：

```asm
    movl    (%rdi), %eax      # 加载原始字
    andl    $0xffffff0f, %eax  # 清除 bit 4-7
    shll    $4, %ecx           # 新值左移到 bit 4-7
    orl     %ecx, %eax         # 合并
    movl    %eax, (%rdi)       # 写回
```

---

## 5.7 `gen_cast()` — 类型转换

`gen_cast()` 实现 C 语言的类型转换语义。它处理从 vstack 栈顶值到目标类型的所有转换情况。

### 5.7.1 常量折叠

当源值是编译时常量时，转换在编译时完成，不生成任何代码：

```c
    c = (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    if (c) {
        /* 整数 → 浮点 */
        if (df) {
            if (sbt_bt == VT_LLONG)
                vtop->c.ld = vtop->c.i;
            else
                vtop->c.ld = (uint32_t)vtop->c.i;
            if (dbt == VT_FLOAT)
                vtop->c.f = (float)vtop->c.ld;
            else if (dbt == VT_DOUBLE)
                vtop->c.d = (double)vtop->c.ld;
        }
        /* 浮点 → 整数 */
        else if (sf) {
            if (dbt & VT_UNSIGNED)
                vtop->c.i = (uint64_t)vtop->c.ld;
            else
                vtop->c.i = (int64_t)vtop->c.ld;
        }
        /* 整数 → 整数：截断或符号扩展 */
        else {
            if (dbt_bt == VT_BYTE)
                vtop->c.i &= 0xff;
            else if (dbt_bt == VT_SHORT)
                vtop->c.i &= 0xffff;
            /* ... */
        }
        goto done;
    }
```

### 5.7.2 运行时转换

非常量值需要生成实际的转换指令：

**整数扩展（int → long long）**：

```c
    if (sbt_bt == VT_INT && dbt_bt == VT_LLONG) {
        gv(RC_INT);
        /* 生成 movslq（有符号扩展）或 movl（零扩展，实际上
           x86-64 的 movl 自动零扩展到 64 位） */
        gen_cvt_csti(dbt, vtop, 0);
    }
```

**浮点 ↔ 整数**：

```c
    if (sf && !df) {
        /* 浮点 → 整数 */
        gen_cvt_ftoi1(dbt);  /* 生成 cvttss2si / cvttsd2si */
    } else if (!sf && df) {
        /* 整数 → 浮点 */
        gen_cvt_itof1(dbt);  /* 生成 cvtsi2ss / cvtsi2sd */
    }
```

**float ↔ double**：

```c
    if (sf && df) {
        if (dbt == VT_DOUBLE)
            gen_cvt_ftof(VT_DOUBLE);  /* cvtss2sd */
        else
            gen_cvt_ftof(VT_FLOAT);   /* cvtss2sd → cvtsd2ss */
    }
```

### 5.7.3 VT_MUSTCAST 延迟转换

TinyCC 有一个重要的优化：当从内存加载 `char` 或 `short` 值时，CPU 的 `movsbl`/`movzbl` 指令已经完成了符号扩展/零扩展。但如果后续操作需要完整的 `int` 类型，可能还需要额外的转换。

为避免冗余转换，`gv()` 在加载时设置 `VT_MUSTCAST` 标志，表示"这个值需要在使用前进行延迟转换"。`gen_cast()` 在入口处检查此标志：

```c
    if (vtop->r & VT_MUSTCAST)
        force_charshort_cast();
```

---

## 5.8 条件跳转优化

### 5.8.1 `gvtst()` — 条件测试生成

`gvtst()` 是条件跳转的核心函数。它的职责是：根据 vstack 栈顶的值，生成跳转到指定标签的条件分支。

```c
static int gvtst(int inv, int t)
{
    int op, x, u;

    gvtst_set(inv, t);
    t = vtop->jtrue, u = vtop->jfalse;
    if (inv)
        x = u, u = t, t = x;
    op = vtop->cmp_op;

    /* 根据操作类型生成跳转 */
    if (op > 1)
        t = gjmp_cond(op ^ inv, t);  /* 条件跳转 */
    else if (op != inv)
        t = gjmp(t);                  /* 无条件跳转 */

    /* 解析互补跳转到当前位置 */
    gsym(u);

    vtop--;
    return t;
}
```

### 5.8.2 避免冗余比较

`gvtst()` 的关键优化是：如果栈顶值已经是 `VT_CMP` 状态（即 CPU 条件标志已经设置好），就**不需要再生成比较指令**。

例如，对于 `if (a > b)`，`gen_op(TOK_GT)` 已经生成了 `cmp` 指令并设置了条件码。`gvtst()` 直接使用这些条件码生成 `jg` 跳转，避免了 `cmp` + `test` 的冗余序列。

### 5.8.3 短路求值

对于逻辑与（`&&`）和逻辑或（`||`），TinyCC 使用**短路求值**：

**`a && b`** 的处理：

```
1. 求值 a
2. gvtst(false, end_label)  — 如果 a 为假，跳转到 end
3. 求值 b
4. gsym(end_label)          — 回填跳转目标
```

**`a || b`** 的处理：

```
1. 求值 a
2. gvtst(true, end_label)   — 如果 a 为真，跳转到 end
3. 求值 b
4. gsym(end_label)          — 回填跳转目标
```

这通过 `vtop->jtrue` 和 `vtop->jfalse` 两个前向跳转链实现。当一个操作数的值确定后，相应的跳转链被解析到正确的位置。

### 5.8.4 `vcheck_cmp()` — 条件码保护

当需要对栈顶下方有 `VT_CMP` 值的栈进行操作时（如 `vswap()`、`vrotb()`），必须先将 `VT_CMP` 物化：

```c
static void vcheck_cmp(void)
{
    /* 不能在有后续指令生成时保留 CPU 标志。
       也不能让 VT_JMP 出现在栈顶以外的位置。 */
    if (vtop->r == VT_CMP || vtop->r == VT_JMP || vtop->r == VT_JMPI) {
        if (!nocode_wanted)
            gv(RC_INT);  /* 物化到整数寄存器 */
    }
}
```

---

## 5.9 寄存器分配

### 5.9.1 `get_reg()` — 寄存器分配器

TinyCC 使用一个简单的**线性扫描**寄存器分配策略：

```c
ST_FUNC int get_reg(int rc)
{
    int r;
    SValue *p;

    /* 第一步：寻找空闲寄存器 */
    for (r = 0; r < NB_REGS; r++) {
        if (reg_classes[r] & rc) {
            if (nocode_wanted)
                return r;  /* 代码抑制模式下随便选一个 */
            for (p = vstack; p <= vtop; p++) {
                if ((p->r & VT_VALMASK) == r || p->r2 == r)
                    goto notfound;
            }
            return r;  /* 找到空闲寄存器 */
        }
    notfound: ;
    }

    /* 第二步：没有空闲寄存器，溢出一个 */
    for (p = vstack; p <= vtop; p++) {
        r = p->r2;
        if (r < VT_CONST && (reg_classes[r] & rc))
            goto save_found;
        r = p->r & VT_VALMASK;
        if (r < VT_CONST && (reg_classes[r] & rc)) {
        save_found:
            save_reg(r);  /* 溢出到栈上 */
            return r;
        }
    }
    return -1;  /* 不应到达这里 */
}
```

**分配策略总结：**

1. **复用（Reuse）**：如果值已经在正确类别的寄存器中（`gv()` 检查 `r_ok`），直接使用。
2. **空闲（Free）**：遍历所有寄存器，找到一个不在 vstack 中使用的寄存器。
3. **溢出（Spill）**：如果所有寄存器都被占用，从栈底开始找到第一个使用目标类别寄存器的 vstack 条目，将其溢出到栈上。

**从栈底开始溢出**是一个重要细节：`gen_opi()` 等函数在调用 `gv2()` 后，结果在 `vtop[-1]` 中，`vtop` 被弹出。从栈底溢出确保不会溢出刚刚分配的寄存器。

### 5.9.2 `save_reg()` / `save_reg_upstack()`

```c
ST_FUNC void save_reg_upstack(int r, int n)
{
    for (p = vstack, p1 = vtop - n; p <= p1; p++) {
        if ((p->r & VT_VALMASK) == r || p->r2 == r) {
            if (!l) {
                /* 第一次发现引用：分配临时栈空间并存储 */
                l = get_temp_local_var(size, align, &r2);
                store(r, &sv);
            }
            /* 标记该 vstack 条目已被溢出 */
            p->r = VT_LVAL | VT_LOCAL;
            p->c.i = l;
            p->r2 = r2;
        }
    }
}
```

`save_reg(r)` 溢出所有引用寄存器 `r` 的 vstack 条目。`save_reg_upstack(r, n)` 只溢出 `vtop - n` 以下的条目——这在 `gv()` 中使用，因为栈顶条目即将被覆盖。

### 5.9.3 `get_temp_local_var()`

溢出的寄存器需要临时存储空间。`get_temp_local_var()` 在当前函数的栈帧中分配临时变量：

```c
static int get_temp_local_var(int size, int align, int *r2)
{
    /* 首先尝试复用已不再使用的临时变量 */
    for (i = 0; i < nb_temp_local_vars; i++) {
        temp_var = &arr_temp_local_vars[i];
        if (!(used & (1 << i)) && temp_var->size >= size
            && temp_var->align >= align) {
            *r2 = (VT_CONST + 1) + i;
            return temp_var->location;
        }
    }
    /* 没有可复用的，分配新的 */
    loc = (loc - size) & -align;
    /* ... */
    return loc;
}
```

这避免了每次溢出都增长栈帧，通过复用机制减少了栈空间的浪费。

---

## 5.10 x86-64 后端详解

本节以 x86-64（SysV ABI，Linux）为例，详细展示后端接口的具体实现。源码位于 `x86_64-gen.c`。

### 5.10.1 寄存器集合

x86-64 后端定义了 25 个逻辑寄存器：

```c
#define NB_REGS  25

enum {
    TREG_RAX = 0,   TREG_RCX = 1,   TREG_RDX = 2,
    /* 3 = rbx (不使用) */
    TREG_RSP = 4,   /* 栈指针（不用于通用分配） */
    /* 5 = rbp (帧指针，不使用) */
    TREG_RSI = 6,   TREG_RDI = 7,
    TREG_R8  = 8,   TREG_R9  = 9,
    TREG_R10 = 10,  TREG_R11 = 11,
    /* 12-15 = r12-r15 (callee-saved，不使用) */
    TREG_XMM0 = 16, TREG_XMM1 = 17, TREG_XMM2 = 18, TREG_XMM3 = 19,
    TREG_XMM4 = 20, TREG_XMM5 = 21, TREG_XMM6 = 22, TREG_XMM7 = 23,
    TREG_ST0 = 24,  /* x87 栈顶（用于 long double） */
};
```

**寄存器分类：**

| 类别 | 寄存器 | 用途 |
|------|--------|------|
| `RC_INT` | rax, rcx, rdx, r8-r11 | 通用整数运算 |
| `RC_RAX` | rax | 乘法/除法的隐含操作数 |
| `RC_RCX` | rcx | 移位计数 |
| `RC_RDX` | rdx | 除法的高位 |
| `RC_FLOAT` | xmm0-xmm7 | SSE 浮点运算 |
| `RC_ST0` | st0 | x87 long double |

```c
ST_DATA const int reg_classes[NB_REGS] = {
    /* eax */  RC_INT | RC_RAX,
    /* ecx */  RC_INT | RC_RCX,
    /* edx */  RC_INT | RC_RDX,
    0,                        /* rbx — 不使用 */
    0,                        /* rsp — 栈指针 */
    0,                        /* rbp — 帧指针 */
    RC_RSI,                   /* rsi — 仅参数传递 */
    RC_RDI,                   /* rdi — 仅参数传递 */
    RC_R8,  RC_R9,  RC_R10,  RC_R11,
    0, 0, 0, 0,              /* r12-r15 — callee-saved，不使用 */
    RC_FLOAT | RC_XMM0,      /* xmm0 */
    RC_FLOAT | RC_XMM1,      /* xmm1 */
    RC_FLOAT | RC_XMM2,      /* xmm2 */
    RC_FLOAT | RC_XMM3,      /* xmm3 */
    RC_FLOAT | RC_XMM4,      /* xmm4 */
    RC_FLOAT | RC_XMM5,      /* xmm5 */
    RC_XMM6,                 /* xmm6 — callee-saved on Windows */
    RC_XMM7,                 /* xmm7 — callee-saved on Windows */
    RC_ST0                   /* st0 — x87 */
};
```

注意 rsi 和 rdi **不在** `RC_INT` 中——它们仅用于参数传递，不参与通用整数运算。这是因为 `gv(RC_INT)` 需要找到一个可以自由使用的寄存器，而 rsi/rdi 在某些上下文中可能已被 `gfunc_call()` 用于参数传递。

### 5.10.2 SysV AMD64 调用约定

**参数传递：**

```c
#define REGN 6
static const uint8_t arg_regs[REGN] = {
    TREG_RDI, TREG_RSI, TREG_RDX, TREG_RCX, TREG_R8, TREG_R9
};
```

整数/指针参数按顺序使用 rdi, rsi, rdx, rcx, r8, r9。浮点参数使用 xmm0-xmm7。超出寄存器数量的参数通过栈传递。

**返回值：**

```c
#define REG_IRET  TREG_RAX    /* 整数返回值 */
#define REG_IRE2  TREG_RDX    /* 第二个整数返回值（128 位） */
#define REG_FRET  TREG_XMM0  /* 浮点返回值 */
#define REG_FRE2  TREG_XMM1  /* 第二个浮点返回值 */
```

### 5.10.3 `gfunc_prolog()` — 函数序言

```c
void gfunc_prolog(Sym *func_sym)
{
    /* 跳过序言空间（稍后回填） */
    ind += FUNC_PROLOG_SIZE;  /* FUNC_PROLOG_SIZE = 11 */
    func_sub_sp_offset = ind;

    /* 如果函数返回结构体，添加隐式指针参数 */
    if (ret_mode == x86_64_mode_memory) {
        push_arg_reg(reg_param_index);
        func_vc = loc;
        reg_param_index++;
    }

    /* 处理每个参数 */
    while ((sym = sym->next) != NULL) {
        if (reg_param_index < REGN) {
            if (is_sse_float(type->t)) {
                /* 浮点参数：movq xmmN, loc(%rbp) */
                o(0xd60f66);
                gen_modrm(reg_param_index, VT_LOCAL, NULL, addr);
            } else {
                /* 整数参数：mov rN, loc(%rbp) */
                gen_modrm64(0x89, arg_regs[reg_param_index],
                           VT_LOCAL, NULL, addr);
            }
        }
        addr += 8;
        reg_param_index++;
    }

    /* 可变参数函数：保存所有寄存器参数到栈上 */
    while (reg_param_index < REGN) {
        if (func_var) {
            gen_modrm64(0x89, arg_regs[reg_param_index],
                       VT_LOCAL, NULL, addr);
            addr += 8;
        }
        reg_param_index++;
    }
}
```

**`gfunc_epilog()` — 函数尾声：**

```c
void gfunc_epilog(void)
{
    v = (-loc + 15) & -16;  /* 对齐到 16 字节 */
    saved_ind = ind;

    /* 回填序言代码 */
    ind = func_sub_sp_offset - FUNC_PROLOG_SIZE;
    o(0xe5894855);  /* push %rbp; mov %rsp, %rbp */
    o(0xec8148);    /* sub $v, %rsp */
    gen_le32(v);
    ind = saved_ind;

    /* 生成尾声 */
    o(0xc9);  /* leave */
    o(0xc3);  /* ret */
}
```

**生成的代码结构：**

```asm
func:
    push    %rbp                # 保存帧指针
    mov     %rsp, %rbp          # 建立新帧
    sub     $N, %rsp            # 分配局部变量空间
    mov     %rdi, -8(%rbp)      # 保存第一个参数
    mov     %rsi, -16(%rbp)     # 保存第二个参数
    ; ... 函数体 ...
    leave                       # 等价于 mov %rbp,%rsp; pop %rbp
    ret
```

注意 `gfunc_epilog()` 使用**回填**技术：函数序言的 `push %rbp; mov %rsp, %rbp; sub $N, %rsp` 指令在函数体生成之前无法知道 `N` 的值（因为局部变量在解析过程中陆续分配）。因此先跳过 11 字节，生成函数体，最后在 `gfunc_epilog()` 中回填序言。

### 5.10.4 `load()` — 寄存器加载

`load()` 是 x86-64 后端最复杂的函数之一，它处理从任意 SValue 位置到指定寄存器的加载。

**从常量加载：**

```c
if (v == VT_CONST) {
    if (fr & VT_SYM) {
        /* 符号引用：lea sym(%rip), %r */
        orex(1, 0, r, 0x8d);
        o(0x05 + REG_VALUE(r) * 8);
        gen_addrpc32(fr, sv->sym, fc);
    } else if (is64_type(ft)) {
        if (sv->c.i >> 32) {
            /* 64 位常量：movabs $imm64, %r */
            orex(1, r, 0, 0xb8 + REG_VALUE(r));
            gen_le64(sv->c.i);
        } else {
            /* 32 位常量（零扩展到 64 位）：mov $imm32, %r */
            orex(0, r, 0, 0xb8 + REG_VALUE(r));
            gen_le32(sv->c.i);
        }
    }
}
```

**从栈帧加载（VT_LOCAL）：**

```c
else if (v == VT_LOCAL) {
    /* lea offset(%rbp), %r */
    orex(1, 0, r, 0x8d);
    gen_modrm(r, VT_LOCAL, sv->sym, fc);
}
```

**从内存加载（VT_LVAL）：**

```c
if (fr & VT_LVAL) {
    /* 根据类型选择指令 */
    if ((ft & VT_BTYPE) == VT_FLOAT) {
        b = 0x6e0f66;    /* movd mem, xmm */
    } else if ((ft & VT_BTYPE) == VT_DOUBLE) {
        b = 0x7e0ff3;    /* movq mem, xmm */
    } else if ((ft & VT_TYPE) == VT_BYTE) {
        b = 0xbe0f;      /* movsbl mem, %r (符号扩展) */
    } else if ((ft & VT_TYPE) == (VT_BYTE | VT_UNSIGNED)) {
        b = 0xb60f;      /* movzbl mem, %r (零扩展) */
    } else if ((ft & VT_TYPE) == VT_SHORT) {
        b = 0xbf0f;      /* movswl mem, %r */
    } else {
        b = 0x8b;        /* mov mem, %r */
    }
    gen_modrm64(b, r, fr, sv->sym, fc);
}
```

**ModR/M 编码：**

`gen_modrm()` 和 `gen_modrm64()` 是 x86-64 后端的基础设施，负责生成 ModR/M 字节和 SIB 字节：

```c
static void gen_modrm_impl(int op_reg, int r, Sym *sym, int c, int is_got)
{
    op_reg = REG_VALUE(op_reg) << 3;
    if ((r & VT_VALMASK) == VT_CONST) {
        if (!(r & VT_SYM)) {
            /* 绝对地址：[disp32] */
            o(0x04 | op_reg);
            oad(0x25, c);
        } else {
            /* RIP 相对：(%rip)+disp32 */
            o(0x05 | op_reg);
            gen_addrpc32(r, sym, c);
        }
    } else if ((r & VT_VALMASK) == VT_LOCAL) {
        /* rbp 相对 */
        if (c == (signed char)c) {
            o(0x45 | op_reg);  /* disp8 */
            g(c);
        } else {
            oad(0x85 | op_reg, c);  /* disp32 */
        }
    } else {
        /* 寄存器间接：(%reg) */
        g(0x00 | op_reg | REG_VALUE(r));
    }
}
```

ModR/M 字节的编码格式：

```
  7   6   5   4   3   2   1   0
+---+---+---+---+---+---+---+---+
|  mod  |    reg    |    r/m    |
+---+---+---+---+---+---+---+---+

mod=00: [r/m]              (寄存器间接寻址)
mod=01: [r/m + disp8]      (8 位偏移)
mod=10: [r/m + disp32]     (32 位偏移)
mod=11: r/m (寄存器直接)

reg:    操作码扩展或目标寄存器
r/m:    基址寄存器
```

对于 `VT_LOCAL`（即 `(%rbp)` + 偏移），mod=01 或 mod=10，r/m=101（rbp 的编码）。

### 5.10.5 `store()` — 存储到内存

```c
void store(int r, SValue *v)
{
    if (bt == VT_FLOAT) {
        o(0x7e0f66);     /* movd xmm, mem */
    } else if (bt == VT_DOUBLE) {
        o(0xd60f66);     /* movq xmm, mem */
    } else if (bt == VT_LDOUBLE) {
        o(0xc0d9);       /* fld %st(0) */
        o(0xdb);         /* fstpt mem */
    } else if (bt == VT_BYTE || bt == VT_BOOL) {
        orex(0, 0, r, 0x88);  /* movb %r, mem */
    } else if (is64_type(bt)) {
        op64 = 0x89;           /* movq %r, mem */
    } else {
        orex(0, 0, r, 0x89);  /* movl %r, mem */
    }
    gen_modrm64(op64, r, v->r, v->sym, fc);
}
```

### 5.10.6 `gfunc_call()` — 函数调用

SysV ABI 的函数调用生成是最复杂的后端函数之一：

```c
void gfunc_call(int nb_args)
{
    save_regs(nb_args);  /* 保存所有活跃寄存器 */

    /* 分类每个参数 */
    for (i = 0; i < nb_args; i++) {
        mode = classify_x86_64_arg(&sv->type, ...);
        switch (mode) {
        case x86_64_mode_integer:
            /* 整数参数 → 寄存器或栈 */
            break;
        case x86_64_mode_sse:
            /* 浮点参数 → xmm 寄存器或栈 */
            break;
        case x86_64_mode_memory:
            /* 大结构体 → 通过栈传递（复制） */
            break;
        }
    }

    /* 分配整数参数到寄存器 */
    gen_reg = 0;
    for (i = 0; i < nb_args; i++) {
        if (onstack[i] == 1) {  /* 整数寄存器参数 */
            gv(RC_INT);
            if (gen_reg < REGN) {
                d = arg_regs[gen_reg];
                orex(1, d, r, 0x89);  /* mov %r, %arg_reg */
                o(0xc0 + REG_VALUE(r) * 8 + REG_VALUE(d));
            }
            gen_reg++;
        }
    }

    /* 分配 SSE 参数到 xmm 寄存器 */
    sse_reg = 0;
    for (i = 0; i < nb_args; i++) {
        if (onstack[i] == 2) {  /* SSE 寄存器参数 */
            gv(RC_FLOAT);
            if (sse_reg < 8) {
                /* 确保在正确的 xmmN 中 */
            }
            sse_reg++;
        }
    }

    /* 调用目标 */
    vrotb(nb_args + 1);  /* 将函数地址旋转到栈顶 */
    r = gv(RC_INT);      /* 加载函数地址到寄存器 */
    o(0xff);             /* call *%r */
    o(0xd0 + REG_VALUE(r));

    /* 处理返回值 */
    vtop -= nb_args + 1;  /* 弹出参数和函数地址 */
    vpushi(0);
    PUT_R_RET(vtop, func_vt->type.t);  /* 设置返回值寄存器 */
}
```

---

## 5.11 常量折叠

### 5.11.1 `gen_opic()` — 整数常量折叠

`gen_opic()` 在调用后端 `gen_opi()` 之前，尝试在编译时计算结果：

```c
static void gen_opic(int op)
{
    int c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    int c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    uint64_t l1 = c1 ? value64(v1->c.i, v1->type.t) : 0;
    uint64_t l2 = c2 ? value64(v2->c.i, v2->type.t) : 0;

    if (c1 && c2) {
        /* 两个操作数都是常量：完全折叠 */
        switch(op) {
        case '+': l1 += l2; break;
        case '-': l1 -= l2; break;
        case '*': l1 *= l2; break;
        case '&': l1 &= l2; break;
        case '|': l1 |= l2; break;
        case '^': l1 ^= l2; break;
        case TOK_SHL: l1 <<= (l2 & shm); break;
        case TOK_EQ:  l1 = (l1 == l2); break;
        /* ... */
        }
        v1->c.i = value64(l1, v1->type.t);
        vtop--;  /* 弹出一个操作数，结果留在 v1 中 */
    }
```

**单操作数优化**：当只有一个操作数是常量时，`gen_opic()` 执行多种代数简化：

```c
    else {
        /* 交换律：将常量放到右侧 */
        if (c1 && (op == '+' || op == '&' || op == '^' || op == '|' || op == '*'))
            vswap();

        /* 零消除 */
        if (c2 && l2 == 0 && (op == '+' || op == '-' || op == '|' || op == '^'))
            vtop--;  /* x + 0 = x */

        /* 恒等消除 */
        if (c2 && l2 == 1 && (op == '*' || op == '/' || op == TOK_PDIV))
            vtop--;  /* x * 1 = x */

        /* 乘法强度削减：2 的幂次乘法 → 移位 */
        if (c2 && (op == '*') && l2 > 0 && (l2 & (l2 - 1)) == 0) {
            int n = 0;
            while (l2 > 1) { l2 >>= 1; n++; }
            vtop->c.i = n;
            op = TOK_SHL;  /* x * 8 → x << 3 */
            goto general_case;
        }

        /* 除法强度削减：2 的幂次除法 → 移位 */
        if (c2 && (op == TOK_PDIV) && l2 > 0 && (l2 & (l2 - 1)) == 0) {
            /* ... 类似处理，转换为 SAR ... */
        }

        /* 2 的幂次取模 → 掩码 */
        if (c2 && (op == TOK_UMOD) && l2 > 0 && (l2 & (l2 - 1)) == 0) {
            vtop->c.i = l2 - 1;
            op = '&';  /* x % 8 → x & 7 */
            goto general_case;
        }
    }
}
```

### 5.11.2 `gen_opif()` — 浮点常量折叠

```c
static void gen_opif(int op)
{
    c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;

    if (c1 && c2) {
        /* 提取浮点值 */
        if (bt == VT_FLOAT) {
            f1 = v1->c.f; f2 = v2->c.f;
        } else if (bt == VT_DOUBLE) {
            f1 = v1->c.d; f2 = v2->c.d;
        } else {
            f1 = v1->c.ld; f2 = v2->c.ld;
        }

        /* 只对有限数进行常量折叠（排除 NaN 和 Infinity） */
        if (!(ieee_finite(f1) || !ieee_finite(f2)) && !CONST_WANTED)
            goto general_case;

        switch(op) {
        case '+': f1 += f2; break;
        case '-': f1 -= f2; break;
        case '*': f1 *= f2; break;
        case '/':
            if (f2 == 0.0 && !CONST_WANTED)
                goto general_case;  /* 除零需要运行时异常 */
            f1 /= f2;
            break;
        }
        /* 存储结果 */
        if (bt == VT_FLOAT) v1->c.f = f1;
        else if (bt == VT_DOUBLE) v1->c.d = f1;
        else v1->c.ld = f1;
        vtop--;
    }
}
```

注意浮点常量折叠的一个重要限制：对于 NaN 和 Infinity，折叠只在常量求值上下文（`CONST_WANTED`）中进行。这是因为运行时的浮点运算可能产生 IEEE 754 异常信号，编译时折叠会丢失这些信号。

---

## 5.12 代码抑制 `nocode_wanted`

### 5.12.1 机制概述

`nocode_wanted` 是一个全局整数变量，用于控制代码生成的抑制。它的不同位有不同的含义：

```c
ST_DATA int nocode_wanted;

#define NODATA_WANTED   (nocode_wanted > 0)          /* 不输出静态数据 */
#define DATA_ONLY_WANTED 0x80000000                   /* 函数外部/静态初始化器 */
#define CODE_OFF_BIT     0x20000000                   /* 不可达代码（如 if(0) 的分支） */
#define NOEVAL_MASK      0x0000FFFF                   /* sizeof/typeof 等不求值上下文 */
#define NOEVAL_WANTED    (nocode_wanted & NOEVAL_MASK)
#define CONST_WANTED_BIT 0x00010000                   /* 常量表达式求值 */
#define CONST_WANTED     (nocode_wanted & CONST_WANTED_MASK)
```

### 5.12.2 各标志的使用场景

**`DATA_ONLY_WANTED`（0x80000000）**：

在函数体外部设置。此时编译器处理全局变量声明和静态初始化器，不应生成可执行代码，但需要生成数据段内容。

**`CODE_OFF_BIT`（0x20000000）**：

在不可达代码路径中设置。例如：

```c
if (0) {
    /* 这段代码的 CODE_OFF_BIT 被设置 */
    x = 1;  /* 不生成任何代码 */
}
```

由 `CODE_OFF()` 和 `CODE_ON()` 宏控制。

**`NOEVAL_MASK`（0x0000FFFF）**：

在 `sizeof()`、`typeof()` 等不产生代码的上下文中，通过 `nocode_wanted++` 递增。这是一个计数器，支持嵌套：

```c
/* sizeof(arr[func()]) — func() 不应被调用 */
nocode_wanted++;   /* 进入 sizeof */
/* 解析 arr[func()] — gen_op 不生成代码 */
nocode_wanted--;   /* 离开 sizeof */
```

**`CONST_WANTED_BIT`（0x00010000）**：

在常量表达式求值中设置（如数组大小、case 标签、静态初始化器）。此时即使在 `nocode_wanted` 上下文中，也需要对常量进行求值。

### 5.12.3 代码抑制的效果

当 `nocode_wanted` 非零时，多个关键函数跳过代码生成：

- `get_reg()`：直接返回第一个匹配的寄存器，不检查是否被占用
- `save_reg()` / `save_reg_upstack()`：直接返回，不生成存储指令
- `gv()`：跳过寄存器分配和加载逻辑（在某些路径上）
- `gfunc_call()`：不生成调用指令
- 各种 `o()`、`g()` 输出函数：在 `NODATA_WANTED` 时不输出字节

### 5.12.4 标签解析与代码抑制的交互

```c
/* 在前向标签处清除 nocode_wanted */
static void gsym(int t) {
    while (t) {
        unsigned char *ptr = cur_text_section->data + t;
        uint32_t n = read32le(ptr);
        /* ... 回填跳转 ... */
        t = n;
    }
    /* 如果标签被使用，清除 CODE_OFF_BIT */
    if (ind)
        nocode_wanted &= ~CODE_OFF_BIT;
}
```

当一个前向跳转的目标标签被解析时，如果跳转可能到达当前位置，`CODE_OFF_BIT` 被清除。这正确处理了如下情况：

```c
if (0)
    goto label;
/* CODE_OFF_BIT 设置 */
x = 1;  /* 不生成代码 */
label:
y = 2;  /* CODE_OFF_BIT 清除，恢复代码生成 */
```

---

## 5.13 完整示例：从 C 代码到 x86-64 汇编

### 5.13.1 示例代码

考虑以下 C 函数：

```c
int compute(int a, int b, int c) {
    int x = a + b * c;
    return x;
}
```

我们将逐步跟踪 TinyCC 代码生成器如何将这个函数翻译为 x86-64 汇编。

### 5.13.2 函数序言

解析到函数定义时，`gfunc_prolog()` 被调用：

```
gfunc_prolog 生成:
    ind += 11  (跳过序言空间)

参数保存:
    mov %rdi, -8(%rbp)     # a → 栈帧偏移 -8
    mov %rsi, -16(%rbp)    # b → 栈帧偏移 -16
    mov %rdx, -24(%rbp)    # c → 栈帧偏移 -24

局部变量 x 分配在偏移 -32
```

序言代码在函数尾声时回填：

```asm
    push    %rbp
    mov     %rsp, %rbp
    sub     $32, %rsp           # 4 个 int 参数/局部变量 × 8 字节
```

### 5.13.3 表达式 `a + b * c` 的 vstack 跟踪

**步骤 1：解析标识符 `a`**

```
vpushsym() 将 a 压入 vstack:
    vstack[0]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-8, sym=&a_sym }
    含义：a 是栈帧偏移 -8 处的 int 左值
```

**步骤 2：解析标识符 `b`**

```
vpushsym() 将 b 压入 vstack:
    vstack[0]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-16, sym=&b_sym }
    vstack[1]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-8, sym=&a_sym }
```

**步骤 3：解析标识符 `c`**

```
vpushsym() 将 c 压入 vstack:
    vstack[0]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-24, sym=&c_sym }
    vstack[1]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-16, sym=&b_sym }
    vstack[2]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-8, sym=&a_sym }
```

**步骤 4：执行 `b * c`（gen_op('*')）**

```
gen_op('*'):
  → combine_types: 结果类型为 INT
  → gen_opic('*'):
    - c1=0, c2=0（都不是常量）
    - 不满足任何优化条件
    → 调用 gen_opi('*')
      → gv2(RC_INT, RC_INT):
        - gv(RC_INT) for vtop (c):
          - r = VT_LOCAL|VT_LVAL → 需要加载
          - get_reg(RC_INT) → 返回 TREG_RAX (0)
          - load(0, vtop): mov -24(%rbp), %eax
          - vtop->r = 0 (TREG_RAX)
        - gv(RC_INT) for vtop[-1] (b):
          - 注意：vtop 已经改变了，现在 vtop[-1] 是 b
          - r = VT_LOCAL|VT_LVAL → 需要加载
          - get_reg(RC_INT) → 返回 TREG_RCX (1)
          - load(1, vtop[-1]): mov -16(%rbp), %ecx
          - vtop[-1].r = 1 (TREG_RCX)
      → 生成指令: imul %ecx, %eax
      → vtop--: 弹出 c，结果留在 vstack[0]

vstack[0]: { type=INT, r=TREG_RAX (0), c.i=0 }  (b*c 的结果在 eax)
vstack[1]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-8 }  (a 仍是左值)
```

**步骤 5：执行 `a + (b*c)`（gen_op('+')）**

```
gen_op('+'):
  → combine_types: 结果类型为 INT
  → gen_opic('+'):
    - c1=0, c2=0
    → 调用 gen_opi('+')
      → cc=0（vtop 不是常量）
      → gv2(RC_INT, RC_INT):
        - gv(RC_INT) for vtop (b*c 结果):
          - r=0 (TREG_RAX), 不是左值, r < VT_CONST, reg_classes[0] & RC_INT ✓
          - r_ok = 1 → 已经在正确寄存器中
        - gv(RC_INT) for vtop[-1] (a):
          - r=VT_LOCAL|VT_LVAL → 需要加载
          - get_reg(RC_INT) → TREG_RCX (1) 仍然空闲
          - load(1, vtop[-1]): mov -8(%rbp), %ecx
          - vtop[-1].r = 1 (TREG_RCX)
      → 生成指令: add %ecx, %eax
      → vtop--: 弹出操作数，结果在 TREG_RAX

vstack[0]: { type=INT, r=TREG_RAX (0) }  (a+b*c 的结果在 eax)
```

**步骤 6：赋值给 `x`（vstore）**

```
vstore():
  - 目标（vtop[-1]）是 x 的左值：VT_LOCAL|VT_LVAL, c.i=-32
  - 源（vtop）已在 TREG_RAX 中
  → store(TREG_RAX, vtop[-1]): mov %eax, -32(%rbp)

vstack 清空（vpop）
```

**步骤 7：`return x`**

```
vpushsym() 将 x 压入 vstack:
    vstack[0]: { type=INT, r=VT_LOCAL|VT_LVAL, c.i=-32 }

gv(RC_IRET):  # RC_IRET = RC_RAX
  - 加载 x 到 rax
  - load(TREG_RAX, vtop): mov -32(%rbp), %eax
  - vtop->r = TREG_RAX

gfunc_epilog():
  - leave
  - ret
```

### 5.13.4 最终汇编输出

```asm
compute:
    push    %rbp
    mov     %rsp, %rbp
    sub     $32, %rsp
    mov     %edi, -8(%rbp)       # 保存参数 a
    mov     %esi, -16(%rbp)      # 保存参数 b
    mov     %edx, -24(%rbp)      # 保存参数 c
    mov     -24(%rbp), %eax      # 加载 c
    mov     -16(%rbp), %ecx      # 加载 b
    imul    %ecx, %eax           # b * c
    mov     -8(%rbp), %ecx       # 加载 a
    add     %ecx, %eax           # a + (b * c)
    mov     %eax, -32(%rbp)      # 存储到 x
    mov     -32(%rbp), %eax      # 加载 x 作为返回值
    leave
    ret
```

### 5.13.5 优化观察

读者可能注意到上述汇编存在冗余：`b * c` 的结果已经在 `%eax` 中，但被存储到 `-32(%rbp)` 后又立即加载回来。这是因为 TinyCC 的代码生成器不做**寄存器分配全局优化**——每个语句的结果被独立处理，不跟踪跨语句的值流。

一个优化的编译器（如 GCC -O2）会生成：

```asm
compute:
    mov     %edx, %eax           # eax = c
    imul    %esi, %eax           # eax = b * c
    add     %edi, %eax           # eax = a + b * c
    ret
```

这需要活跃性分析和全局寄存器分配——正是 TinyCC 为追求编译速度而放弃的优化。

---

## 5.14 本章小结与练习

### 本章小结

本章深入分析了 TinyCC 的代码生成器，核心要点如下：

1. **直接代码生成**：TinyCC 不构建中间表示，直接从语法树生成目标机器码。这一设计以牺牲代码质量为代价，换取了极高的编译速度。

2. **虚拟栈（vstack）**：SValue 栈是代码生成器的核心数据结构。每个 SValue 记录值的类型（`type`）、物理位置（`r`, `r2`）和常量/符号信息（`c`, `sym`）。`r` 字段的编码允许值以多种虚拟形态存在：常量、栈偏移、条件码、跳转结果等。

3. **延迟物化**：`gv()` 函数是值从虚拟形态到物理寄存器的"物化"关口。它处理常量加载、内存解引用、位域提取、寄存器类别匹配等所有情况。

4. **后端接口**：平台无关层通过 `load()`/`store()`、`gfunc_call()`/`gfunc_prolog()`/`gfunc_epilog()`、`gen_opi()`/`gen_opf()`、`gjmp()`/`gjmp_cond()`/`gsym()` 等函数与后端交互。

5. **常量折叠**：`gen_opic()` 和 `gen_opif()` 在调用后端之前尝试编译时求值，包括零消除、恒等消除、乘法强度削减等代数优化。

6. **代码抑制**：`nocode_wanted` 机制通过位标志控制代码生成的开关，支持 sizeof/typeof 不求值、不可达代码消除、常量表达式求值等场景。

7. **寄存器分配**：采用简单的线性扫描策略——复用 → 空闲 → 溢出。溢出使用临时栈变量并支持复用。

8. **x86-64 后端**：使用 rax, rcx, rdx, r8-r11 作为通用整数寄存器，xmm0-xmm7 作为浮点寄存器。SysV ABI 使用 rdi, rsi, rdx, rcx, r8, r9 传递前 6 个整数参数。ModR/M 编码是 x86 指令生成的核心机制。

### 练习

**练习 5.1**：vstack 跟踪

给定以下 C 代码，手动跟踪 vstack 状态在每一步的变化（包括 SValue 的 `type`、`r`、`c.i` 字段），假设目标平台为 x86-64：

```c
int result = (a + 1) * (b - 2);
```

其中 `a` 和 `b` 是 `int` 类型的局部变量，分别位于栈帧偏移 -8 和 -16 处。

参考：[exercises/ex1_vstack.md](exercises/ex1_vstack.md)

**练习 5.2**：汇编预测与验证

编写以下 C 函数，手动预测 TinyCC 生成的 x86-64 汇编，然后使用 `tcc -S` 验证你的预测：

```c
int abs_diff(int a, int b) {
    int d = a - b;
    if (d < 0)
        d = -d;
    return d;
}
```

参考：[exercises/ex2_codegen.md](exercises/ex2_codegen.md)

**练习 5.3**：寄存器分配跟踪

对于以下函数，详细跟踪 `get_reg()` 的每次调用，记录哪些寄存器被分配、哪些被溢出：

```c
int foo(int a, int b, int c, int d) {
    int e = a + b;
    int f = c + d;
    int g = e * f;
    return g + a;
}
```

假设只有 3 个可用的通用整数寄存器（rax, rcx, rdx），分析溢出发生的时机和位置。

参考：[exercises/ex3_register.md](exercises/ex3_register.md)

---

## 参考文献

1. Bellard, F. "TCC: Tiny C Compiler." https://bellard.org/tcc/
2. System V Application Binary Interface, AMD64 Architecture Processor Supplement.
3. Intel Corporation. "Intel 64 and IA-32 Architectures Software Developer's Manual."
4. TinyCC 源码：`tccgen.c`（平台无关代码生成），`x86_64-gen.c`（x86-64 后端），`tcc.h`（核心数据结构定义）。
