# 练习 5.3：寄存器分配跟踪

## 目标

手动跟踪 TinyCC 的 `get_reg()` 寄存器分配器在函数代码生成过程中的每次调用，理解寄存器复用、溢出（spill）和临时变量机制。

## 前置知识

- `get_reg(int rc)` 的三步策略：复用 → 空闲 → 溢出（参见 5.9.1 节）
- `save_reg(int r)` / `save_reg_upstack(int r, int n)` 的溢出逻辑（参见 5.9.2 节）
- `gv(int rc)` 中的 `r_ok` 判断（参见 5.4.2 节）
- x86-64 的寄存器集合和分类（参见 5.10.1 节）
- `reg_classes[]` 数组的含义

## 背景

TinyCC 的寄存器分配是**按需**（on-demand）的：没有预先的活跃性分析或图着色。每次需要寄存器时，`get_reg()` 扫描 vstack 寻找空闲寄存器，找不到就溢出最老的值。

### 可用寄存器（RC_INT 类别）

在 x86-64 上，`RC_INT` 类别的寄存器包括：

| 编号 | 名称 | `reg_classes` |
|------|------|---------------|
| 0 | rax | RC_INT \| RC_RAX |
| 1 | rcx | RC_INT \| RC_RCX |
| 2 | rdx | RC_INT \| RC_RDX |
| 8 | r8  | RC_R8 |
| 9 | r9  | RC_R9 |
| 10 | r10 | RC_R10 |
| 11 | r11 | RC_R11 |

注意：r8-r11 的 `reg_classes` **不包含** `RC_INT`，它们有自己的独立类别。当 `gv(RC_INT)` 调用 `get_reg(RC_INT)` 时，只会在 rax(0)、rcx(1)、rdx(2) 中选择。

### 关键规则

1. `gv(RC_INT)` 调用 `get_reg(RC_INT)` 时，只会分配 rax, rcx, rdx 三个寄存器之一。
2. 如果三个寄存器都被 vstack 中的条目占用，`get_reg()` 会溢出**栈底**（最老）的值。
3. `save_reg_upstack(r, 1)` 只溢出 vtop 以下的条目——这是 `gv()` 中使用的变体，因为 vtop 即将被覆盖。
4. 溢出的值被存储到函数栈帧的临时变量区域，其 vstack 条目的 `r` 字段被更新为 `VT_LVAL | VT_LOCAL`。

## 题目

给定以下 C 函数：

```c
int heavy(int a, int b, int c) {
    int d = a + b;
    int e = b + c;
    int f = d * e;
    int g = f - a;
    int h = g + b;
    int i = h * c;
    return i;
}
```

假设：
- 只有 rax(0)、rcx(1)、rdx(2) 三个寄存器可用于 `RC_INT` 分配
- 参数 a, b, c 已保存在栈帧中：
  - a → [rbp-8]
  - b → [rbp-16]
  - c → [rbp-24]
- 局部变量 d-i 分别分配在 [rbp-32] 到 [rbp-72]

## 任务

逐步跟踪每个表达式的代码生成过程，记录：

1. **每次 `get_reg()` 调用**：哪个寄存器被分配？是空闲分配还是溢出分配？
2. **每次 `save_reg()` 调用**：哪个寄存器被溢出？溢出到哪里？哪些 vstack 条目受影响？
3. **每次 `gv()` 调用**：`r_ok` 的值是多少？是否需要加载？

### 追踪模板

```
操作: <表达式>
  vstack (操作前):
    [0] r=<位置>, ...   ← vtop
    [1] r=<位置>, ...
    ...

  get_reg(RC_INT): 分配 rax/rcx/rdx
    - 空闲检查: rax=占用/空闲, rcx=..., rdx=...
    - 结果: 分配 <寄存器>
    - [如有溢出] save_reg(<寄存器>): 溢出 vstack[N] 到 [rbp-offset]

  vstack (操作后):
    [0] r=<位置>, ...   ← vtop
    ...
```

## 第一部分：手动跟踪

### `int d = a + b`

**操作前 vstack**（a 和 b 的左值已压入）：

```
[0] type=VT_INT, r=VT_LOCAL|VT_LVAL, c.i=-16 (b)   ← vtop
[1] type=VT_INT, r=VT_LOCAL|VT_LVAL, c.i=-8  (a)
```

**gen_op('+') → gen_opi('+') → gv2(RC_INT, RC_INT)**

请填写以下过程：

```
gv(RC_INT) for vtop[0] (b):
  r = VT_LOCAL|VT_LVAL → r_ok = ?
  get_reg(RC_INT):
    rax: ?  rcx: ?  rdx: ?
    分配: ?
  load(?, b): 生成指令 ?
  vtop[0].r = ?

gv(RC_INT) for vtop[-1] (a):
  r = VT_LOCAL|VT_LVAL → r_ok = ?
  get_reg(RC_INT):
    rax: ?  rcx: ?  rdx: ?
    分配: ?
  load(?, a): 生成指令 ?
  vtop[-1].r = ?

gen_opi('+'): 生成指令 ?
vtop--: 弹出 b

结果 vstack:
  [0] r=? (d 的结果在 ? 中)
```

### `int e = b + c`

此时 vstack 中有 d 的结果。继续跟踪：

```
操作前 vstack:
  [0] type=VT_INT, r=VT_LOCAL|VT_LVAL, c.i=-24 (c)   ← vtop
  [1] type=VT_INT, r=VT_LOCAL|VT_LVAL, c.i=-16 (b)
  [2] type=VT_INT, r=? (d 的结果)
```

请填写 `b + c` 的处理过程。

### `int f = d * e`

此时 vstack 中有 e 和 d 的结果。

```
操作前 vstack:
  [0] r=? (e 的结果)   ← vtop
  [1] r=? (d 的结果)
```

**关键问题**：`gen_opi('*')` 调用 `gv2(RC_INT, RC_INT)` 时：
- `gv(RC_INT)` 对 vtop（e）的 `r_ok` 是多少？
- `gv(RC_INT)` 对 vtop[-1]（d）的 `r_ok` 是多少？
- 是否需要 `save_reg()`？

### `int g = f - a`

```
操作前 vstack:
  [0] r=VT_LOCAL|VT_LVAL, c.i=-8 (a)   ← vtop
  [1] r=? (f 的结果)
```

**关键问题**：此时 rax, rcx, rdx 中有多少个被占用？加载 a 时是否需要溢出？

### `int h = g + b`

### `int i = h * c`

### `return i`

## 第二部分：完整汇编预测

根据你的跟踪结果，预测 TinyCC 生成的完整汇编代码。特别注意溢出导致的额外 `mov` 指令。

```asm
heavy:
    push    %rbp
    mov     %rsp, %rbp
    sub     $?, %rsp
    ; 保存参数
    mov     %edi, -8(%rbp)     # a
    mov     %esi, -16(%rbp)    # b
    mov     %edx, -24(%rbp)    # c
    ; d = a + b
    ; ...
    ; e = b + c
    ; ...
    ; f = d * e
    ; ...
    ; g = f - a
    ; ...
    ; h = g + b
    ; ...
    ; i = h * c
    ; ...
    ; return i
    leave
    ret
```

## 第三部分：验证

```bash
# 保存上述 C 代码为 heavy.c
./tcc -S -o heavy.s heavy.c
cat heavy.s
```

## 参考答案

<details>
<summary>点击展开参考答案</summary>

### `d = a + b`

```
gv(RC_INT) for b:
  r=0x132 → r_ok=0
  get_reg(RC_INT): rax 空闲 → 分配 rax(0)
  load(rax, b): mov -16(%rbp), %eax
  vtop[0].r = rax(0)

gv(RC_INT) for a:
  r=0x132 → r_ok=0
  get_reg(RC_INT):
    rax: 被 vtop[0] 占用 → 跳过
    rcx: 空闲 → 分配 rcx(1)
  load(rcx, a): mov -8(%rbp), %ecx
  vtop[-1].r = rcx(1)

gen_opi('+'): add %ecx, %eax
vtop--
```

**操作后 vstack**（加上 result 左值）：

```
store(rax, &d): mov %eax, -32(%rbp)
vstack 清空（赋值后弹出）
```

### `e = b + c`

```
vpushsym(b): [0] r=0x132, c.i=-16
vpushsym(c): [0] r=0x132, c.i=-24

gv(RC_INT) for c:
  get_reg: rax 空闲 → rax
  mov -24(%rbp), %eax

gv(RC_INT) for b:
  get_reg: rax 占用(c), rcx 空闲 → rcx
  mov -16(%rbp), %ecx

add %ecx, %eax
store(rax, &e): mov %eax, -40(%rbp)
```

注意：此时 vstack 中没有 d 的结果（d 已在上一步 store 后弹出）。TinyCC 没有跨语句的寄存器保活——每个语句的结果被独立存储到栈帧。

### `f = d * e`

```
vpushsym(d): [0] r=0x132, c.i=-32
vpushsym(e): [0] r=0x132, c.i=-40

gv(RC_INT) for e:
  get_reg: rax 空闲 → rax
  mov -40(%rbp), %eax

gv(RC_INT) for d:
  get_reg: rax 占用, rcx 空闲 → rcx
  mov -32(%rbp), %ecx

imul %ecx, %eax
store(rax, &f): mov %eax, -48(%rbp)
```

### `g = f - a`

```
vpushsym(f): [0] r=0x132, c.i=-48
vpushsym(a): [0] r=0x132, c.i=-8

gv(RC_INT) for a:
  get_reg: rax 空闲 → rax
  mov -8(%rbp), %eax

gv(RC_INT) for f:
  get_reg: rax 占用, rcx 空闲 → rcx
  mov -48(%rbp), %ecx

subl %eax, %ecx  (注意: f - a, 不是 a - f)
  实际上 vswap 可能发生，取决于操作数顺序
  gen_opi('-') 中 cc=0, gv2(RC_INT, RC_INT):
    vtop[0] = a (在 rax), vtop[-1] = f (需要加载)
    gv for vtop[0](a): rax, r_ok=1
    gv for vtop[-1](f): get_reg → rcx
    load rcx from -48(%rbp)
  sub %eax, %ecx  → ecx = f - a

store(rcx, &g): mov %ecx, -56(%rbp)
```

### `h = g + b`

```
类似前面的模式，rax 和 rcx 足够，不需要溢出。
mov -56(%rbp), %eax  # load g
mov -16(%rbp), %ecx  # load b
add %ecx, %eax
mov %eax, -64(%rbp)  # store h
```

### `i = h * c`

```
同样不需要溢出。
mov -64(%rbp), %eax  # load h
mov -24(%rbp), %ecx  # load c
imul %ecx, %eax
mov %eax, -72(%rbp)  # store i
```

### `return i`

```
mov -72(%rbp), %eax  # load i → rax (返回值寄存器)
leave
ret
```

### 关键发现

在这个特定例子中，**没有发生寄存器溢出**。原因有两个：

1. 每个表达式的结果在 store 后立即从 vstack 弹出，不会占用寄存器。
2. 每个二元操作只需要 2 个寄存器（rax 和 rcx），而我们有 3 个可用寄存器。

要触发溢出，需要更复杂的表达式链，例如在**单个表达式**中需要超过 3 个寄存器的情况：

```c
int trigger_spill(int a, int b, int c, int d) {
    return (a + b) * (c - d) + (a - c) * (b + d);
}
```

在这个表达式中，子表达式 `(a+b)` 的结果需要保留在寄存器中，同时还要计算 `(c-d)`、`(a-c)`、`(b+d)`，总共需要 4-5 个寄存器，就会触发溢出。

### 完整汇编输出

```asm
heavy:
    push    %rbp
    mov     %rsp, %rbp
    sub     $72, %rsp
    mov     %edi, -8(%rbp)
    mov     %esi, -16(%rbp)
    mov     %edx, -24(%rbp)
    # d = a + b
    mov     -8(%rbp), %ecx       # load a
    mov     -16(%rbp), %eax      # load b
    add     %ecx, %eax           # (注意: 实际顺序可能因 vswap 而不同)
    mov     %eax, -32(%rbp)      # store d
    # e = b + c
    mov     -16(%rbp), %ecx      # load b
    mov     -24(%rbp), %eax      # load c
    add     %ecx, %eax
    mov     %eax, -40(%rbp)      # store e
    # f = d * e
    mov     -32(%rbp), %ecx      # load d
    mov     -40(%rbp), %eax      # load e
    imull   %ecx, %eax
    mov     %eax, -48(%rbp)      # store f
    # g = f - a
    mov     -48(%rbp), %ecx      # load f
    mov     -8(%rbp), %eax       # load a
    subl    %eax, %ecx           # f - a
    mov     %ecx, -56(%rbp)      # store g
    # h = g + b
    mov     -56(%rbp), %ecx      # load g
    mov     -16(%rbp), %eax      # load b
    addl    %ecx, %eax
    mov     %eax, -64(%rbp)      # store h
    # i = h * c
    mov     -64(%rbp), %ecx      # load h
    mov     -24(%rbp), %eax      # load c
    imull   %ecx, %eax
    mov     %eax, -72(%rbp)      # store i
    # return i
    mov     -72(%rbp), %eax
    leave
    ret
```

</details>

## 扩展练习：触发溢出

修改上面的函数，使代码生成过程中**确实发生**寄存器溢出。提示：

```c
int trigger_spill(int a, int b, int c, int d) {
    /* 这个单表达式需要同时保持 a+b 和 c-d 的结果，
       再计算 a-c 和 b+d，总共需要超过 3 个寄存器 */
    return (a + b) * (c - d) + (a - c) * (b + d);
}
```

1. 追踪此表达式的 vstack 状态
2. 找到 `save_reg()` 被调用的位置
3. 记录溢出发生在哪个寄存器上
4. 验证溢出后的汇编输出

## 思考题

1. 为什么 `get_reg()` 从栈底（`vstack`）开始扫描溢出候选，而不是从栈顶？
2. 如果增加更多可用寄存器（例如允许 r8, r9, r10, r11 参与通用分配），`get_reg()` 的逻辑需要什么修改？这对 `gv()` 有什么影响？
3. `save_reg_upstack(r, 1)` 与 `save_reg(r)` 的区别是什么？为什么 `gv()` 使用前者？
4. TinyCC 的寄存器分配策略在什么情况下会产生最差的代码质量？能否构造一个极端例子？
