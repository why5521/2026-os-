# 操作系统实验二：物理内存和页表

## 实验基本信息

| 项目 | 内容 |
|---|---|
| 实验名称 | Lab2：物理内存和页表管理 |
| 小组成员 | 李星泽、魏华阳、张煜坤 |
| 完成日期 | 2026-10-23 |

## 一、实验目的

1. 理解操作系统如何通过设备树（DTB）发现物理内存的起始地址和大小；
2. 理解以页为单位管理物理内存的方法，掌握 `Page` 描述符数组的建立过程；
3. 理解虚拟地址、物理地址和物理页号之间的转换关系，掌握 `PADDR`、`pa2page`、`page2pa` 等接口；
4. 理解 `pmm_manager` 框架，掌握 First-Fit 与 Best-Fit 连续物理内存分配算法；
5. 实现 Buddy System 分配器并设计测试，比较不同页面分配算法的特点。
6. 用gdb调试qemu内部程序，查看虚拟地址如何转化成物理地址

## 二、实验环境

- 操作系统内核：ucore（RISC-V）
- 模拟环境：QEMU，使用 OpenSBI 启动内核，qemu-system-riscv64
- 地址转换与分页机制：Sv39
- 内存信息来源：设备树（DTB）
- 构建与测试：Makefile、`tools/grade.sh`

## 三、实验整体逻辑

本实验围绕“发现物理内存 → 建立页描述符 → 管理空闲页 → 分配/释放页”这一条主线展开。整体调用链如下：

```text
kern_init()
  -> pmm_init()
       -> init_pmm_manager()     选择并初始化物理内存管理器
       -> page_init()            从 DTB 取得内存范围，建立 Page 描述符数组
            -> init_memmap()     把内核与描述符数组之外的完整空闲页交给 pmm_manager
       -> check_alloc_page()     调用 pmm_manager->check() 验证分配/释放正确性
```

### 项目组成与执行流程

- `kern/driver/dtb.c`：解析设备树，获取物理内存的起始地址和长度；
- `kern/mm/pmm.c`：负责物理内存初始化，并通过统一接口调用当前物理内存管理器；
- `kern/mm/pmm.h`：定义 `struct pmm_manager`、`Page` 相关接口以及地址转换接口；
- `kern/mm/default_pmm.c`：实现 First-Fit 分配器；
- `kern/mm/best_fit_pmm.c`：实现 Best-Fit 分配器；
- `kern/mm/buddy_pmm.c`、`kern/mm/buddy_pmm.h`：实现 Buddy System 分配器；
- `kern/mm/memlayout.h`、`kern/mm/mmu.h`、`kern.ld`：提供内存布局、分页相关定义和链接地址信息。

### 初始化与分配流程

1. `entry.S` 在进入 C 语言之前建立启动页表并开启 Sv39 分页，将内核高地址虚拟空间映射到物理内存；
2. `kern_init()` 清零 BSS 后调用 `dtb_init()`，解析设备树并得到 `memory_base` 和 `memory_size`；
3. `pmm_init()` 通过 `init_pmm_manager()` 选定物理内存管理器；当前配置使用 `best_fit_pmm_manager`；
4. `page_init()` 计算 `npage`、`pages` 和 `va_pa_offset`，先把所有页描述符标记为保留，再把描述符数组之后的完整空闲页交给管理器；
5. `best_fit_init_memmap()` 按物理地址顺序将空闲页块加入空闲链表；
6. 后续的 `alloc_pages()` 和 `free_pages()` 通过 `pmm_manager` 的函数指针完成页面分配、拆分、释放和合并。

### 地址与页描述符的对应关系

```text
内核虚拟地址 end          -> 用 PADDR 转成物理地址
Page 描述符虚拟地址 pages -> 用 PADDR 转成物理地址
物理地址                  -> 通过 PPN / PGSIZE 得到物理页号
物理页号                  -> 通过 pages[ppn - nbase] 得到 Page 描述符
```

`struct pmm_manager` 将具体分配策略封装为一组函数指针，包括 `init`、`init_memmap`、`alloc_pages`、`free_pages`、`nr_free_pages` 和 `check`。`pmm.c` 中对应的公开接口主要负责转发到当前管理器，因此可以在不改变调用者的情况下切换分配算法。

## 四、实验过程与结果

### 1. 物理内存探测与页描述符初始化

`kern_entry` 将 OpenSBI 传入的 DTB 物理地址保存在 `boot_dtb` 中。`dtb_init()` 将该地址加上 `PHYSICAL_MEMORY_OFFSET` 得到虚拟地址，校验魔数 `0xd00dfeed`，再遍历设备树结构，找到 `memory` 节点的 `reg` 属性，获取内存起始地址和长度，并保存到 `memory_base`、`memory_size` 中供 `page_init()` 使用。

`page_init()` 是本实验的重要初始化环节，主要完成以下工作：

- 检查 `get_memory_base()` 和 `get_memory_size()` 的结果，无效时调用 `panic()`；
- 设置 `va_pa_offset = PHYSICAL_MEMORY_OFFSET`，供 `PADDR` 将内核虚拟地址转换成物理地址；
- 根据内存末端计算 `npage = PPN(mem_end)`，根据 DRAM 起始地址得到固定的 `nbase`；
- 令 `pages` 指向内核 `end` 之后按页对齐的虚拟地址，并将 `[nbase, npage)` 对应的 `Page` 描述符先初始化为保留状态；
- 计算描述符数组末尾的物理地址，并向上按页对齐，得到可用空闲区的起点；
- 将内存末端向下按页对齐，排除不完整的尾页；
- 只有空闲区非空时才调用 `init_memmap()`；
- 按要求输出物理内存布局。

空闲区边界的计算关系如下：

```text
pages_end_pa = PADDR(pages) + (npage - nbase) * sizeof(struct Page)
free_start_pa = ROUNDUP(pages_end_pa, PGSIZE)
free_end_pa   = ROUNDDOWN(mem_end, PGSIZE)
```

所有描述符一开始都通过 `SetPageReserved()` 标记为保留状态，因此内核镜像和描述符数组占用的页面不会被直接交给分配器。`init_memmap()` 在建立空闲块时会检查页面状态，并清除纳入空闲区的页面的保留状态。

### 2. First-Fit 分配器

First-Fit 使用一条按物理地址升序排列的空闲块链表。

- `default_init_memmap()`：将页描述符的 `ref`、`flags`、`property` 清零；块首页的 `property` 记录空闲块大小，并设置 `PG_property`；
- `default_alloc_pages()`：从链表头开始查找第一个满足 `property >= n` 的空闲块；如果块大于需求，则拆分剩余部分并将其插回链表；
- `default_free_pages()`：将释放的页面重新标记为空闲块，按地址插入链表，并尝试与前后相邻空闲块合并。

First-Fit 的主要优点是实现简单。其不足在于查找过程可能需要扫描较长的链表，且分配结果依赖空闲块的排列顺序。可以考虑采用 next-fit 指针、按大小分桶、记录最大空闲块或使用更合适的数据结构降低查找开销；多核环境下还需要对空闲链表进行同步保护。

### 3. Best-Fit 分配器

Best-Fit 与 First-Fit 使用相似的数据结构，区别在于分配策略：它会遍历空闲链表，在所有满足 `property >= n` 的空闲块中选择容量最小的块；如果容量相同，则保留地址较小的块。

- `best_fit_init_memmap()`：按物理地址升序插入初始空闲块；
- `best_fit_alloc_pages()`：遍历整条空闲链表，选择满足需求且容量最小的块；块过大时，将剩余部分作为新的空闲块插回链表；
- `best_fit_free_pages()`：按地址顺序插入被释放的块，并分别检查前驱和后继，合并相邻的空闲块；
- `best_fit_pmm_manager`：注册 `init`、`init_memmap`、`alloc_pages`、`free_pages`、`nr_free_pages` 和 `check` 等接口，并在 `init_pmm_manager()` 中选择该管理器。

核心选择逻辑可以概括为：

```text
best = NULL
for each free block p:
    if p->property >= n and (best == NULL or p->property < best->property):
        best = p
```

由于链表按地址升序排列，容量相同的空闲块会优先保留先遍历到的块，因此能够稳定地选择地址较小的块。释放时，先按地址插入，再比较相邻块的地址与大小，满足连续条件时进行合并。

当前实现每次分配都需要遍历链表，时间复杂度为 O(n)。可进一步采用按块大小组织的分离链表、平衡树或分桶结构，降低查找开销；也可以结合块大小与地址设计混合策略，在分配速度和内存碎片之间折中。

### 4. Buddy System 分配器

Buddy System 将空闲内存按 2 的幂大小分块。`order = k` 的块包含 `2^k` 个连续页面，且起始物理页号必须按 `2^k` 对齐。每种 `order` 维护一条空闲链表，块首页的 `property` 记录块大小。

- `buddy_init_memmap()`：初始空闲区的起点和长度不一定是 2 的幂，因此需要将其拆分成多个自然对齐的 2 的幂大小块；
- `buddy_alloc_pages()`：找到能够满足请求的最小可用 `order`，如果块过大则逐层对半拆分，并把后半块加入对应的较低 `order` 空闲链表；
- `buddy_free_pages()`：释放页面后，通过 `buddy_ppn = ppn ^ size` 查找伙伴块；当伙伴仍为空闲块且 `order` 相同时，就合并两个块，并继续尝试向上合并；
- `buddy_check()`：检查链表属性、块对齐、分配、释放和伙伴合并等行为。

初始化时，分配器从空闲区起点开始，每次选择“不超过剩余长度且起始页号满足大小对齐要求”的最大 2 的幂块，直到覆盖整个初始空闲区。这样可以避免把非 2 的幂大小的内存区错误地当成一个完整伙伴块。

测试覆盖以下内容：

- **结构不变量**：链表中的块设置了 `PG_property`，`property` 与对应 `order` 的块大小一致，起始页号满足对齐要求；
- **分配行为**：分配 1、2、4、8 页块，检查返回值非空、块之间不重叠、地址满足对齐约束，并检查分配后的 `PG_property` 状态；
- **释放与合并**：释放后，`nr_free_pages()` 恢复到初始值；
- **再次分配**：释放多个块后再次申请 8 页块，检查伙伴块是否能够成功合并。

这些检查能够发现拆分时未将后半块放回链表、伙伴页号计算错误、`nr_free` 计数不一致、块首页标志位未正确清除等问题。测试通过 `pmm_manager` 接口和 `Page` 字段进行检查，不依赖未公开的内部变量。

当前 `init_pmm_manager()` 仍然默认选择 `best_fit_pmm_manager`，以满足评分脚本对 Best-Fit 的测试要求。需要单独测试 Buddy System 时，可以将管理器指针切换为：

```c
pmm_manager = &buddy_pmm_manager;
```

此时 `page_init()` 会调用 `buddy_init_memmap()`，后续 `check_alloc_page()` 会调用 `buddy_check()`。本次未完成 SLUB 扩展任务，因此不包含该部分的实现和测试。

### 5. 分支任务：gdb调试qemu

按照实验手册的指示，得到调试图片见`images`

## 五、调试问题与解决

### 1. 开启 Sv39 后发生取指缺页

**现象：** 内核在 QEMU 中没有输出，异常日志显示：

```text
cause: 0xc (exec_page_fault), epc: 0x0000000080200038
```

`0x80200038` 是执行 `csrw satp` 后的 `sfence.vma` 所在位置。

**原因：** 启动页表只将高地址 `0xFFFFFFFFC0000000` 开始的 1 GiB 映射到物理内存，没有映射当前正在执行的 `0x80200000` 附近的物理地址。写入 `satp` 后，后续指令按新页表取指，因缺少对应映射而发生异常。

**解决：** 在 `boot_page_table_sv39` 中增加恒等映射项，将 `VA=0x80000000` 映射到 `PA=0x80000000`，使切换页表后的指令仍能正常取指；之后再通过 `jr` 跳到高地址虚拟空间，由原有映射继续执行。该修改不影响 `page_init()` 和评分测试。

### 2. `-device loader` 未设置入口地址

**现象：** 使用原 Makefile 中的 `-device loader,file=bin/ucore.img,addr=0x80200000` 时，OpenSBI 输出 `Domain0 Next Address: 0x0`，内核未开始执行。

**原因：** 当前使用的 QEMU `loader` 设备只负责将文件写入内存，不会像预期那样同时设置下一跳地址。

**解决：** 将 `make qemu`、`make debug`、`make test` 和 `tools/grade.sh` 中的加载方式改为 `-kernel bin/ucore.img`，由 QEMU 加载镜像并设置 `Next Address=0x80200000`。

### 3. Best-Fit 拆分和合并边界

**问题：** 分配时，如果空闲块大于请求大小，需要将剩余部分重新插入链表；释放时，则需要同时检查前驱和后继块是否相邻。

**解决：** 拆分时先保存 `list_prev(&page->page_link)`，删除原块后将剩余块插入相应位置，维持链表的地址顺序。释放时先按地址插入，再分别检查 `prev` 和 `next`，合并后更新 `base` 指针和 `property`。

### 4. Buddy System 的初始空闲区不是 2 的幂

**问题：** `page_init()` 交给分配器的空闲区起点和长度不一定满足 2 的幂要求，直接将其视为一个伙伴块会破坏对齐约束。

**解决：** `buddy_init_memmap()` 从区间起点开始，每次选取不超过剩余长度且起始页号满足对齐条件的最大 2 的幂块，循环分解直到剩余长度为 0。

## 六、练习与思考

### 1. 操作系统如何获取可用物理内存范围

如果操作系统无法预先知道硬件内存范围，可以采用以下途径：

1. **固件或引导协议传递：** 由 bootloader（如 OpenSBI、UEFI、GRUB）探测内存，再通过 DTB、UEFI Memory Map、Multiboot 信息等结构传递给内核；
2. **设备树或 ACPI：** 从 `memory` 节点或 SRAT 等表中读取内存布局；
3. **主动探测：** 在允许的地址范围内尝试读写并捕获总线异常，以判断哪些地址对应真实 RAM，但必须避免覆盖固件数据和外设区域；
4. **体系结构寄存器或 ROM 表：** 部分平台会提供内存大小寄存器或固件配置表。

本实验采用固件传递与设备树解析的方式：OpenSBI 将 DTB 地址放在寄存器 `a1` 中，内核解析 DTB 得到物理内存范围。

### 2. First-Fit 与 Best-Fit 的特点

First-Fit 从空闲链表头开始，选择第一个满足请求的空闲块，实现简单，但分配结果依赖链表顺序。Best-Fit 遍历空闲块，选择容量最接近请求大小的可用块，可能减少单次分配留下的剩余空间，但需要完整扫描链表。两者都需要在释放时合并相邻空闲块，并可能受到外部碎片影响。

### 3. Buddy System 的特点

Buddy System 将空闲内存划分为 2 的幂大小的块，分配时逐层拆分，释放时查找伙伴并合并。其合并过程较直接，但按 2 的幂分配可能产生内部碎片；初始内存区还必须先分解为满足大小和对齐约束的多个块。

## 七、重要知识点与 OS 原理的对应

| 本实验知识点 | OS 原理知识点 | 含义、关系与差异 |
| --- | --- | --- |
| DTB 物理内存探测 | 硬件资源发现 / 引导协议 | 实验通过 OpenSBI 传递 DTB；原理中还包括 UEFI Memory Map、ACPI 等，目标都是让内核知道哪些物理地址可用。 |
| `struct Page` 描述符数组 | 页框管理 / 物理页框号 | 实验用连续数组描述每个物理页；也可以使用位图、页框数据库等结构记录页框状态和引用计数。 |
| 空闲链表与 First-Fit | 连续内存分配 / 外部碎片 | 实验按块管理空闲页，首次适配容易实现，但可能留下碎片；其他策略还包括 Best-Fit、Worst-Fit 和伙伴系统。 |
| Best-Fit | 动态分区分配 | 实验在页级实现“选择最小足够块”的策略，面临查找开销和碎片问题。 |
| Buddy System | 伙伴系统 / 内部碎片 | 实验按 2 的幂拆分和合并；伙伴系统合并较快，但可能产生内部碎片。 |
| `va_pa_offset`、`PADDR` | 地址转换 / 直接映射 | 实验的内核空间采用固定偏移的直接映射；完整虚拟内存管理还涉及多级页表与 TLB。 |
| Sv39 启动页表 | 分页机制 / 多级页表 | 实验建立了简单的 1 GiB 大页映射；完整系统还需要动态管理页表、按需分页和页表切换。 |

## 八、实验总结

本实验完成了物理内存探测、页描述符数组初始化以及页面分配器的理解和测试。通过 DTB 获取物理内存范围后，`page_init()` 排除内核镜像、描述符数组和不完整尾页，将可用完整页面交给当前的物理内存管理器。First-Fit、Best-Fit 和 Buddy System 分别体现了不同的空闲块选择、拆分和合并策略；自动测试结果显示，页初始化与 Best-Fit 测试均通过，总分为 20/20。

实验也说明，本实验的重点是物理页框管理，还没有实现按需分页与缺页异常处理、页面置换、用户虚拟地址空间管理、完整的多级页表动态管理、TLB 地址空间切换、多核同步、SLUB/SLAB 小对象分配，以及碎片率的定量分析。

## 九、AI 协作开发经验

本实验中使用 AI 辅助理解物理内存初始化、页面分配算法和调试问题，并将建议与实际代码、内核输出及自动测试结果进行核对。提示词和相关交互记录单独保存在 `prompt.md` 中。
