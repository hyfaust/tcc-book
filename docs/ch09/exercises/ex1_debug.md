# 练习 1: 调试信息格式对比实验

## 背景

TCC 支持两种调试信息格式：STAB 和 DWARF。本练习要求你比较这两种格式的差异。

## 任务

### 任务 1: 生成并检查调试段

1. 使用以下命令分别生成 STAB 和 DWARF 格式的调试信息：
   ```bash
   tcc -g -c -o debug_stab.o debug_test.c
   tcc -gdwarf -c -o debug_dwarf.o debug_test.c
   ```

2. 使用 `readelf -S` 查看两种格式生成的段：
   ```bash
   readelf -S debug_stab.o   # 查看 .stab 和 .stabstr
   readelf -S debug_dwarf.o  # 查看 .debug_* 段
   ```

3. 使用 `size` 命令比较两种格式的大小：
   ```bash
   size debug_stab.o debug_dwarf.o
   ```

### 任务 2: 解析调试信息

1. 使用 `objdump` 查看 STAB 信息：
   ```bash
   objdump --stabs debug_stab.o
   ```

2. 使用 `objdump` 查看 DWARF 信息：
   ```bash
   objdump --dwarf=info debug_dwarf.o
   objdump --dwarf=line debug_dwarf.o
   objdump --dwarf=abbrev debug_dwarf.o
   ```

3. 分析：
   - STAB 中的类型编码字符串（如 `int:t1=r1;-2147483648;2147483647;`）
   - DWARF 中的 abbreviation 表和 DIE 树
   - 行号信息的编码方式

### 任务 3: GDB 调试对比

1. 分别用两种格式编译并用 GDB 调试
2. 测试以下操作在两种格式下的表现：
   - `list` 命令（源码显示）
   - `print variable`（变量打印）
   - `print struct_var`（结构体打印）
   - `info locals`（局部变量）
   - `backtrace`（调用栈）

## 验证标准

- 能正确识别两种格式对应的段名
- 能解释 STAB 类型编码字符串的含义
- 能解释 DWARF abbreviation 和 DIE 的结构
- 理解两种格式在空间效率和调试器兼容性方面的权衡
