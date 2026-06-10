/*
 * vstack_trace.c
 *
 * 第五章配套示例：带注释的 vstack 状态追踪
 *
 * 本文件展示 TinyCC 代码生成器在处理表达式 "int x = a + b * c;"
 * 时，虚拟栈（vstack）在每个步骤的状态变化。
 *
 * 目标平台：x86-64 (SysV ABI)
 * 假设 a, b, c 为 int 类型局部变量，x 为新声明的局部变量。
 *
 * 编译并查看汇编输出：
 *   tcc -S -o asm_output.S vstack_trace.c
 *
 * 本文件中的注释模拟了 TinyCC 内部代码生成器的操作序列。
 * 每个 STEP N 标记对应一次对 vstack 的操作。
 */

/* ===================================================================
 * 示例函数：我们将追踪此函数的代码生成过程
 * =================================================================== */

int compute(int a, int b, int c) {
    int x = a + b * c;
    return x;
}

/* ===================================================================
 * vstack 状态追踪
 *
 * SValue 结构体关键字段说明：
 *   type.t  : 类型标志 (VT_INT=3, VT_LLONG=4, VT_FLOAT=8, ...)
 *   r       : 值的位置 (VT_CONST=0x30, VT_LOCAL=0x32, VT_LVAL=0x100,
 *             物理寄存器 0-15)
 *   r2      : 第二个寄存器（long long 高字），未用时为 VT_CONST
 *   c.i     : 常量值或栈帧偏移量
 *   sym     : 符号引用（局部变量名）
 *
 * 位置编码速查：
 *   r & 0x3f = 0x00  → TREG_RAX  (物理寄存器 rax)
 *   r & 0x3f = 0x01  → TREG_RCX  (物理寄存器 rcx)
 *   r & 0x3f = 0x02  → TREG_RDX  (物理寄存器 rdx)
 *   r & 0x3f = 0x30  → VT_CONST  (编译时常量)
 *   r & 0x3f = 0x32  → VT_LOCAL  (栈帧偏移)
 *   r & 0x100        → VT_LVAL   (左值标志，需要解引用)
 *
 * 初始状态：
 *   vtop 指向 vstack[-1]（栈空）
 *   函数参数已通过 gfunc_prolog() 保存到栈帧：
 *     a → [rbp - 8]
 *     b → [rbp - 16]
 *     c → [rbp - 24]
 *   局部变量 x 将分配在 [rbp - 32]
 *
 * =================================================================== */


/*
 * === STEP 1: 解析标识符 'a' ===
 * 调用链: expr_primary() → vpushsym()
 * 操作: 将变量 a 的左值压入 vstack
 *
 * vstack:
 *   [0] type=VT_INT, r=VT_LOCAL|VT_LVAL(0x132), c.i=-8, sym=a
 *       含义: a 是栈帧偏移 -8 处的 int 左值
 *
 * vtop → [0]
 */


/*
 * === STEP 2: 解析标识符 'b' ===
 * 调用链: expr_primary() → vpushsym()
 * 操作: 将变量 b 的左值压入 vstack
 *
 * vstack:
 *   [0] type=VT_INT, r=0x132, c.i=-16, sym=b    ← vtop
 *   [1] type=VT_INT, r=0x132, c.i=-8,  sym=a
 *
 * vtop → [0]
 */


/*
 * === STEP 3: 解析标识符 'c' ===
 * 调用链: expr_primary() → vpushsym()
 * 操作: 将变量 c 的左值压入 vstack
 *
 * vstack:
 *   [0] type=VT_INT, r=0x132, c.i=-24, sym=c    ← vtop
 *   [1] type=VT_INT, r=0x132, c.i=-16, sym=b
 *   [2] type=VT_INT, r=0x132, c.i=-8,  sym=a
 *
 * vtop → [0]
 */


/*
 * === STEP 4: 执行乘法 'b * c' ===
 * 调用链: gen_op('*') → gen_opic('*') → gen_opi('*')
 *
 * 4a. gen_op() 入口:
 *     - combine_types(): 两个 int → 结果 int
 *     - 不是指针操作，进入 std_op 路径
 *     - 非无符号，不修改操作符
 *     - gen_cast_s(VT_INT): 两个操作数已经是 int，无操作
 *     - 调用 gen_opic('*')
 *
 * 4b. gen_opic('*'):
 *     - c1 = 0 (b 不是常量), c2 = 0 (c 不是常量)
 *     - 不满足任何优化条件
 *     - 调用 gen_opi('*')
 *
 * 4c. gen_opi('*'):
 *     - ll = 0 (不是 long long)
 *     - cc = 0 (vtop 不是常量)
 *     - 调用 gv2(RC_INT, RC_INT)
 *
 * 4d. gv2(RC_INT, RC_INT):
 *     i. gv(RC_INT) for vtop[0] (c):
 *        - r = 0x132 (VT_LOCAL|VT_LVAL) → r_ok = 0 (是左值)
 *        - get_reg(RC_INT) → 扫描寄存器:
 *          rax(0): 不在 vstack 中 → 分配 TREG_RAX
 *        - load(TREG_RAX, vtop):
 *          生成: mov -24(%rbp), %eax
 *        - vtop[0].r = TREG_RAX (0x0000)
 *
 *     ii. gv(RC_INT) for vtop[-1] (b):
 *         - r = 0x132 (VT_LOCAL|VT_LVAL) → r_ok = 0
 *         - get_reg(RC_INT) → 扫描寄存器:
 *           rax(0): 被 vtop[0] 占用 → 跳过
 *           rcx(1): 不在 vstack 中 → 分配 TREG_RCX
 *         - load(TREG_RCX, vtop[-1]):
 *           生成: mov -16(%rbp), %ecx
 *         - vtop[-1].r = TREG_RCX (0x0001)
 *
 * 4e. gen_opi('*') 生成乘法:
 *     生成: imul %ecx, %eax
 *     vtop--: 弹出 c
 *
 * vstack:
 *   [0] type=VT_INT, r=TREG_RAX(0x0000), c.i=0  ← vtop (b*c 的结果在 eax)
 *   [1] type=VT_INT, r=0x132, c.i=-8, sym=a
 *
 * vtop → [0]
 */


/*
 * === STEP 5: 执行加法 'a + (b*c)' ===
 * 调用链: gen_op('+') → gen_opic('+') → gen_opi('+')
 *
 * 5a. gen_op() 入口:
 *     - combine_types(): int + int → int
 *     - 非无符号
 *     - gen_cast_s(VT_INT): 无操作
 *     - 调用 gen_opic('+')
 *
 * 5b. gen_opic('+'):
 *     - c1 = 0 (a 不是常量), c2 = 0 (b*c 不是常量)
 *     - 不满足优化条件
 *     - 调用 gen_opi('+')
 *
 * 5c. gen_opi('+'):
 *     - ll = 0, cc = 0
 *     - opc = 0 (add 的操作码扩展)
 *     - 调用 gv2(RC_INT, RC_INT)
 *
 * 5d. gv2(RC_INT, RC_INT):
 *     i. gv(RC_INT) for vtop[0] (b*c 结果):
 *        - r = TREG_RAX (0x0000)
 *        - r_ok = !(VT_LVAL) && (0 < VT_CONST) && (reg_classes[0] & RC_INT)
 *        - r_ok = 1 → 已经在正确的整数寄存器中，无需操作
 *
 *     ii. gv(RC_INT) for vtop[-1] (a):
 *         - r = 0x132 (VT_LOCAL|VT_LVAL) → r_ok = 0
 *         - get_reg(RC_INT) → 扫描寄存器:
 *           rax(0): 被 vtop[0] 占用 → 跳过
 *           rcx(1): 不在 vstack 中（注意：STEP 4 中 rcx 仅临时使用，
 *                   vtop[-1] 已被弹出更新）→ 分配 TREG_RCX
 *         - load(TREG_RCX, vtop[-1]):
 *           生成: mov -8(%rbp), %ecx
 *         - vtop[-1].r = TREG_RCX (0x0001)
 *
 * 5e. gen_opi('+') 生成加法:
 *     cc = 0 → 使用寄存器-寄存器路径:
 *     orex(ll=0, r=TREG_RAX, fr=TREG_RCX, 0x01):  → 无 REX 前缀
 *     o(0xc0 + REG_VALUE(TREG_RAX) + REG_VALUE(TREG_RCX) * 8)
 *     = o(0xc0 + 0 + 1*8) = o(0xc8)
 *     完整指令: 01 c8 → add %ecx, %eax
 *     vtop--: 弹出 a
 *
 * vstack:
 *   [0] type=VT_INT, r=TREG_RAX(0x0000), c.i=0  ← vtop (a+b*c 在 eax)
 *
 * vtop → [0]
 */


/*
 * === STEP 6: 赋值 'x = a + b * c' ===
 * 调用链: vstore()
 *
 * 6a. 目标（vtop[-1]）是 x 的左值:
 *     vpushsym() 已将 x 压入:
 *     vtop[-1] = { type=VT_INT, r=VT_LOCAL|VT_LVAL(0x132), c.i=-32, sym=x }
 *
 * vstack (赋值前):
 *   [0] type=VT_INT, r=TREG_RAX(0x0000)      ← vtop (a+b*c 的结果)
 *   [1] type=VT_INT, r=0x132, c.i=-32, sym=x
 *
 * 6b. vstore():
 *     - sbt = VT_INT, dbt = VT_INT → 标量存储路径
 *     - delayed_cast 检查: dbt 不是 char/short → 无延迟转换
 *     - gen_cast(&vtop[-1].type): int → int，无操作
 *     - gv(RC_INT): vtop 已在 TREG_RAX 中，r_ok = 1
 *     - 检查 vtop[-1] (x 的左值):
 *       r = VT_LOCAL|VT_LVAL, c.i = -32
 *       不是 VT_LLOCAL → 不需要额外加载地址
 *     - store(TREG_RAX, vtop[-1]):
 *       生成: mov %eax, -32(%rbp)
 *       (使用 orex(0,0,r,0x89) + gen_modrm(r, VT_LOCAL, NULL, -32))
 *     - vswap(); vtop--: 清理栈
 *
 * vstack: 空
 * vtop → vstack[-1]
 */


/*
 * === STEP 7: return x ===
 * 调用链: greturn() → vpushsym() → gv(RC_IRET) → gfunc_epilog()
 *
 * 7a. vpushsym() 将 x 的左值压入:
 *     vstack[0] = { type=VT_INT, r=0x132, c.i=-32, sym=x }
 *
 * 7b. gv(RC_IRET):  # RC_IRET = RC_RAX
 *     - r = 0x132 (VT_LOCAL|VT_LVAL) → r_ok = 0
 *     - get_reg(RC_RAX) → TREG_RAX (0)
 *     - load(TREG_RAX, vtop):
 *       生成: mov -32(%rbp), %eax
 *     - vtop->r = TREG_RAX
 *
 * 7c. gfunc_epilog():
 *     生成: leave; ret
 */


/* ===================================================================
 * 汇编输出总结（由 tcc -S 生成的实际输出）
 * ===================================================================
 *
 * compute:
 *     push    %rbp
 *     mov     %rsp, %rbp
 *     sub     $32, %rsp
 *     mov     %edi, -8(%rbp)       # 保存参数 a (STEP prolog)
 *     mov     %esi, -16(%rbp)      # 保存参数 b
 *     mov     %edx, -24(%rbp)      # 保存参数 c
 *     mov     -24(%rbp), %eax      # STEP 4d.i: 加载 c → eax
 *     mov     -16(%rbp), %ecx      # STEP 4d.ii: 加载 b → ecx
 *     imul    %ecx, %eax           # STEP 4e: b * c
 *     mov     -8(%rbp), %ecx       # STEP 5d.ii: 加载 a → ecx
 *     add     %ecx, %eax           # STEP 5e: a + (b*c)
 *     mov     %eax, -32(%rbp)      # STEP 6b: 存储到 x
 *     mov     -32(%rbp), %eax      # STEP 7b: 加载 x 作为返回值
 *     leave                        # STEP 7c: 恢复帧指针
 *     ret                          # STEP 7c: 返回
 */


/* ===================================================================
 * 辅助：更复杂的表达式示例
 * =================================================================== */

/* 示例 2: 条件表达式与短路求值 */
int logic_example(int a, int b, int c) {
    /*
     * 表达式: a > 0 && b > 0
     *
     * STEP 1: vpushsym(a) → vstack[0] = {a 的左值}
     * STEP 2: vpushi(0)   → vstack[0] = {常量 0}, vstack[1] = {a}
     * STEP 3: gen_op('>') → gen_opic → gen_opi
     *   - gv(RC_INT): 加载 a 到 eax
     *   - gv(RC_INT): 0 → 已经是 VT_CONST
     *   - 生成: cmp $0, %eax  (实际上是比较的反向)
     *   - vtop->r = VT_CMP, cmp_op = TOK_GT
     *   - vstack[0] = {r=VT_CMP, cmp_op=TOK_GT, jtrue=0, jfalse=0}
     *
     * STEP 4: 处理 && (TOK_LAND)
     *   - gvtst(false, 0): 如果 a > 0 为假，跳转到短路点
     *   - 生成: jle forward_label
     *   - vstack 清空
     *
     * STEP 5: vpushsym(b) → vstack[0] = {b 的左值}
     * STEP 6: vpushi(0)   → vstack[0] = {常量 0}, vstack[1] = {b}
     * STEP 7: gen_op('>')
     *   - 类似 STEP 3
     *   - vstack[0] = {r=VT_CMP, cmp_op=TOK_GT}
     *
     * STEP 8: 最终结果
     *   - gvtst 解析跳转链
     *   - 结果类型为 int (0 或 1)
     */
    return (a > 0) && (b > 0);
}

/* 示例 3: 函数调用 */
int call_example(int a, int b) {
    /*
     * 表达式: compute(a, b, a + b)
     *
     * STEP 1: vpushsym(compute) → 函数地址
     * STEP 2: vpushsym(a)       → 第一个参数
     * STEP 3: vpushsym(b)       → 第二个参数
     * STEP 4: vpushsym(a)       → 第三个参数的一部分
     * STEP 5: vpushsym(b)       → 第三个参数的一部分
     * STEP 6: gen_op('+')       → 计算 a + b
     *         生成: 加载 a 和 b，add
     * STEP 7: gfunc_call(3)
     *   - save_regs(): 保存所有活跃寄存器
     *   - 参数 0 (a): gv(RC_INT) → mov -8(%rbp), %edi  (arg_regs[0]=rdi)
     *   - 参数 1 (b): gv(RC_INT) → mov -16(%rbp), %esi  (arg_regs[1]=rsi)
     *   - 参数 2 (a+b): gv(RC_INT) → %edx  (arg_regs[2]=rdx)
     *   - 函数地址: gv(RC_INT) → %rax
     *   - 生成: call *%rax
     *   - 返回值在 %eax 中
     */
    return compute(a, b, a + b);
}

/* 示例 4: 位域操作 */
struct Flags {
    unsigned int enabled : 1;
    unsigned int mode    : 3;
    unsigned int level   : 4;
};

int bitfield_example(struct Flags *f) {
    /*
     * 表达式: f->mode
     *
     * STEP 1: vpushsym(f)      → 指针值
     * STEP 2: 解引用 → 加上 mode 的偏移
     * STEP 3: gv() 处理位域:
     *   - bit_pos = 1, bit_size = 3
     *   - 类型标记 VT_BITFIELD
     *   - 加载完整字: mov (%rdi), %eax
     *   - 右移 1 位: shr $1, %eax
     *   - 左移 28 位: shl $28, %eax  (32 - 1 - 3 = 28)
     *   - 算术右移 29 位: sar $29, %eax  (32 - 3 = 29)
     *   - 结果在 eax 中，类型为 int
     */
    return f->mode;
}

/* ===================================================================
 * main - 验证所有示例函数可正确执行
 * =================================================================== */
#include <stdio.h>

int main(void)
{
    struct Flags flags = { 1, 5 };

    printf("=== vstack_trace 示例运行 ===\n");
    printf("compute(2, 3, 4) = %d  (期望: 2+3*4=14)\n", compute(2, 3, 4));
    printf("logic_example(1, 2, 3) = %d\n", logic_example(1, 2, 3));
    printf("call_example(3, 4) = %d  (期望: compute(3,4,7)=34)\n", call_example(3, 4));
    printf("bitfield_example(&flags) = %d  (期望: mode=5)\n", bitfield_example(&flags));
    return 0;
}
