# Lab1 AI 交互记录

> 请根据实际使用情况补充或修改下面内容，不要记录没有发生过的交互。

## 1. 环境配置

### 提示词

请帮我配置 Lab1 所需的 WSL、RISC-V 交叉编译器、QEMU 和 GDB 环境。

### 实际操作结果

安装 Ubuntu WSL，并安装 `make`、`gcc-riscv64-unknown-elf`、`qemu-system-riscv` 和 `gdb-multiarch`。最终确认 GCC、QEMU 和 GDB 均能显示版本号。

## 2. 编译和运行

### 提示词

请指导我编译 Lab1，并使用 QEMU 运行最小内核。

### 实际操作结果

执行 `make clean`、`make` 成功生成 `bin/kernel` 和 `bin/ucore.img`；使用 QEMU 运行后看到 `(THU.CST) os is loading ...`。

## 3. GDB 调试

### 提示词

请指导我使用 GDB 观察 RISC-V 处理器从 OpenSBI 启动到 `kern_entry`、`kern_init` 的过程。

### 实际操作结果

观察到初始 PC 为 `0x1000`，命中 `kern_entry` 地址 `0x80200000`，栈指针为 `0x80203000`，随后进入 `kern_init` 地址 `0x8020000a`。
