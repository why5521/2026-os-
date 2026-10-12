# 操作系统实验一：最小可执行内核

## 实验基本信息

| 项目 | 内容 |
|---|---|
| 实验名称 | Lab1：最小可执行内核与 RISC-V 启动调试 |
| 小组成员 | 李星泽、魏华阳、张煜坤 |
| 完成日期 | 2026-10-11 |

## 一、实验目的

1. 理解 RISC-V 内核从 OpenSBI 到内核入口的启动过程。
2. 掌握 RISC-V 交叉编译器、QEMU 和 GDB 的基本使用。
3. 使用 QEMU 运行最小内核，并使用 GDB 观察启动过程。
4. 理解 `kern_entry`、内核栈和 `kern_init` 的作用。

## 二、实验环境

- Windows + WSL Ubuntu
- RISC-V GCC 14.2.0
- QEMU 10.2.1
- GDB 17.1（`gdb-multiarch`）
- 模拟平台：QEMU RISC-V virt

## 三、实验整体逻辑

实验首先使用 RISC-V 交叉编译器将内核源代码编译为 RISC-V ELF 文件 `bin/kernel`，再生成内核镜像 `bin/ucore.img`。QEMU 模拟 RISC-V 硬件并启动 OpenSBI，OpenSBI 完成初始化后将控制权交给加载在 `0x80200000` 的内核。内核从 `kern_entry` 开始执行，设置栈指针后跳转到 `kern_init`。

### 项目组成与执行流

- `kern/init/entry.S`：内核最早的汇编入口；
- `kern/init/init.c`：内核初始化函数 `kern_init`；
- `libs/`：字符串、格式化输出和 SBI 调用封装；
- `tools/kernel.ld`：链接脚本，指定入口和内存布局；
- `Makefile`：组织编译、链接和镜像生成。

执行流为：源文件 → 交叉编译 → `bin/kernel`（ELF）→ `bin/ucore.img` → QEMU/OpenSBI → `kern_entry` → `kern_init`。

### ELF、链接脚本与 SBI 输出

`bin/kernel` 是带有段信息和符号信息的 ELF 文件，便于链接器和 GDB 使用；`bin/ucore.img` 是通过 `objcopy` 生成的内核镜像。链接脚本将内核基址设为 `0x80200000`，并依次安排代码段、只读数据段、数据段和 BSS 段。

内核的字符输出通过 `cprintf` 逐层调用 SBI 封装，最终由 `ecall` 请求 OpenSBI 提供的控制台服务，再显示到 QEMU 的终端。

## 四、实验过程与结果

### 1. 编译内核

执行 `make clean` 和 `make`，成功生成 `bin/kernel` 与 `bin/ucore.img`。

![编译结果](./images/compile.png)

### 2. 使用 QEMU 运行

使用 QEMU 加载 `bin/kernel`，OpenSBI 显示下一阶段地址为 `0x80200000`，内核输出 `(THU.CST) os is loading ...`，说明内核启动成功。

![QEMU运行结果](./images/qemu_run.png)

### 3. 使用 GDB 调试

GDB 观察到处理器最初从 `0x1000` 开始执行 OpenSBI 启动代码。随后控制权转移到 `0x80200000` 的 `kern_entry`。执行 `la sp, bootstacktop` 后，`sp` 变为 `0x80203000`，再通过 `tail kern_init` 进入 `kern_init`，其地址为 `0x8020000a`。

![GDB调试结果](./images/gdb_debug.png)

## 五、GDB 练习记录

使用 `set architecture riscv:rv64` 设置目标架构，使用 `target remote localhost:1234` 连接 QEMU。GDB 观察到初始 PC 为 `0x1000`，设置 `break kern_entry` 后继续运行，在 `0x80200000` 命中内核入口；单步执行后，`sp` 变为 `0x80203000`，再单步进入 `kern_init` `0x8020000a`。

## 六、练习一：理解内核入口

`la sp, bootstacktop` 是一条伪指令，将 `bootstacktop` 的地址装入栈指针寄存器 `sp`。进入 C 语言函数前必须先建立有效的内核栈，因此该指令为后续函数调用准备运行环境。

`tail kern_init` 是尾调用伪指令，直接跳转到 `kern_init`，不保存返回地址。由于 `kern_init` 不会返回，所以没有必要保留返回地址。

## 七、实验总结

本实验验证了从 QEMU 复位入口、OpenSBI 固件到内核入口的完整启动链路。通过 GDB 单步调试，我观察到了 `0x1000`、`0x80200000` 和 `0x8020000a` 等关键地址，并理解了 PC、SP、内核入口和启动栈的作用。

## 八、AI 协作开发经验

本实验中使用 AI 辅助理解环境配置、Makefile、启动汇编和 GDB 输出。所有命令均在本地实际执行，并根据终端结果进行验证。完整交互记录见 `prompt.md`。
