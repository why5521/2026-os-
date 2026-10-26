# Lab 2 Prompts

#### 功能模块一——任务一      **prompt**

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

#### 功能模块二——任务二      **prompt**

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

#### 功能模块二——任务三      **prompt** 1：从零生成并接入

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

#### 功能模块二——任务三      **prompt** 2：设计测试

```text
请为上面的 Buddy System 实现编写 buddy_check()，覆盖：
1. 各 order 链表中块的大小、property 和物理页号对齐；
2. 分配 1/2/4/8 页块，检查非空、互不重叠、地址对齐、PG_property 已清除；
3. 释放后空闲页数恢复到初始值；
4. 释放后能够再次分配一个 8 页块，验证伙伴合并确实发生；
5. 不依赖未公开的内部变量，只通过 pmm_manager 接口和 Page 字段检查。
```
