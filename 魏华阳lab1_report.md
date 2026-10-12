# 实验一：最小可执行内核（比麻雀更小的麻雀）

## 一、实验目的

实验一讲解最小可执行内核和启动流程。内核运行在 QEMU 模拟的 64 位 RISC-V 计算机上。为了把内核正确对接到 QEMU，需要掌握：

1. 使用**链接脚本**描述内存布局；
2. 进行**交叉编译**生成可执行文件，进而生成内核镜像；
3. 使用 **OpenSBI** 作为 bootloader 加载内核镜像，并用 QEMU 模拟；
4. 使用 OpenSBI 提供的服务在屏幕上格式化打印字符串。

## 二、整体逻辑线

本章围绕"让一个最小内核在 QEMU 上跑起来"这条主线，按以下顺序逐步实现：

1. **确定内存布局**：通过链接脚本 `tools/kernel.ld` 指定内核基址 `0x80200000`、入口 `kern_entry`，并划分 `.text/.rodata/.data/.bss` 段；
2. **交叉编译与链接**：`Makefile` 用 `riscv64-unknown-elf-` 前缀的编译器/链接器把源码编译成 ELF（`bin/kernel`），再用 `objcopy` 剥离成二进制镜像（`bin/ucore.img`）；
3. **固件引导**：QEMU 加电后从复位地址 `0x1000` 执行 OpenSBI 固件，OpenSBI 完成硬件初始化后把内核加载到 `0x80200000` 并跳转过去；
4. **内核启动**：`kern_entry` 建立内核栈后进入 C 函数 `kern_init`，完成 BSS 清零、打印启动信息、设置中断入口、初始化时钟并开启时钟中断；
5. **输出通道**：内核打印不走外设驱动，而是通过 `ecall` 陷入 M 模式、调用 OpenSBI 的 SBI 服务完成字符输出。

### 完整执行流

```
加电 → 复位地址 0x1000 (OpenSBI 固件 fw_dynamic 入口)
     → OpenSBI 主初始化，加载内核到 0x80200000
     → 跳转 0x80200000 (kern_entry)
     → la sp, bootstacktop   (建立内核栈)
     → tail kern_init        (进入 C 入口)
     → 清 BSS、打印 "os is loading"、设置 stvec、初始化时钟、开中断
     → while(1) 死循环，由时钟中断驱动，每 100 tick 打印 "100 ticks"
```

## 三、核心文件与模块理解

### 1. `tools/kernel.ld`（链接脚本）

```ld
OUTPUT_ARCH(riscv)
ENTRY(kern_entry)
BASE_ADDRESS = 0x80200000;
```

- `ENTRY(kern_entry)` 指定程序入口符号为 `kern_entry`；
- `BASE_ADDRESS = 0x80200000` 是整个内核的加载基址，`SECTIONS` 从该地址开始依次放置 `.text`、`.rodata`、`.data`、`.bss`；
- 提供 `etext`、`edata`、`end` 三个链接器符号，`kern_init` 用 `end - edata` 计算出 BSS 段大小并清零。

### 2. `kern/init/entry.S`（内核入口）

```asm
kern_entry:
    la sp, bootstacktop
    tail kern_init
```

这是内核最先执行的两条指令，详见练习 1。

### 3. `kern/init/init.c`（内核主流程）

`kern_init` 依次完成：

- `memset(edata, 0, end - edata)`：清零 BSS 段；
- `cprintf("(THU.CST) os is loading ...\n")`：打印启动信息（验证输出链路）；
- `write_csr(stvec, &__alltraps)`：把异常/中断入口写入 `stvec` 寄存器；
- `clock_init()`：设置第一次 S 模式时钟中断；
- `set_csr(sie, SIE_STIE)`：在 `sie` 中开放时钟中断（STIE 位）；
- `intr_enable()`：置 `sstatus.SIE`，全局开中断；
- `while (1);`：死循环，此后全靠时钟中断驱动。

### 4. `libs/sbi.c`（SBI 服务封装）

通过一条 `ecall` 指令陷入 M 模式，请求 OpenSBI 固件提供服务。`sbi_call(type, arg0, arg1, arg2)` 把功能号放进 `x17`、参数放进 `x10/x11/x12`，执行 `ecall`，返回值从 `x10` 取回。封装了：

- `sbi_console_putchar`（功能号 1）：输出一个字符；
- `sbi_console_getchar`（功能号 2）：读取一个字符；
- `sbi_set_timer`（功能号 0）：设置下一次时钟中断的时间。

### 5. `kern/driver/console.c` + `kern/libs/stdio.c`（输出链路）

`cons_putc` 直接调用 `sbi_console_putchar`；上层 `cputch → vcprintf → vprintfmt` 实现类似 `printf` 的格式化输出 `cprintf`。这是实验期间调试的最主要手段。

### 6. `kern/trap/`（异常/中断入口与分发）

- `trapentry.S` 的 `__alltraps`：在内核栈上分配 36×8 字节的 `trapframe`，保存 32 个通用寄存器与 `sstatus/sepc/stval/scause` 四个 CSR，以 `sp` 为参数调用 C 函数 `trap()`，返回后恢复现场并 `sret`；
- `trap.c` 的 `trap()`：根据 `scause` 最高位区分中断（`<0`）与异常（`>=0`）分别派发；
- `interrupt_handler`：清掉 `scause` 最高位得到中断号，识别 `IRQ_S_TIMER`（时钟中断），设置下一次时钟事件、`ticks++`，每 100 次打印 `"100 ticks"`。

### 7. `kern/driver/clock.c`（时钟）

`TIMER_INTERVAL = 100000`，QEMU virt 平台时钟频率约 10 MHz，因此每次中断间隔约 10 ms。`clock_set_next_event` 调用 `sbi_set_timer(get_time() + TIMER_INTERVAL)` 设置下一次时钟中断。

### 8. `kern/driver/intr.c`（中断开关）

`intr_enable/intr_disable` 分别置位/清除 `sstatus.SIE`，控制 S 模式全局中断开关。

## 四、练习解答

### 练习 1：理解内核启动中的程序入口操作

阅读 `kern/init/entry.S`：

```asm
kern_entry:
    la sp, bootstacktop
    tail kern_init
```

**`la sp, bootstacktop` 完成了什么？目的？**

`la` 是一条伪指令，编译后被展开为两条指令（GDB 反汇编验证）：

```asm
0x80200000: auipc sp,0x3     # sp = PC + 0x3000 = 0x80203000
0x80200004: mv    sp,sp       # (addi sp, sp, 0)
```

它把栈指针 `sp` 设置为链接脚本中定义的符号 `bootstacktop`（0x80203000），也就是内核启动栈的栈顶。

- 内核启动栈定义在 `entry.S` 的 `.data` 段：`bootstack: .space KSTACKSIZE`，`KSTACKSIZE = 2 * PGSIZE = 8 KB`；
- `bootstack` 位于 0x80201000，栈顶 `bootstacktop = 0x80201000 + 0x2000 = 0x80203000`。

**目的**：RISC-V 的 `sp` 在加电/进入内核时是未定义的，而 C 语言函数调用（`kern_init` 及其后续调用链）必须依赖栈来保存返回地址、局部变量和参数。因此进入任何 C 代码之前，必须先建立一块合法的内核栈。

**`tail kern_init` 完成了什么？目的？**

`tail` 是一条尾调用伪指令，编译后是一条无条件跳转：

```asm
0x80200008: j 0x8020000a   # 跳转到 kern_init
```

它直接跳转到 C 函数 `kern_init`，**不压栈、不保存返回地址**。

**目的**：`kern_init` 声明为 `__attribute__((noreturn))`，内核初始化后进入死循环、永不返回。因此用 `tail`（尾调用）而不是 `call`，可以省去保存/恢复 `ra` 和栈帧的开销，也避免返回后落入不可预期的代码。跳转后，内核的控制流正式从汇编进入 C 世界。

### 练习 2：使用 GDB 验证启动流程

**调试方法**：

1. 终端 A 启动带调试的 QEMU：`make debug`（即 `qemu-system-riscv64 -machine virt -nographic -bios default -kernel bin/kernel -s -S`，`-S` 表示启动后暂停等待 GDB）；
2. 终端 B 用 `gdb-multiarch` 连接：

```gdb
file bin/kernel
set architecture riscv:rv64
target remote localhost:1234
```

**观察结果**：

连接后首先看到程序停在复位地址：

```
0x0000000000001000 in ?? ()
pc  0x1000  0x1000
```

`0x1000` 处的最初几条指令为（`x/8i $pc`）：

```asm
=> 0x1000: auipc  t0,0x0
   0x1004: addi   a2,t0,40
   0x1008: csrr   a0,mhartid
   0x100c: ld     a1,32(t0)
   0x1010: ld     t0,24(t0)
   0x1014: jr     t0
```

随后在 0x80200000 设断点并 `continue`，命中断点：

```
Breakpoint 1, 0x0000000080200000 in ?? ()
pc  0x80200000  0x80200000
=> 0x80200000: auipc  sp,0x3
   0x80200004: mv     sp,sp
   0x80200008: j      0x8020000a
```

加载符号表后确认：

```
kern_entry    = 0x80200000
kern_init     = 0x8020000a
bootstack     = 0x80201000
bootstacktop  = 0x80203000
```

运行到 `kern_init` 时，`sp` 已是 0x80203000（内核栈顶），证明启动栈建立成功。

**问题回答**：

- **RISC-V 硬件加电后最初执行的几条指令位于什么地址？**
  位于复位地址 **`0x1000`**。这是 RISC-V 规范约定的复位向量地址（QEMU virt 平台同样遵循）。

- **它们主要完成了哪些功能？**
  `0x1000` 处的代码是 **OpenSBI 固件的 fw_dynamic 入口**，功能是：
  1. `csrr a0, mhartid`：读取当前硬件线程 ID；
  2. 从 `0x1000` 偏移处读取 `fw_dynamic` 信息结构（`a1` 为设备树 FDT 地址、`t0` 为 OpenSBI 主代码入口地址）；
  3. `jr t0`：跳转到 OpenSBI 主初始化代码。
  之后 OpenSBI 完成最基础的硬件初始化、把内核加载到 `0x80200000`，最终跳转过去把控制权交给内核 `kern_entry`。

## 五、重要知识点与 OS 原理对应

| 本实验知识点 | 对应 OS 原理 | 简要说明 |
|---|---|---|
| 链接脚本与内存布局 | 程序加载、地址空间 | `BASE_ADDRESS` 决定内核被加载到何处，段的划分对应可执行文件格式 |
| 交叉编译 | 编译原理、交叉开发 | 在 x86 宿主上生成 RISC-V 目标代码 |
| 复位地址 0x1000 | 系统启动 | CPU 上电/复位后 PC 的初始值 |
| OpenSBI 固件引导 | Bootloader / 固件 | 类比 BIOS/UEFI，完成硬件初始化并加载操作系统 |
| SBI `ecall` 服务 | 陷入与系统调用 | 内核以 `ecall` 请求 M 模式固件提供底层服务，是系统调用的雏形 |
| `stvec`/`scause`/`sepc`/`stval` CSR | 中断与异常处理 | 保存向量入口、原因、返回地址、附加信息 |
| 上下文保存/恢复（trapframe） | 进程上下文切换 | 保存全部通用寄存器与关键 CSR，是后续进程切换的基础 |
| 时钟中断 | 定时器/时钟管理 | 通过 SBI 设置定时器，周期产生中断驱动系统 |
| 内核栈 bootstack | 进程/线程栈 | 为 C 运行环境提供栈空间 |

## 六、OS 原理中重要、但本实验未涉及的知识点

- **进程/线程管理**：本实验只有一个执行流，尚无进程概念；
- **虚拟内存与分页机制**：内核直接使用物理地址，未启用 MMU（后续 lab2 引入）；
- **文件系统**：尚未涉及磁盘与文件；
- **系统调用完整机制**：只有内核→SBI 的 `ecall`，尚无用户态→内核的系统调用；
- **用户态/内核态切换**：全程运行在 S 模式；
- **进程调度**：尚无调度器。

## 七、环境搭建与适配说明（本机实际操作）

- 操作系统：Ubuntu 26.04.1 LTS（VMware 虚拟机）；
- 安装工具链：

```bash
sudo apt-get install -y gcc-riscv64-unknown-elf qemu-system-riscv gdb-multiarch make
```

- 编译：在 `lab1` 目录下执行 `make`，生成 `bin/kernel` 与 `bin/ucore.img`；
- 运行：`make qemu`，输出 `(THU.CST) os is loading ...` 并周期打印 `100 ticks`。

> **注意**：Ubuntu 26.04 自带的 QEMU 10.2 使用新版 OpenSBI（v1.8，fw_dynamic 模式），指导书中 `-device loader,file=ucore.img,addr=0x80200000` 的加载方式已不适用（OpenSBI 无法获知入口地址，导致内核不被执行）。需将 `Makefile` 中 `qemu`/`debug` 目标改为 `-kernel $(kernel)`，由 QEMU 把内核 ELF 加载到 0x80200000 并告知 OpenSBI 跳转地址；同时把 `gdb` 目标中的 `riscv64-unknown-elf-gdb` 改为 `gdb-multiarch`。
