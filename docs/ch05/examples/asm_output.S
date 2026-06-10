	.file	"vstack_trace.c"
	.text
	.globl	compute
	.type	compute, @function
/*
 * compute(int a, int b, int c)
 *
 * 函数序言 (gfunc_prolog + gfunc_epilog 回填):
 *   push %rbp; mov %rsp, %rbp; sub $32, %rsp
 *
 * 参数保存:
 *   a (edi) → [rbp-8]
 *   b (esi) → [rbp-16]
 *   c (edx) → [rbp-24]
 *
 * 局部变量 x 分配在 [rbp-32]
 */
compute:
	pushq	%rbp                       # 保存帧指针
	movq	%rsp, %rbp                 # 建立新帧
	subq	$32, %rsp                  # 分配局部空间 (4 × 8 字节对齐)
	movl	%edi, -8(%rbp)             # 保存参数 a
	movl	%esi, -16(%rbp)            # 保存参数 b
	movl	%edx, -24(%rbp)            # 保存参数 c

	# --- int x = a + b * c ---

	# STEP 4: b * c
	#   gv(RC_INT) for c: get_reg → rax
	movl	-24(%rbp), %eax            # load c → %eax (TREG_RAX)
	#   gv(RC_INT) for b: get_reg → rcx
	movl	-16(%rbp), %ecx            # load b → %ecx (TREG_RCX)
	#   gen_opi('*'): imul
	imull	%ecx, %eax                # %eax = b * c

	# STEP 5: a + (b * c)
	#   gv(RC_INT) for b*c: 已在 %eax, r_ok=1, 无需操作
	#   gv(RC_INT) for a: get_reg → rcx (rax 已被占用)
	movl	-8(%rbp), %ecx             # load a → %ecx
	#   gen_opi('+'): add
	addl	%ecx, %eax                 # %eax = a + b * c

	# STEP 6: x = result
	#   store(TREG_RAX, &x)
	movl	%eax, -32(%rbp)            # 存储结果到 x

	# STEP 7: return x
	#   gv(RC_IRET=RC_RAX): load x → %eax
	movl	-32(%rbp), %eax            # 加载返回值
	leave                              # mov %rbp, %rsp; pop %rbp
	ret
	.size	compute, .-compute


	.globl	logic_example
	.type	logic_example, @function
/*
 * logic_example(int a, int b, int c)
 *
 * 展示短路求值: (a > 0) && (b > 0)
 *
 * 跳转链追踪:
 *   TOK_LAND 的处理:
 *     1. 求值 a > 0 → 设置条件码
 *     2. gvtst(false, 0): 如果 a <= 0, 跳转到 .L_false
 *     3. 求值 b > 0 → 设置条件码
 *     4. gsym(.L_false): 回填跳转目标
 */
logic_example:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$24, %rsp
	movl	%edi, -4(%rbp)             # 保存 a
	movl	%esi, -8(%rbp)             # 保存 b
	movl	%edx, -12(%rbp)            # 保存 c

	# --- (a > 0) && (b > 0) ---

	# 求值 a > 0
	movl	-4(%rbp), %eax             # load a
	cmpl	$0, %eax                   # cmp $0, a
	setg	%al                        # setg %al (1 if a > 0)
	movzbl	%al, %ecx                 # 零扩展到 ecx

	# 短路检查: 如果 a <= 0, 直接跳到结果为 0
	testl	%ecx, %ecx
	je	.L_false                       # a <= 0 → 短路

	# 求值 b > 0
	movl	-8(%rbp), %eax             # load b
	cmpl	$0, %eax                   # cmp $0, b
	setg	%al                        # setg %al (1 if b > 0)
	movzbl	%al, %eax                 # 零扩展到 eax

	# 跳过 false 分支
	jmp	.L_done

.L_false:
	movl	$0, %eax                   # 结果为 0

.L_done:
	leave
	ret
	.size	logic_example, .-logic_example


	.globl	call_example
	.type	call_example, @function
/*
 * call_example(int a, int b)
 *
 * 展示函数调用: compute(a, b, a + b)
 *
 * SysV AMD64 调用约定:
 *   参数 0 → %rdi (a)
 *   参数 1 → %rsi (b)
 *   参数 2 → %rdx (a + b)
 *   返回值在 %eax
 *
 * gfunc_call 的处理顺序:
 *   1. save_regs(): 保存所有活跃寄存器
 *   2. 计算第三个参数 a + b (需要先于寄存器分配)
 *   3. 分配参数到寄存器:
 *      arg 0 (a)    → %edi
 *      arg 1 (b)    → %esi
 *      arg 2 (a+b)  → %edx
 *   4. 函数地址 → %rax
 *   5. call *%rax
 */
call_example:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movl	%edi, -4(%rbp)             # 保存 a
	movl	%esi, -8(%rbp)             # 保存 b

	# --- compute(a, b, a + b) ---

	# 计算第三个参数: a + b
	movl	-4(%rbp), %eax             # load a
	movl	-8(%rbp), %ecx             # load b
	addl	%ecx, %eax                 # a + b
	movl	%eax, %edx                 # 第三个参数 → %rdx

	# 准备前两个参数
	movl	-4(%rbp), %edi             # 第一个参数 a → %rdi
	movl	-8(%rbp), %esi             # 第二个参数 b → %rsi

	# 调用 compute
	# 注意: TCC 可能通过 GOT 间接加载函数地址
	call	compute                    # 直接调用 (同编译单元内)

	leave
	ret
	.size	call_example, .-call_example


	.globl	bitfield_example
	.type	bitfield_example, @function
/*
 * bitfield_example(struct Flags *f)
 *
 * 展示位域提取: f->mode (bit 1-3)
 *
 * struct Flags {
 *     unsigned int enabled : 1;   // bit 0
 *     unsigned int mode    : 3;   // bit 1-3
 *     unsigned int level   : 4;   // bit 4-7
 * };
 *
 * gv() 处理位域的过程:
 *   1. bit_pos = 1, bit_size = 3
 *   2. 加载完整字: mov (%rdi), %eax
 *   3. 右移 bit_pos 位: shr $1, %eax
 *      (现在 mode 在最低 3 位)
 *   4. 左移 (32 - bit_pos - bit_size) 位: shl $28, %eax
 *      (清除高位，保留符号位用于有符号位域)
 *   5. 算术右移 (32 - bit_size) 位: sar $29, %eax
 *      (符号扩展回 int)
 *
 * 注意: 这里 mode 是 unsigned，所以实际上使用
 *       movzbl 或 movl + and 掩码的方式更常见。
 *       TCC 的具体实现取决于类型标志。
 */
bitfield_example:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)             # 保存指针 f (64 位)

	# --- return f->mode ---

	# 加载 f 指向的字
	movq	-8(%rbp), %rax             # 加载指针 f
	movl	(%rax), %eax               # 解引用: 加载完整的 unsigned int

	# 提取位域 mode (bit 1-3, 无符号)
	# 方法: 右移 1 位, 然后与掩码 0x7 做 AND
	shrl	$1, %eax                   # 右移 bit_pos=1 位
	andl	$7, %eax                   # 掩码: (1 << 3) - 1 = 0x7

	leave
	ret
	.size	bitfield_example, .-bitfield_example
