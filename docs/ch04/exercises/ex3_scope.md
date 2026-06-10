# 练习 4.3：符号表变化追踪

## 目标

通过手动追踪 tcc 编译过程中的 `sym_push`、`sym_pop`、`sym_link` 操作，深入理解符号栈和记号表的交互机制，以及名称遮蔽（name shadowing）的实现原理。

## 背景：关键数据结构和操作

### 数据结构

```
global_stack:   Sym* → Sym* → ... → NULL  (全局符号链表，通过 prev 链接)
local_stack:    Sym* → Sym* → ... → NULL  (局部符号链表，通过 prev 链接)
table_ident[]:  TokenSym* 数组，以记号编号为索引
  每个 TokenSym 有:
    sym_identifier → 当前可见的同名变量/函数
    sym_struct     → 当前可见的同名结构体标签
```

### 关键操作

- **`sym_push(v, type, r, c)`**：
  1. 分配新 Sym `s`
  2. `s->prev = *ps`（压入栈顶，ps 指向 local_stack 或 global_stack）
  3. `*ps = s`
  4. 调用 `sym_link(s, 1)`：`s->prev_tok = *sym_id_ptr; *sym_id_ptr = s`
  5. 设置 `s->sym_scope = local_scope`

- **`sym_link(s, 1)`**（使符号可见）：
  ```
  s->prev_tok = table_ident[idx]->sym_identifier
  table_ident[idx]->sym_identifier = s
  ```

- **`sym_link(s, 0)`**（使符号不可见，恢复旧定义）：
  ```
  table_ident[idx]->sym_identifier = s->prev_tok
  ```

- **`sym_pop(stack, boundary, keep)`**：
  弹出从栈顶到 boundary 的所有符号，对每个符号调用 `sym_link(s, 0)`

---

## 题目

### 题目 1：简单作用域

追踪以下代码的符号表变化。为每个全局标识符（`x`、`f`）计算 `TOK_IDENT` 偏移后的索引，记为 `idx_x`、`idx_f`。

```c
int x = 10;

int f(int a) {
    int b = a + x;
    {
        int x = 100;
        b = b + x;
    }
    return b + x;
}
```

**追踪格式**：对每个操作，记录：
1. 操作名称和参数
2. `local_stack` 的变化（列出栈中所有符号，从顶到底）
3. `global_stack` 的变化（如果有）
4. `table_ident[idx_x]->sym_identifier` 的变化（链表状态）

**追踪开始**：

```
初始状态:
  global_stack = NULL
  local_stack = NULL
  table_ident[idx_x]->sym_identifier = NULL

=== 1. 解析 'int x = 10' (全局) ===
  parse_btype: t = VT_INT
  type_decl: v = 'x'
  has_init = 1, l = VT_CONST

  sym_push('x', VT_INT, VT_CONST|VT_SYM, 0):
    分配 Sym_G_x
    Sym_G_x.prev = NULL (global_stack 为空)
    global_stack = Sym_G_x
    sym_link(Sym_G_x, 1):
      Sym_G_x.prev_tok = NULL
      table_ident[idx_x]->sym_identifier = Sym_G_x
      Sym_G_x.sym_scope = 0

  状态:
    global_stack: [Sym_G_x] → NULL
    local_stack: NULL
    table_ident[idx_x]->sym_identifier: [Sym_G_x] → NULL

  decl_initializer_alloc: x = 10 初始化

=== 2. 解析 'int f(int a)' 函数头 ===
  parse_btype: t = VT_INT
  type_decl: 解析函数声明符

  post_type (函数参数):
    解析参数 'int a':
      sym_push('a', VT_INT, VT_LOCAL|VT_LVAL, 0):
        分配 Sym_P_a
        local_stack = [Sym_P_a]
        sym_link(Sym_P_a, 1):
          Sym_P_a.prev_tok = NULL (a 是新名)
          table_ident[idx_a]->sym_identifier = Sym_P_a
          Sym_P_a.sym_scope = 1

  创建函数原型 Sym，然后 sym_pop 弹出参数符号

  decl 遇到 '{'，调用 gen_function:
    sym_push2(local_stack, SYM_FIELD, 0, 0):  // 哨兵
      local_stack = [Sentinel] → NULL

    sym_push_params:
      重新压入参数 a:
        local_stack = [Sym_P_a2] → [Sentinel] → NULL
        table_ident[idx_a]->sym_identifier = Sym_P_a2

=== 3. 解析 'int b = a + x' (函数体) ===

  请继续追踪...
```

**你的任务**：继续完成步骤 3-6 的追踪，包括：
- `sym_push('b', ...)` 的效果
- 内层作用域中 `sym_push('x', ...)` 的遮蔽效果
- `prev_scope` 弹出内层符号后的恢复效果
- 函数结束时 `sym_pop` 清理所有局部符号

### 题目 2：多重遮蔽

追踪以下代码中每个 `printf` 语句处 `x` 的值和它在符号表中的位置：

```c
int x = 1;

void demo(void) {
    int x = 2;              /* 遮蔽全局 x */
    {
        int x = 3;          /* 遮蔽函数级 x */
        printf("%d\n", x);  /* 输出? */
    }
    printf("%d\n", x);      /* 输出? */
    {
        int x = 4;          /* 再次遮蔽 */
        printf("%d\n", x);  /* 输出? */
    }
    printf("%d\n", x);      /* 输出? */
}
```

**要求**：
1. 对每个 `{` / `}` 标记作用域的进入和退出
2. 记录 `table_ident[idx_x]->sym_identifier` 在每个 `printf` 处指向哪个 Sym
3. 画出 `prev_tok` 链在最内层时的状态

### 题目 3：typedef 与变量的交互

追踪以下代码的符号表变化。注意 typedef 和变量使用同一个标识符命名空间：

```c
typedef int T;

int test(void) {
    T a;                   /* T 解析为 int */
    {
        typedef float T;   /* 遮蔽全局 T */
        T b;              /* T 解析为 float */
        a = (int)b;
    }
    T c;                   /* T 解析为? */
    return a + c;
}
```

**要求**：
1. 说明 `typedef float T` 如何通过 `sym_push` 遮蔽全局的 `typedef int T`
2. 说明 `prev_scope` 如何恢复全局 typedef
3. 第二个 `T c;` 处的 `T` 解析为什么类型？

### 题目 4：函数原型中的参数作用域

追踪以下声明中参数符号的生命周期：

```c
int process(int n, int data[n]);
```

**要求**：
1. 说明 `post_type` 中函数参数解析的步骤
2. 参数 `n` 何时被压入 `local_stack`？
3. 数组维度中的 `n` 如何被解析？（提示：注意 `local_scope` 的递增和 `sym_push_params` 的时序）
4. 参数符号何时被弹出？

---

## 解题工具

### 符号状态记录表

对每个关键位置，填写下表：

| 位置 | local_stack（顶到底） | table_ident[idx]->sym_identifier |
|------|----------------------|----------------------------------|
| 函数入口 | ... | ... |
| 声明 b 之后 | ... | ... |
| 进入内层 { | ... | ... |
| 声明内层 x 之后 | ... | ... |
| 离开内层 } | ... | ... |
| 函数出口 | ... | ... |

### prev_tok 链记录

对每个标识符名，画出 `prev_tok` 链在不同时刻的状态：

```
时刻 T1 (进入内层作用域后):
  table_ident[idx_x]->sym_identifier → [内层 x, scope=2]
    → prev_tok → [函数参数 x, scope=1]
      → prev_tok → [全局 x, scope=0]
        → prev_tok → NULL
```

---

## 思考题

1. 为什么 tcc 使用 `sym_link` 而不是遍历符号栈来查找符号？这种设计的时间复杂度优势是什么？

2. `sym_pop` 的 `keep` 参数在什么场景下会被设置为非零？为什么这些场景不能直接释放符号？

3. 如果两个变量在不同的作用域中同名但类型不同，tcc 如何确保在内层作用域中类型检查使用的是内层的类型？

4. `local_scope` 计数器的值在函数体外、函数体内、嵌套块中分别是多少？这个计数器在 `sym_push` 的重定义检测中起什么作用？
