# Lab3 Prompt
## 功能模块1：
```text
[PROMPT]
完成trapentry.S中的your code，实现跳转到中断处理程序、上下文恢复等功能。我已经写了一点，你看看有没有问题
[RELY]
整个功能模块可以借助的接口函数、结构体、数据结构为：这个模块主要依赖 struct trapframe、trap()、已经提供的 SAVE_ALL、__alltraps、__trapret、RESTORE_ALL 以及 RISC-V 函数调用约定和特权级 Trap 返回机制
[GUARANTEE]
确保在中断或异常后能保存/恢复上下文，在跳转到trap前将trap_frame栈顶sp作为参数
```

##功能模块2：
```text
[PROMPT]
完成功能模块2：trap.c中的interrupt_handler（）和exception_handler（），分别处理中断和异常
[RELY]
主要依赖 struct trapframe、trap_dispatch()、interrupt_handler()、exception_handler()、print_trapframe()、clock_set_next_event()、ticks、TICK_NUM、SHUTDOWN_AFTER、IRQ_S_TIMER、CAUSE_ILLEGAL_INSTRUCTION、CAUSE_BREAKPOINT 以及 libs/sbi.h 中提供的关机相关接口。其中 tf->cause 用于判断当前 Trap 类型，tf->epc 用于记录和调整 Trap 返回后的执行位置，ticks 用于记录时钟中断次数。具体接口和宏均以实验框架中已有定义为准。
[GUARANTEE]
完整的两个函数，实现中断处理，最后使得整个项目lab3能运行，即能在终端输入make qemu后能完整输出十个时钟中断后结束
[SPECIFICATION]
interrupt_handler() 负责处理中断，其中时钟中断需要完成下一次时钟事件设置、时钟计数、统计信息输出以及达到规定次数后的系统关机。其他类型的中断只需要完成基本的类型区分和相应提示输出，无法识别的中断则通过已有的 print_trapframe(tf) 输出完整 Trap frame 以辅助调试；exception_handler() 负责处理实验要求的异常，其中非法指令异常和断点异常需要输出规定格式的信息，并根据对应指令的长度正确更新 tf->epc，使 Trap 返回以后不会重复执行已经触发异常的指令；其他未要求具体处理的异常则使用已有的 print_trapframe(tf) 作为兜底处理。
```