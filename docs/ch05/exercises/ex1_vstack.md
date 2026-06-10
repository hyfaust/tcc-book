# 练习 5.1：vstack 状态追踪

## 目标

手动追踪 TinyCC 代码生成器在处理复杂表达式时，虚拟栈（vstack）的完整状态变化。通过此练习，深入理解 `SValue` 结构体的语义和代码生成的延迟求值机制。

## 前置知识

- `SValue` 结构体的 `type`、`r`、`r2`、`c.i`、`sym` 字段（参见 5.2.1 节）
- `r` 字段编码：`VT_CONST`(0x30)、`VT_LOCAL`(0x32)、`VT_LVAL`(0x100)、物理寄存器(0-15)（参见 5.2.2 节）
- `gv()` 函数的物化逻辑（参见 5.4 节）
- `gen_op()` 的类型提升和常量折叠逻辑（参见 5.5 节）

## 题目

给定以下 C 代码，目标平台为 x86-64 (SysV ABI)：

```c
int expr(int a, int b) {
    int result = (a + 1) * (b - 2);
    return result;
}
```

已知条件：
- 参数 `a` 通过 `%edi` 传入，由 `gfunc_prolog()` 保存到 `[rbp-8]`
- 参数 `b` 通过 `%esi` 传入，保存到 `[rbp-16]`
- 局部变量 `result` 分配在 `[rbp-24]`
- x86-64 上 `VT_INT` 为 4 字节，`PTR_SIZE` 为 8

## 要求

逐步追踪以下操作序列，填写每一步的 vstack 状态。对每个 SValue 条目，记录 `type.t`、`r`（含标志位）、`c.i`、`sym` 四个关键字段。

### 追踪模板

每一步请按以下格式填写：

```
STEP N: <操作描述>
  调用链: ...
  生成的指令: ...
  vstack:
    [0] type=..., r=..., c.i=..., sym=...    ← vtop
    [1] type=..., r=..., c.i=..., sym=...
    ...
```

### 步骤序列

**STEP 1**: 解析标识符 `a`（`vpushsym`）

**STEP 2**: 解析整数常量 `1`（`vpushi(1)`）

**STEP 3**: 执行加法 `a + 1`（`gen_op('+')`）

> 提示：注意 `gen_opic('+')` 中的常量折叠逻辑——当一个操作数是常量时，它会尝试交换操作数使常量在右侧。但这里两个操作数都不是纯常量（`a` 是 `VT_LOCAL|VT_LVAL`），所以不会触发代数优化。

**STEP 4**: 解析标识符 `b`（`vpushsym`）

**STEP 5**: 解析整数常量 `2`（`vpushi(2)`）

**STEP 6**: 执行减法 `b - 2`（`gen_op('-')`）

**STEP 7**: 执行乘法 `(a+1) * (b-2)`（`gen_op('*')`）

> 提示：此时 `gv2(RC_INT, RC_INT)` 被调用。注意 `vtop[0]`（`b-2` 的结果）和 `vtop[-1]`（`a+1` 的结果）都需要从寄存器中确认或加载。思考 `gv()` 的 `r_ok` 判断逻辑。

**STEP 8**: 赋值 `result = ...`（`vstore`）

**STEP 9**: 返回 `result`（`gv(RC_IRET)` + `gfunc_epilog`）

## 参考答案

<details>
<summary>点击展开参考答案</summary>

### STEP 1: 解析标识符 `a`

```
调用链: expr_primary() → vpushsym()
生成的指令: 无（仅压栈）

vstack:
  [0] type=VT_INT(0x3), r=VT_LOCAL|VT_LVAL(0x132), c.i=-8, sym=&a    ← vtop
```

### STEP 2: 解析整数常量 `1`

```
调用链: vpushi(1) → vpush64(VT_INT, 1) → vsetc()
生成的指令: 无（仅压栈）

vstack:
  [0] type=VT_INT(0x3), r=VT_CONST(0x30), c.i=1, sym=NULL    ← vtop
  [1] type=VT_INT(0x3), r=0x132, c.i=-8, sym=&a
```

### STEP 3: 执行加法 `a + 1`

```
调用链: gen_op('+') → gen_opic('+') → gen_opi('+')

gen_op('+'):
  - combine_types(): int + int → int
  - 非无符号，不修改操作符
  - gen_cast_s(VT_INT): 无操作
  - gen_opic('+'):
    - c1 = (vtop[-1].r & mask) == VT_CONST → 检查 a: r=0x132 → c1=0
    - c2 = (vtop.r & mask) == VT_CONST → 检查 1: r=0x30 → c2=1
    - c2=1 且 op='+'，不满足零消除/恒等消除条件
    - 进入 general_case → gen_opi('+')

gen_opi('+'):
  - cc = (vtop.r & mask) == VT_CONST → cc=1（1 是常量）
  - 进入常量路径:
    vswap(): 交换栈顶（使 a 在 vtop，1 在 vtop[-1]）
    gv(RC_INT) for vtop (a):
      - r=0x132 (VT_LOCAL|VT_LVAL) → r_ok=0
      - get_reg(RC_INT) → TREG_RAX (0)
      - load(TREG_RAX, a): 生成 mov -8(%rbp), %eax
      - vtop->r = 0 (TREG_RAX)
    vswap(): 交换回来（1 在 vtop，a 在 vtop[-1]）
    c = vtop->c.i = 1
    c == (signed char)c → 使用 imm8 路径:
    orex(ll=0, r=TREG_RAX, 0, 0x83): 无 REX
    o(0xc0 | (0 << 3) | 0): o(0xc0)  → add $1, %eax
    g(1): 立即数 1
    完整指令: 83 c0 01 → add $1, %eax

  vtop--: 弹出常量 1

vstack:
  [0] type=VT_INT(0x3), r=TREG_RAX(0x0), c.i=0, sym=NULL    ← vtop
       含义: a+1 的结果在 %eax 中

生成的指令:
    mov -8(%rbp), %eax       # 加载 a
    add $1, %eax             # a + 1
```

### STEP 4: 解析标识符 `b`

```
调用链: vpushsym()
生成的指令: 无

vstack:
  [0] type=VT_INT(0x3), r=0x132, c.i=-16, sym=&b    ← vtop
  [1] type=VT_INT(0x3), r=0x0, c.i=0, sym=NULL
```

### STEP 5: 解析整数常量 `2`

```
调用链: vpushi(2)
生成的指令: 无

vstack:
  [0] type=VT_INT(0x3), r=VT_CONST(0x30), c.i=2, sym=NULL    ← vtop
  [1] type=VT_INT(0x3), r=0x132, c.i=-16, sym=&b
  [2] type=VT_INT(0x3), r=0x0, c.i=0, sym=NULL
```

### STEP 6: 执行减法 `b - 2`

```
调用链: gen_op('-') → gen_opic('-') → gen_opi('-')

gen_opic('-'):
  - c1=0 (b), c2=1 (常量 2)
  - '-' 不是交换律操作，不交换
  - c2=1, l2=2: 不满足零消除条件（op='-' 不在列表中）
  - 进入 general_case → gen_opi('-')

gen_opi('-'):
  - cc=1（vtop=2 是常量）
  - opc=5 (sub 的操作码扩展)
  - 进入常量路径:
    vswap(): 使 b 在 vtop
    gv(RC_INT) for vtop (b):
      - r=0x132 → r_ok=0
      - get_reg(RC_INT):
        rax(0): 被 vstack[2] 占用 (a+1 的结果) → 跳过
        rcx(1): 空闲 → 分配 TREG_RCX
      - load(TREG_RCX, b): 生成 mov -16(%rbp), %ecx
      - vtop->r = 1 (TREG_RCX)
    vswap(): 使 2 在 vtop
    c = 2
    c == (signed char)c → imm8 路径:
    orex(0, TREG_RCX, 0, 0x83): 无 REX
    o(0xc0 | (5 << 3) | 1): o(0xe9) → sub $2, %ecx
    g(2): 立即数 2
    完整指令: 83 e9 02 → sub $2, %ecx

  vtop--: 弹出常量 2

vstack:
  [0] type=VT_INT(0x3), r=TREG_RCX(0x1), c.i=0, sym=NULL    ← vtop
       含义: b-2 的结果在 %ecx 中
  [1] type=VT_INT(0x3), r=TREG_RAX(0x0), c.i=0, sym=NULL
       含义: a+1 的结果在 %eax 中

生成的指令:
    mov -16(%rbp), %ecx      # 加载 b
    sub $2, %ecx             # b - 2
```

### STEP 7: 执行乘法 `(a+1) * (b-2)`

```
调用链: gen_op('*') → gen_opic('*') → gen_opi('*')

gen_opi('*'):
  - ll=0, cc=0（vtop 不是常量）
  - 调用 gv2(RC_INT, RC_INT)

gv2(RC_INT, RC_INT):
  i. gv(RC_INT) for vtop[0] (b-2, 在 TREG_RCX):
     - r = TREG_RCX (0x1)
     - r_ok = !(VT_LVAL) && (1 < VT_CONST) && (reg_classes[1] & RC_INT)
     - r_ok = 1 → 已在正确寄存器中，无需操作

  ii. gv(RC_INT) for vtop[-1] (a+1, 在 TREG_RAX):
      - r = TREG_RAX (0x0)
      - r_ok = !(VT_LVAL) && (0 < VT_CONST) && (reg_classes[0] & RC_INT)
      - r_ok = 1 → 已在正确寄存器中，无需操作

gen_opi('*') 生成乘法:
  - r = vtop[-1].r = TREG_RAX (0)
  - fr = vtop[0].r = TREG_RCX (1)
  - orex(ll=0, fr=TREG_RCX, r=TREG_RAX, 0xaf0f):
    无 REX 前缀
  - o(0xc0 + REG_VALUE(TREG_RCX) + REG_VALUE(TREG_RAX)*8)
    = o(0xc0 + 1 + 0*8) = o(0xc1)
  - 完整指令: 0f af c1 → imul %ecx, %eax

  vtop--: 弹出 b-2

vstack:
  [0] type=VT_INT(0x3), r=TREG_RAX(0x0), c.i=0, sym=NULL    ← vtop
       含义: (a+1)*(b-2) 的结果在 %eax 中

生成的指令:
    imul %ecx, %eax          # (a+1) * (b-2)
```

### STEP 8: 赋值 `result = (a+1)*(b-2)`

```
调用链: vstore()

vstack (赋值前, result 的左值已压入):
  [0] type=VT_INT(0x3), r=TREG_RAX(0x0), c.i=0       ← vtop (乘法结果)
  [1] type=VT_INT(0x3), r=0x132, c.i=-24, sym=&result

vstore():
  - sbt = VT_INT, dbt = VT_INT → 标量存储
  - delayed_cast: dbt 不是 char/short → 无
  - gen_cast: int → int，无操作
  - gv(RC_INT): 已在 TREG_RAX, r_ok=1
  - vtop[-1].r = 0x132 (VT_LOCAL|VT_LVAL), 不是 VT_LLOCAL
  - store(TREG_RAX, &result):
    生成: mov %eax, -24(%rbp)
  - vswap(); vtop--: 清理栈

vstack: 空

生成的指令:
    mov %eax, -24(%rbp)      # 存储到 result
```

### STEP 9: 返回 `result`

```
调用链: greturn() → vpushsym(result) → gv(RC_IRET) → gfunc_epilog()

gv(RC_IRET=RC_RAX):
  - r = 0x132 → r_ok=0
  - get_reg(RC_RAX) → TREG_RAX (0)
  - load(TREG_RAX, result):
    生成: mov -24(%rbp), %eax
  - vtop->r = TREG_RAX

gfunc_epilog():
  生成: leave; ret

vstack: 空（返回值已物化到 %eax）

生成的指令:
    mov -24(%rbp), %eax      # 加载返回值
    leave
    ret
```

### 完整汇编输出

```asm
expr:
    push    %rbp
    mov     %rsp, %rbp
    sub     $24, %rsp
    mov     %edi, -8(%rbp)       # 保存 a
    mov     %esi, -16(%rbp)      # 保存 b
    mov     -8(%rbp), %eax       # STEP 3: 加载 a
    add     $1, %eax             # STEP 3: a + 1
    mov     -16(%rbp), %ecx      # STEP 6: 加载 b
    sub     $2, %ecx             # STEP 6: b - 2
    imul    %ecx, %eax           # STEP 7: (a+1) * (b-2)
    mov     %eax, -24(%rbp)      # STEP 8: 存储到 result
    mov     -24(%rbp), %eax      # STEP 9: 返回值
    leave
    ret
```

</details>

## 思考题

1. 在 STEP 7 中，为什么 `gv2()` 不需要生成任何 `mov` 指令？这与 STEP 3 和 STEP 6 有什么不同？

2. 如果将表达式改为 `(a + 1) * (b - 2) + a`，在最后的加法步骤中，`gv()` 需要对 `a` 做什么操作？为什么？

3. 假设 `a` 和 `b` 是 `char` 类型而非 `int`，`gv()` 在加载时会有什么不同？提示：考虑 `VT_MUSTCAST` 标志和 `movsbl` 指令。
