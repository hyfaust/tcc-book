# 练习 1：宏展开追踪

## 目标

手动追踪 TinyCC 预处理器中宏展开的完整过程，理解三阶段展开机制。

## 背景

TinyCC 的宏展开分为三个阶段：
1. `macro_subst_tok()` — 函数宏参数收集
2. `macro_arg_subst()` — 参数替换、字符串化、拼接
3. `macro_subst()` — 递归展开，防止无限递归

## 任务

对于以下每个宏展开示例，请逐步写出展开过程。标注每一步涉及的函数。

### 示例 A：简单对象宏

```c
#define N 100
int x = N;
```

**展开过程：**

1. `next()` 读取标识符 `N`，调用 `define_find(N)` 找到定义。
2. `macro_subst_tok()` 发现 `s->d` 非空且不是 `MACRO_FUNC`。
3. 无 `##` 操作，直接调用 `macro_subst(tok_str, nested_list, s->d)`。
4. `macro_subst()` 从宏体中读取 `TOK_PPNUM(100)`，输出到 `tok_str`。
5. 最终 token：`int x = 100;`

---

### 示例 B：函数宏与括号保护

```c
#define SQUARE(x) ((x) * (x))
int y = SQUARE(3 + 4);
```

**展开过程：**

1. `next()` 读取 `SQUARE`，`define_find()` 找到函数宏定义。
2. `macro_subst_tok()` 进入函数宏分支。
3. `next_argstream()` 前瞻读取 `(`，确认是函数调用。
4. 参数收集：
   - 形参 `x` 对应实参 `3 + 4`（注意：`+` 在括号匹配中不是分隔符）。
   - `args->d = [3, ' ', +, ' ', 4]`。
5. `macro_arg_subst()` 替换宏体中的 `x`：
   - 宏体：`(( x ) * ( x ))`
   - 替换后：`(( 3 + 4 ) * ( 3 + 4 ))`
6. `macro_subst()` 递归展开结果（无更多宏）。
7. 最终 token：`int y = (( 3 + 4 ) * ( 3 + 4 ));`

**请解答：** 为什么 `SQUARE(x)` 的定义中 `x` 要用括号包围？如果不加括号（`#define SQUARE(x) x * x`），`SQUARE(3+4)` 会得到什么？

---

### 示例 C：字符串化

```c
#define STR(x) #x
#define XSTR(x) STR(x)
#define VERSION 2
const char *v1 = STR(VERSION);
const char *v2 = XSTR(VERSION);
```

**请分别追踪 `v1` 和 `v2` 的展开过程：**

**v1 的展开：**

1. `next()` 读取 `STR`。
2. `macro_subst_tok()` 收集参数：实参为 `VERSION`（注意：`#` 操作符的参数不预先展开）。
3. `macro_arg_subst()` 遇到 `#`，将 `VERSION` 字符串化为 `"VERSION"`。
4. 结果：`const char *v1 = "VERSION";`

**v2 的展开（请自行完成）：**

提示：`XSTR(VERSION)` 中 `VERSION` 先被 `XSTR` 的参数替换（无 `#`），然后再传给 `STR`。

---

### 示例 D：Token 拼接

```c
#define CONCAT(a, b) a ## b
#define MAKE_VAR(n) var_ ## n
int CONCAT(hello, world) = 42;
int MAKE_VAR(count) = 0;
```

**展开过程（请自行完成）：**

提示：
- `macro_subst_tok()` 中，`MACRO_JOIN` 标志触发 `macro_twosharps()`。
- `macro_twosharps()` 将 `a` 和 `b` 的文本拼接，创建临时文件 `:paste:`，重新词法分析。

---

### 示例 E：可变参数与空 VA_ARGS

```c
#define LOG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
LOG("hello");
LOG("x=%d", 42);
```

**请分别追踪两次 `LOG` 调用的展开过程：**

**LOG("hello")：**

1. 参数收集：`fmt = "hello"`，`__VA_ARGS__ = []`（空）。
2. `macro_arg_subst()` 中，`##__VA_ARGS__` 前有 `,`，且 `__VA_ARGS__` 为空。
3. GNU 扩展：删除 `,` 和 `##`。
4. 结果：`fprintf(stderr, "hello" "\n")`

**LOG("x=%d", 42)（请自行完成）：**

---

### 示例 F：嵌套展开与自引用防止

```c
#define A B
#define B A + 1
int x = A;
```

**展开过程：**

1. `next()` 读取 `A`，`define_find()` 找到定义 `B`。
2. `macro_subst_tok()` 将 `A` 压入 `nested_list`，调用 `macro_subst()` 展开 `B`。
3. `macro_subst()` 读取 `B`，`define_find()` 找到定义 `A + 1`。
4. 检查 `nested_list`：`A` 已在其中！标记 `A` 为 `SYM_FIELD`（不展开）。
5. 输出 `A`（不再展开）和 `1`。
6. `macro_subst_tok()` 弹出 `A`。
7. 最终 token：`int x = A + 1;`

**请解答：** 为什么 `A` 在展开 `B` 的过程中不再展开，但之后可以继续展开？这与 C 标准的哪一条规定对应？

---

## 验证方法

使用 `tcc -E` 验证你的展开结果：

```bash
tcc -E your_file.c
```

使用 `tcc -E -dD` 可以同时看到宏定义和展开结果。

## 思考题

1. 为什么 `macro_subst()` 需要 `nosubst` 标志？它在什么场景下使用？
2. `TOK_PLCHLDR` 占位符在 `##` 操作中起什么作用？
3. 为什么 `__COUNTER__` 宏的参数只能展开一次（`s->e` 缓存机制）？
