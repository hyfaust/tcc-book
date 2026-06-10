# 练习 1：从源码构建 TinyCC 并运行测试

## 目标

从源码构建 TinyCC 编译器，运行测试套件，并使用构建好的 tcc 编译和运行示例程序。

## 前提条件

- Linux 或 macOS 系统（或 Windows 上的 MinGW/MSYS2）
- GCC 或 Clang 编译器（用于编译 tcc）
- make 工具
- 基本的 shell 命令行操作能力

## 步骤

### 第一步：获取源码

如果你还没有 TinyCC 源码：

```bash
# 方法 A：从 Git 克隆
git clone https://repo.or.cz/tinycc.git
cd tinycc

# 方法 B：解压 tarball
tar xjf tcc-0.9.28.tar.bz2
cd tcc-0.9.28
```

### 第二步：配置

```bash
./configure
```

**问题 1.1**：运行 `./configure --help`，列出至少 5 个可用的配置选项，并简述它们的作用。

### 第三步：编译

```bash
make
```

**问题 1.2**：记录 `make` 的输出。编译过程中生成了哪些文件？列出你观察到的 `.o` 文件。

**问题 1.3**：查看生成的 `tcc` 可执行文件大小。与你系统上的 `gcc` 可执行文件大小做对比（使用 `ls -lh` 或 `du -h`）。差距有多大？

```bash
ls -lh tcc
ls -lh $(which gcc)    # 或 $(which cc)
```

### 第四步：运行测试

```bash
make test
```

**问题 1.4**：记录测试结果。有多少测试通过？有多少失败？如果存在失败的测试，尝试分析失败原因。

### 第五步：编译和运行示例程序

```bash
# 使用构建好的 tcc 编译 hello.c
./tcc examples/hello.c -o /tmp/hello_tcc

# 使用 tcc 直接运行（-run 模式）
./tcc -run examples/hello.c

# 使用 tcc 编译 simple_math.c
./tcc book/ch01-intro/examples/simple_math.c -o /tmp/simple_math
/tmp/simple_math
```

**问题 1.5**：`-run` 模式和正常编译有什么区别？（提示：查看是否生成了文件。）

### 第六步：性能对比

```bash
# 测试 tcc 编译速度
time ./tcc examples/hello.c -o /tmp/hello_tcc

# 测试 gcc 编译速度（无优化）
time gcc -O0 examples/hello.c -o /tmp/hello_gcc

# 测试 gcc 编译速度（带优化）
time gcc -O2 examples/hello.c -o /tmp/hello_gcc_o2
```

**问题 1.6**：记录三种编译方式的耗时。tcc 比 gcc -O0 快多少倍？

### 第七步：自举测试

```bash
# 用 tcc 编译 tcc 自身
./tcc -o tcc_bootstrap tcc.c

# 验证自举的 tcc 能正常工作
./tcc_bootstrap -run examples/hello.c
```

**问题 1.7**：自举编译成功说明了什么？如果自举编译出的程序输出与原始 tcc 不同，意味着什么？

## 提交

回答所有 **问题 1.x**，并附上关键命令的输出截图或日志。
