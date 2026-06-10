# 练习 2: 内联汇编实验

## 背景

TCC 支持 GCC 风格的内联汇编，允许在 C 代码中嵌入平台特定的汇编指令。本练习要求你编写使用内联汇编的程序。

## 任务

### 任务 1: 平台信息查询

编写一个程序，使用内联汇编查询当前 CPU 的信息：
- **x86-64**：使用 `cpuid` 指令获取 CPU 厂商、型号、特性标志
- **ARM64**：使用 `mrs` 指令读取 `MIDR_EL1`、`CTR_EL0` 等系统寄存器
- **RISC-V**：使用 `csrr` 指令读取 `mvendorid`、`marchid` 等 CSR

### 事务 2: 无锁数据结构

使用 `lock cmpxchg`（x86-64）或 `ldxr/stxr`（ARM64）或 `lr.d/sc.d`（RISC-V）实现一个简单的无锁栈：

```c
typedef struct node {
    int data;
    struct node *next;
} node_t;

typedef struct {
    node_t *top;
} lockfree_stack_t;

void push(lockfree_stack_t *stack, node_t *node);
node_t *pop(lockfree_stack_t *stack);
```

### 任务 3: 精确计时

使用平台特定的高精度计时器测量代码执行时间：
- **x86-64**：`rdtsc` 指令
- **ARM64**：`cntvct_el0` 寄存器
- **RISC-V**：`rdtime` 指令

编写一个基准测试，比较 TCC 编译代码与 GCC `-O2` 编译代码的执行速度。

## 提示

- 使用 `#if defined(__x86_64__)` 等宏进行平台选择
- `asm volatile` 防止编译器优化掉汇编代码
- 破坏列表（clobber list）中的 `"memory"` 确保编译器不会跨汇编指令缓存内存值
- 破坏列表中的 `"cc"` 表示条件码寄存器被修改

## 验证标准

- 程序能在目标平台上正确编译和运行
- 内联汇编的约束和破坏列表正确
- 理解了 `volatile`、约束字符串和破坏列表的作用
