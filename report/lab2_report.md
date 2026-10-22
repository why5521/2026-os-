# 实验二：物理内存和页表

## 一、实验目的

1. 理解操作系统如何通过设备树（DTB）发现物理内存的起始地址和大小；
2. 理解以页为单位管理物理内存的方法，掌握 `Page` 描述符数组的建立过程；
3. 理解虚拟地址、物理地址和物理页号之间的转换关系，掌握 `PADDR`、`pa2page`、`page2pa` 等接口；
4. 理解 `pmm_manager` 框架，掌握 First-Fit 与 Best-Fit 连续物理内存分配算法；
5. 从零实现 Buddy System 分配器并设计测试，比较不同页面分配算法的特点。

## 二、整体逻辑线

本实验围绕“发现物理内存 → 建立页描述符 → 管理空闲页 → 分配/释放页”这一条主线展开，调用链为：

```text
kern_init()
  -> pmm_init()
       -> init_pmm_manager()     选择并初始化物理内存管理器
       -> page_init()            从 DTB 取得内存范围，建立 Page 描述符数组
            -> init_memmap()     把内核与描述符数组之外的完整空闲页交给 pmm_manager
       -> check_alloc_page()     调用 pmm_manager->check() 验证分配/释放正确性
```

具体步骤：

1. `entry.S` 在进入 C 语言之前建立启动页表并开启 Sv39 分页，把内核的高地址虚拟空间映射到物理内存；
2. `kern_init()` 先清零 BSS，再调用 `dtb_init()` 从设备树中解析出 `memory_base` 和 `memory_size`；
3. `pmm_init()` 首先通过 `init_pmm_manager()` 选定 `best_fit_pmm_manager`；
4. `page_init()` 计算 `npage`、`pages`、`va_pa_offset`，把所有描述符先标记为保留，再把描述符数组之后的完整空闲页交给当前管理器；
5. `best_fit_init_memmap()` 把空闲页块按地址顺序插入空闲链表；
6. 之后 `alloc_pages()` / `free_pages()` 通过 `pmm_manager` 的函数指针完成页面的分配、拆分、回收和合并。

## 三、核心文件与函数理解

### 1. `kern/driver/dtb.c`：物理内存探测

`kern_entry` 把 OpenSBI 传入的 DTB 物理地址保存在 `boot_dtb` 中。`dtb_init()` 把该地址加上 `PHYSICAL_MEMORY_OFFSET` 得到虚拟地址，校验魔数 `0xd00dfeed`，然后遍历设备树结构，找到 `memory` 节点的 `reg` 属性，得到内存起始地址和长度，分别保存在 `memory_base`、`memory_size` 中，供 `page_init()` 使用。

### 2. `kern/mm/pmm.c`：`page_init()`

`page_init()` 是本实验的第一个核心任务，主要完成以下工作：

- 校验 `get_memory_base()` 和 `get_memory_size()` 的结果，无效时调用 `panic()`；
- 设置 `va_pa_offset = PHYSICAL_MEMORY_OFFSET`，使 `PADDR` 能把内核虚拟地址转换成物理地址；
- 由内存末端计算 `npage = PPN(mem_end)`，由 DRAM 起始地址得到固定的 `nbase`；
- 令 `pages` 指向内核 `end` 之后按页对齐的地址，并把 `[nbase, npage)` 对应的所有 `Page` 描述符初始化为保留状态；
- 计算描述符数组末尾的物理地址，再向上按页对齐，得到空闲区起点；
- 把内存末端向下按页对齐，去掉不完整的尾页；
- 仅当空闲区非空时调用 `init_memmap(base, n)`，把这段完整页面交给当前管理器；
- 按实验指导书要求的格式输出物理内存布局。

关键点是必须区分：

```text
内核虚拟地址 end          -> 用 PADDR 转成物理地址
Page 描述符虚拟地址 pages -> 用 PADDR 转成物理地址
物理地址                  -> PPN / PGSIZE 得到物理页号
物理页号                  -> pages[ppn - nbase] 得到描述符
```

### 3. `kern/mm/pmm.h`：`pmm_manager` 框架

`struct pmm_manager` 用函数指针把一种页面分配算法封装成对象，包含：

- `init`：初始化算法内部数据结构；
- `init_memmap`：把一段初始空闲物理页加入管理；
- `alloc_pages`：分配至少 n 个连续物理页；
- `free_pages`：释放从 `base` 开始的 n 个连续物理页；
- `nr_free_pages`：返回空闲页数；
- `check`：自检函数。

`pmm.c` 中的 `alloc_pages()`、`free_pages()`、`nr_free_pages()` 只是转发到当前 `pmm_manager`，因此切换算法只需要更换 `pmm_manager` 指向的结构体。

### 4. `kern/mm/default_pmm.c`：First-Fit

First-Fit 使用一条按物理地址升序排列的空闲块链表：

- `default_init_memmap()`：把每个页描述符的 `ref`、`flags`、`property` 清零，块首页的 `property` 记录块大小，并置 `PG_property`；
- `default_alloc_pages()`：从头遍历链表，找到第一个 `property >= n` 的块；若块比需求大，就把剩余部分拆成新块插回链表；
- `default_free_pages()`：把释放的页重新标记为空闲块，插入链表，并分别与前后相邻的空闲块合并。

### 5. `kern/mm/best_fit_pmm.c`：Best-Fit

Best-Fit 与 First-Fit 的数据结构相同，区别在分配策略：

- `best_fit_init_memmap()`：按物理地址升序插入空闲块，保证链表有序；
- `best_fit_alloc_pages()`：遍历整条空闲链表，在所有 `property >= n` 的块中选择 `property` 最小的块；若容量相同，保留先遇到的地址较小的块；
- `best_fit_free_pages()`：按地址顺序插入被释放的块，并同时检查前驱和后继，完成相邻空闲块合并；
- 最后定义 `best_fit_pmm_manager`，并在 `init_pmm_manager()` 中把当前管理器切换为它。

### 6. `kern/mm/buddy_pmm.c`：Buddy System

Buddy System 把空闲内存按 2 的幂大小分块，`order = k` 的块包含 `2^k` 个连续页，且起始物理页号按 `2^k` 对齐：

- 每种 order 维护一条空闲链表，块首页的 `property` 记录块大小；
- `buddy_init_memmap()`：由于初始空闲区的起点和长度不一定是 2 的幂，需要把区间分解成若干个自然对齐的 2 的幂块；
- `buddy_alloc_pages()`：找到最小的可用 order，若块比需求大就逐层对半拆分，后半块放回较低 order 的空闲链表；
- `buddy_free_pages()`：释放后通过 `buddy_ppn = ppn ^ size` 找到伙伴块，若伙伴空闲且 order 相同就合并，并不断向上重复；
- `buddy_check()`：自行编写的测试函数，检查链表属性、对齐、分配、释放及合并。

## 四、练习解答

### 功能模块一 任务一：物理内存探测与页描述符初始化

#### spec

实现 `kern/mm/pmm.c` 中的 `static void page_init(void)`：

- 无参数、无返回值，由 `pmm_init()` 调用；
- 读取 `get_memory_base()` / `get_memory_size()`，无效时终止初始化；
- 正确设置 `va_pa_offset`、`npage`、`pages`；
- 所有 `Page` 描述符先置为保留状态；
- 内核镜像、描述符数组以及不完整尾页都不能进入空闲区；
- 只有存在完整空闲页时才调用 `init_memmap()`；
- 输出物理内存布局。

#### prompt

```text
请阅读 lab2 的 kern/mm/pmm.c、pmm.h、memlayout.h、mmu.h、dtb.c 和 kern.ld，
实现 static void page_init(void)。

要求：
1. 从 get_memory_base()/get_memory_size() 获取物理内存范围，非法时 panic；
2. 设置 va_pa_offset = PHYSICAL_MEMORY_OFFSET；
3. 按内存末端设置 npage = PPN(mem_base + mem_size)；
4. pages 指向内核 end 之后按 PGSIZE 对齐的虚拟地址；
5. 先把 [nbase, npage) 的所有 Page 描述符置为 reserved；
6. 描述符数组末尾的物理地址向上按页对齐作为空闲区起点；
7. 内存末端向下按页对齐作为空闲区终点；
8. 只有空闲区非空时才调用 init_memmap(pa2page(free_start), n)；
9. 输出 "physical memory map:" 和 "  memory: ..." 两行；
10. 不得修改 pmm.h 中已有的地址转换接口和测试框架。
```

#### 实现说明

实现中先用 `PADDR(end)` 得到内核物理末尾，再计算描述符数组占用的字节数：

```text
pages_end_pa = PADDR(pages) + (npage - nbase) * sizeof(struct Page)
free_start_pa = ROUNDUP(pages_end_pa, PGSIZE)
free_end_pa   = ROUNDDOWN(mem_end, PGSIZE)
```

所有描述符一开始都 `SetPageReserved()`，因此内核尾页和描述符数组所在页天然不会被分配器使用；`init_memmap()` 内部会检查 `PageReserved()` 并在建立空闲块时清除这些页的保留状态。

#### 思考题：OS 如何获取可用物理内存范围

如果 OS 无法提前知道硬件内存范围，可以采用以下方法：

1. **固件/引导协议传递**：由 bootloader（如 OpenSBI、UEFI、GRUB）探测内存并通过 DTB、UEFI Memory Map、multiboot 信息等结构告诉内核；
2. **设备树/ACPI**：从 `memory` 节点或 SRAT 等表中读取；
3. **主动探测**：在允许的地址空间内尝试读写并捕获总线异常，从而判断哪些地址对应真实 RAM（需要小心不能覆盖固件和外设）；
4. **体系结构寄存器或 ROM 表**：部分平台提供内存大小寄存器或固件配置表。

本实验采用的正是第 1、2 种方式：OpenSBI 扫描硬件后把 DTB 地址放在 `a1`，内核解析 DTB 得到内存范围。

### 功能模块二 任务一：理解 First-Fit

First-Fit 的流程可以概括为：

1. **初始化空闲页**：`default_init_memmap()` 把一段连续物理页视为一个大空闲块，块首页记录大小并置 `PG_property`；
2. **分配连续页**：`default_alloc_pages()` 从空闲链表头开始找第一个容量足够的块，若块太大则把剩余部分拆成新块插回链表；
3. **回收页面**：`default_free_pages()` 清空被释放页的状态，把它们重新组成一个空闲块并按地址插入链表；
4. **合并相邻块**：释放时检查前一个和后一个空闲块，如果物理地址连续就合并，减少外部碎片。

First-Fit 的改进空间：

- 空闲链表无序时分配需要 O(n)，可以按地址或大小组织，甚至使用分离空闲链表、平衡树；
- 每次从头扫描容易产生低地址碎片，可加入 next-fit 指针或按大小分桶；
- 可以记录最大空闲块，快速判断能否满足大块请求；
- 多核环境下需要加锁保护空闲链表。

### 功能模块二 任务二：实现 Best-Fit

#### spec

在 `kern/mm/best_fit_pmm.c` 中实现完整的 Best-Fit 管理器：

- 维护按地址升序排列的空闲块链表和全局空闲页数；
- 分配时遍历所有空闲块，选择容量不小于 n 且容量最小的块；
- 容量相同时选择地址较小的块，保证行为确定；
- 块过大时拆分剩余部分；
- 释放时按地址插入并合并前后相邻空闲块；
- 定义 `best_fit_pmm_manager` 并完成注册；
- 在 `init_pmm_manager()` 中切换到 `best_fit_pmm_manager`；
- 不修改 `basic_check()` 和 `best_fit_check()`。

#### prompt

```text
请参考 kern/mm/default_pmm.c 的 First-Fit 实现，在 kern/mm/best_fit_pmm.c 中
实现 Best-Fit 物理内存管理器。

要求：
1. 使用 free_area_t 维护按物理地址升序的空闲块链表；
2. init_memmap：断言页为 reserved，清零 flags/property/ref，块首页记录大小；
3. alloc_pages：遍历所有空闲块，选择 property >= n 且 property 最小的块；
   若 property 相同，选择地址较小的块；块过大时把 page+n 开始的剩余部分
   作为新空闲块插回链表；
4. free_pages：按地址插入空闲链表，并分别与 prev 和 next 合并相邻块；
5. 定义 best_fit_pmm_manager，注册 init/init_memmap/alloc_pages/free_pages/
   nr_free_pages/check；
6. 在 pmm.c 的 init_pmm_manager() 中改用 &best_fit_pmm_manager；
7. 不修改 basic_check 和 best_fit_check。
```

#### 实现说明

分配时的核心循环如下（伪代码）：

```text
best = NULL
for each free block p:
    if p->property >= n and (best == NULL or p->property < best->property):
        best = p
```

由于链表本身按地址升序，容量相同时先遇到的块地址更小，直接保留即可。释放时先按地址插入，再与 `list_prev`、`list_next` 指向的块比较 `p + p->property == base` 或 `base + base->property == p`，满足条件就合并。

#### 思考题：Best-Fit 的改进空间

- 当前实现每次分配都要遍历整条链表，时间复杂度 O(n)；可以按块大小建立分离链表或平衡树，把查找降到 O(log n)；
- 可以维护“最小可用块”或按 order 分桶，快速判断是否存在合适块；
- 拆分和合并频率较高时，可加入延迟合并或空闲块缓存；
- 多核场景下需要锁或 per-CPU 空闲链表；
- 可结合地址和大小做混合策略，在碎片率和分配速度之间折中。

### 功能模块二 任务三：Buddy System

#### prompt 一：从零生成并接入

```text
请参考 ucore 的 struct pmm_manager 和 struct Page，从零实现一个 Buddy System
物理内存管理器（kern/mm/buddy_pmm.c 和 buddy_pmm.h）。

要求：
1. order=k 的块大小为 2^k 页，起始物理页号必须按 2^k 对齐；
2. 每种 order 一条空闲链表，块首页的 property 记录块大小；
3. init_memmap：把不一定按 2 的幂对齐的初始空闲区分解成若干自然对齐块；
4. alloc_pages(n)：找到最小的可用 order，逐层对半拆分，把后半块加入低 order 链表；
5. free_pages(base,n)：用 ppn ^ size 找伙伴，伙伴空闲且 order 相同时合并，
   并循环向上合并；
6. 定义 buddy_pmm_manager 并注册全部接口；
7. 不改变当前 Best-Fit 的默认启用状态，但代码要能通过切换管理器接入内核。
```

#### prompt 二：设计测试

```text
请为上面的 Buddy System 实现编写 buddy_check()，覆盖：
1. 各 order 链表中块的大小、property 和物理页号对齐；
2. 分配 1/2/4/8 页块，检查非空、互不重叠、地址对齐、PG_property 已清除；
3. 释放后空闲页数恢复到初始值；
4. 释放后能够再次分配一个 8 页块，验证伙伴合并确实发生；
5. 不依赖未公开的内部变量，只通过 pmm_manager 接口和 Page 字段检查。
```

#### 测试覆盖与有效性

`buddy_check()` 覆盖了：

- **结构不变量**：链表中的每个块都置了 `PG_property`，`property` 等于对应 order 的块大小，起始页号按块大小对齐；
- **分配行为**：不同大小请求都能成功，返回块互不重叠，且地址满足对齐约束；
- **释放与合并**：释放后 `nr_free_pages()` 回到初始值，说明没有丢页或重复计数；
- **再次分配**：释放多个块后仍能分配 8 页块，说明伙伴块确实被合并，而不是仅仅挂在链表上。

这些检查能有效发现常见的 Buddy 实现错误，例如拆分时忘记把后半块放回链表、合并时用错伙伴页号、`nr_free` 增减不一致、块首页标志位没有正确清除等。

#### 接入方式

当前提交中 `init_pmm_manager()` 仍然选择 `best_fit_pmm_manager`，以满足评分脚本对 `[Test 2] Best-Fit: PASS` 的要求。若需要单独运行 Buddy System，只需把：

```c
pmm_manager = &best_fit_pmm_manager;
```

改成：

```c
pmm_manager = &buddy_pmm_manager;
```

`page_init()` 会自动调用 `buddy_init_memmap()`，随后 `check_alloc_page()` 会调用 `buddy_check()`。

### 扩展任务 Challenge：SLUB

本次未完成 SLUB 扩展任务，因此不在报告中提交相关 prompt 与测试。

## 五、运行结果

编译并运行后，内核输出（节选）如下：

```text
DTB Init
HartID: 0
DTB Address: 0x87e00000
Physical Memory from DTB:
  Base: 0x0000000080000000
  Size: 0x0000000008000000 (128 MB)
  End:  0x0000000087ffffff
DTB init completed
(THU.CST) os is loading ...
Special kernel symbols:
  entry  0xffffffffc02000d6 (virtual)
  etext  0xffffffffc0201670 (virtual)
  edata  0xffffffffc0206018 (virtual)
  end    0xffffffffc0206078 (virtual)
Kernel executable memory footprint: 24KB
memory management: best_fit_pmm_manager
physical memory map:
  memory: 0x0000000080000000, [0x0000000080000000, 0x0000000087ffffff].
check_alloc_page() succeeded!
satp virtual address: 0xffffffffc0205000
satp physical address: 0x0000000080205000
```

在临时目录中注入官方测试后运行 `tools/grade.sh`，结果为：

```text
  -check page init:                          OK
  -check best fit:                           OK
Total Score: 20/20
```

## 六、遇到的问题与解决

### 1. 打开 Sv39 后取指缺页

**现象**：内核在 QEMU 中完全没有输出，QEMU 异常日志显示：

```text
cause: 0xc (exec_page_fault), epc: 0x0000000080200038
```

`0x80200038` 正是 `csrw satp` 之后的 `sfence.vma`。

**原因**：启动页表只把高地址 `0xFFFFFFFFC0000000` 开始的 1 GiB 映射到物理内存，没有映射当前正在执行的物理地址 `0x80200000` 附近。新版 QEMU 在写 `satp` 后不会继续在同一个翻译块中执行后续指令，因此下一条指令立即按新页表取指并缺页。

**解决**：在 `boot_page_table_sv39` 中增加一个恒等映射项，把 `VA=0x80000000` 映射到 `PA=0x80000000`，使写 `satp` 后的几条指令仍能正常取指；随后 `jr` 跳到高地址虚拟空间，仍由原来的最后一项映射负责。该修改不影响 `page_init()` 和评分测试。

### 2. `-device loader` 不设置入口地址

**现象**：使用 Makefile 原来的 `-device loader,file=bin/ucore.img,addr=0x80200000` 时，OpenSBI 打印 `Domain0 Next Address: 0x0`，内核不会开始执行。

**原因**：新版 QEMU 的 `loader` 设备只负责把文件写入内存，不再像旧版本那样顺带设置下一跳地址。

**解决**：把 `make qemu`、`make debug`、`make test` 和 `tools/grade.sh` 中的加载方式改为 `-kernel bin/ucore.img`，由 QEMU 在加载镜像的同时设置 `Next Address=0x80200000`。

### 3. Best-Fit 的合并与拆分边界

**问题**：实现 Best-Fit 时容易漏掉两种情况：块比请求大时需要把剩余部分插入链表；释放时需要同时合并前驱和后继。

**解决**：拆分时先保存 `list_prev(&page->page_link)`，删除原块后把剩余块插到该位置，保证链表仍按地址有序；释放时先按地址插入，再分别检查 `prev` 和 `next`，并在合并后更新 `base` 指针和 `property`。

### 4. Buddy 初始空闲区不是 2 的幂

**问题**：`page_init()` 交给分配器的空闲区起点和长度都不一定是 2 的幂，直接把整段当成一个块会破坏伙伴系统的对齐要求。

**解决**：`buddy_init_memmap()` 从区间起点开始，每次选择“不超过剩余长度且起始页号按大小对齐”的最大 2 的幂块，循环分解，直到剩余长度为 0。

## 七、重要知识点与 OS 原理的对应

| 本实验知识点 | OS 原理知识点 | 含义、关系与差异 |
| --- | --- | --- |
| DTB 物理内存探测 | 硬件资源发现 / 引导协议 | 实验通过 OpenSBI 传递 DTB；原理中还包括 UEFI Memory Map、ACPI 等，目标都是让内核知道哪些物理地址可用 |
| `struct Page` 描述符数组 | 页框管理 / 物理页框号 | 实验用连续数组描述每个物理页；原理中还可以用位图、页框数据库等结构，核心都是记录页框状态和引用计数 |
| 空闲链表 + First-Fit | 连续内存分配 / 外部碎片 | 实验按块管理空闲页，首次适配容易实现但可能留下碎片；原理中还有最佳适配、最坏适配、伙伴系统等 |
| Best-Fit | 动态分区分配 | 实验在页级实现“选择最小足够块”的策略；原理中 Best-Fit 常用于可变分区，二者都面临查找开销和碎片问题 |
| Buddy System | 伙伴系统 / 内部碎片 | 实验按 2 的幂拆分合并；原理中伙伴系统常用于内核页分配和 DMA，优点是合并快，缺点是有内部碎片 |
| `va_pa_offset`、`PADDR` | 地址转换 / 直接映射 | 实验的内核空间采用固定偏移的直接映射；原理中虚拟内存还需要多级页表和 TLB，二者处于不同层次 |
| Sv39 启动页表 | 分页机制 / 多级页表 | 实验只建立了一个简单的 1 GiB 大页映射；原理中需要完整的虚拟地址空间管理、按需分页和页表切换 |

## 八、OS 原理中重要但本实验未覆盖的知识点

1. **按需分页与缺页异常处理**：本实验只做物理页分配，没有实现虚拟页到物理页的动态建立；
2. **页面置换算法**：如 FIFO、LRU、Clock 等，本实验未涉及内存不足时的回收；
3. **虚拟内存区域管理**：如 `mm_struct`、VMA、用户地址空间布局；
4. **多级页表的完整管理**：本实验只建立启动时的大页映射，没有实现页表的动态分配、复制和释放；
5. **TLB 刷新与地址空间切换**：只使用了 `sfence.vma`，未讨论 ASID、TLB shootdown；
6. **并发与锁**：物理页分配器在单核启动阶段运行，未考虑多核竞争；
7. **SLUB/SLAB 小对象分配**：属于扩展任务，本实验未实现；
8. **内存碎片度量与回收策略**：未定量分析外部碎片和内部碎片。
