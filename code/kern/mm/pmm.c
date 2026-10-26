#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    pmm_manager = &best_fit_pmm_manager;
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

<<<<<<< HEAD
//lab2 function1 YOUR CODE
=======
//lab2 function1 2412114
>>>>>>> 6aad1394a17efdfc34e9f5188348876a1bd58a5b
/*
 * page_init - 根据设备树给出的物理内存信息建立页描述符数组，
 * 并把内核镜像与描述符数组之外的空闲物理页交给当前 pmm_manager 管理。
 */
static void page_init(void) {
    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    uint64_t mem_end;

    /* 设备树信息无效时终止初始化 */
    if (mem_base == 0 || mem_size == 0) {
        panic("page_init: invalid physical memory information");
    }

    /* 内核虚拟地址与物理地址之间的固定偏移 */
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    /* npage 是可管理物理页号的上界，nbase 是 DRAM 起始页号 */
    mem_end = mem_base + mem_size;
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: physical memory range too small");
    }

    /* 描述符数组紧接在内核镜像 end 之后，并按页对齐 */
    extern char end[];
    uintptr_t end_va = (uintptr_t)end;
    pages = (struct Page *)ROUNDUP(end_va, PGSIZE);

    /* 先把所有描述符都置为保留状态，避免未完成布局的页面被分配 */
    for (size_t i = 0; i < npage - nbase; i++) {
        pages[i].ref = 0;
        pages[i].flags = 0;
        pages[i].property = 0;
        SetPageReserved(&pages[i]);
    }

    /* 输出探测到的物理内存范围 */
    cprintf("physical memory map:\n");
    cprintf("  memory: 0x%016lx, [0x%016lx, 0x%016lx].\n",
            mem_base, mem_base, mem_end - 1);

    /* 计算描述符数组末尾的物理地址，并向上按页对齐得到空闲区起点 */
    uintptr_t pages_pa = PADDR((uintptr_t)pages);
    uintptr_t pages_end_pa =
        pages_pa + (npage - nbase) * sizeof(struct Page);
    uintptr_t free_start_pa = ROUNDUP(pages_end_pa, PGSIZE);
    /* 设备树报告的内存末端向下按页对齐，去掉不完整的尾页 */
    uintptr_t free_end_pa = ROUNDDOWN(mem_end, PGSIZE);

    if (free_start_pa < free_end_pa) {
        struct Page *base = pa2page(free_start_pa);
        size_t n = (free_end_pa - free_start_pa) / PGSIZE;
        init_memmap(base, n);
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
