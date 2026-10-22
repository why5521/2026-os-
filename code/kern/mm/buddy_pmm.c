#include <pmm.h>
#include <list.h>
#include <buddy_pmm.h>
#include <stdio.h>

/*
 * Buddy System 物理内存管理器（lab2 任务三）。
 *
 * 所有空闲块的大小都是 2 的幂（以页为单位），order 为 k 的块包含
 * 2^k 个连续物理页，并且块起始物理页号按 2^k 对齐。每个 order 维护
 * 一条空闲链表，块首页的 property 记录块大小（页数）。
 *
 * 说明：本模块保留在工程中并参与编译，当前内核启用的仍是 Best-Fit
 * 管理器；若需要单独验证，把 init_pmm_manager() 中的 best_fit_pmm_manager
 * 换成 buddy_pmm_manager 即可（pmm_init -> page_init 会调用本模块的
 * init_memmap，随后 check_alloc_page() 会调用 buddy_check()）。
 */

#define MAX_ORDER 16

static free_area_t buddy_free_area[MAX_ORDER + 1];
static size_t buddy_nr_free = 0;

static inline size_t
buddy_block_size(int order) {
    return 1UL << order;
}

/* 返回 >= n 的最小 2 的幂对应的 order */
static int
buddy_order_of(size_t n) {
    int order = 0;
    while (buddy_block_size(order) < n) {
        order++;
    }
    return order;
}

static void
buddy_add_free_block(struct Page *base, int order) {
    list_add_before(&(buddy_free_area[order].free_list), &(base->page_link));
}

static void
buddy_init(void) {
    for (int order = 0; order <= MAX_ORDER; order++) {
        list_init(&(buddy_free_area[order].free_list));
        buddy_free_area[order].nr_free = 0;
    }
    buddy_nr_free = 0;
}

/*
 * 把 [base, base+n) 划分成若干自然对齐的 2 的幂大小块并加入空闲链表。
 * 空闲区起点和长度都不一定是 2 的幂，因此不能只把整段当成一个块。
 */
static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    struct Page *p = base;
    for (; p != base + n; p++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    buddy_nr_free += n;

    p = base;
    size_t remaining = n;
    while (remaining > 0) {
        size_t ppn = page2ppn(p);
        int order = MAX_ORDER;
        while (order > 0) {
            size_t size = buddy_block_size(order);
            if (size <= remaining && (ppn & (size - 1)) == 0) {
                break;
            }
            order--;
        }
        size_t size = buddy_block_size(order);
        p->property = (unsigned int)size;
        SetPageProperty(p);
        buddy_add_free_block(p, order);
        p += size;
        remaining -= size;
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > buddy_nr_free) {
        return NULL;
    }

    /* 找到最小的、有可用块的 order */
    int req_order = buddy_order_of(n);
    int order = req_order;
    while (order <= MAX_ORDER &&
           list_empty(&(buddy_free_area[order].free_list))) {
        order++;
    }
    if (order > MAX_ORDER) {
        return NULL;
    }

    list_entry_t *le = list_next(&(buddy_free_area[order].free_list));
    struct Page *block = le2page(le, page_link);
    list_del(&(block->page_link));

    /* 从高 order 逐层对半拆分，后半块成为新的空闲块 */
    while (order > req_order) {
        order--;
        size_t size = buddy_block_size(order);
        struct Page *buddy = block + size;
        buddy->property = (unsigned int)size;
        SetPageProperty(buddy);
        buddy_add_free_block(buddy, order);
    }

    block->property = 0;
    ClearPageProperty(block);
    buddy_nr_free -= buddy_block_size(req_order);
    return block;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);

    int order = buddy_order_of(n);
    size_t size = buddy_block_size(order);
    assert(size == n);

    struct Page *p = base;
    for (; p != base + size; p++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }

    base->property = (unsigned int)size;
    SetPageProperty(base);
    buddy_nr_free += size;

    /* 只要伙伴块空闲且同 order，就不断向上合并 */
    while (order < MAX_ORDER) {
        size_t ppn = page2ppn(base);
        size_t buddy_ppn = ppn ^ buddy_block_size(order);
        if (buddy_ppn < nbase || buddy_ppn >= npage) {
            break;
        }
        struct Page *buddy = &pages[buddy_ppn - nbase];
        if (!PageProperty(buddy) ||
            buddy->property != buddy_block_size(order)) {
            break;
        }

        list_del(&(buddy->page_link));
        ClearPageProperty(buddy);

        if (buddy < base) {
            ClearPageProperty(base);
            base = buddy;
        }
        order++;
        base->property = (unsigned int)buddy_block_size(order);
        SetPageProperty(base);
    }

    buddy_add_free_block(base, order);
}

static size_t
buddy_nr_free_pages(void) {
    return buddy_nr_free;
}

void
buddy_check(void) {
    size_t initial_free = buddy_nr_free;

    /* 1. 检查各 order 链表中块的属性与对齐 */
    for (int order = 0; order <= MAX_ORDER; order++) {
        list_entry_t *le = &(buddy_free_area[order].free_list);
        while ((le = list_next(le)) != &(buddy_free_area[order].free_list)) {
            struct Page *p = le2page(le, page_link);
            assert(PageProperty(p));
            assert(p->property == buddy_block_size(order));
            assert((page2ppn(p) & (buddy_block_size(order) - 1)) == 0);
        }
    }

    /* 2. 分配不同大小的块，检查返回地址对齐且不再标记为空闲块首页 */
    struct Page *p1 = alloc_pages(1);
    struct Page *p2 = alloc_pages(2);
    struct Page *p4 = alloc_pages(4);
    struct Page *p8 = alloc_pages(8);
    assert(p1 != NULL && p2 != NULL && p4 != NULL && p8 != NULL);
    assert(p1 != p2 && p1 != p4 && p1 != p8);
    assert(p2 != p4 && p2 != p8 && p4 != p8);
    assert(!PageProperty(p1) && !PageProperty(p2));
    assert(!PageProperty(p4) && !PageProperty(p8));
    assert((page2ppn(p2) & 1) == 0);
    assert((page2ppn(p4) & 3) == 0);
    assert((page2ppn(p8) & 7) == 0);

    /* 3. 释放后应与伙伴合并，空闲页数恢复 */
    free_pages(p1, 1);
    free_pages(p2, 2);
    free_pages(p4, 4);
    free_pages(p8, 8);
    assert(buddy_nr_free == initial_free);

    /* 4. 合并后应能再次分配一个 8 页块 */
    struct Page *q = alloc_pages(8);
    assert(q != NULL);
    assert(!PageProperty(q));
    free_pages(q, 8);
    assert(buddy_nr_free == initial_free);

    cprintf("buddy_check() succeeded!\n");
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
