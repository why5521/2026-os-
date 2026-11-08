#include <mmu.h>
#include <memlayout.h>

    .section .text,"ax",%progbits
    .globl kern_entry
kern_entry:
    # a0: hartid
    # a1: dtb physical address
    # save hartid and dtb address
    la t0, boot_hartid
    sd a0, 0(t0)
    la t0, boot_dtb
    sd a1, 0(t0)
    # t0 := 三级页表的虚拟地址
    lui     t0, %hi(boot_page_table_sv39)
    # t1 := 0xffffffff40000000 即虚实映射偏移量
    li      t1, 0xffffffffc0000000 - 0x80000000
    # t0 减去虚实映射偏移量 0xffffffff40000000，变为三级页表的物理地址
    sub     t0, t0, t1
    # t0 >>= 12，变为三级页表的物理页号
    srli    t0, t0, 12
    # t1 := 8 << 60，设置 satp 的 MODE 字段为 Sv39
    li      t1, 8 << 60
    # 将刚才计算出的预设三级页表物理页号附加到 satp 中
    or      t0, t0, t1
    csrw    satp, t0
    sfence.vma
    lui sp, %hi(bootstacktop)
    lui t1, %hi(bootstacktop)
    addi t1, t1, %lo(bootstacktop)
    mv sp, t1
    lui t0, %hi(kern_init)
    addi t0, t0, %lo(kern_init)
    jr t0

.section .data
    .align PGSHIFT
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:
    .align PGSHIFT
    .global boot_page_table_sv39
boot_page_table_sv39:
    # Keep the physical 0x80000000-0xbfffffff window identity-mapped so
    # instructions following the satp write remain executable until the
    # explicit jump to the kernel's high virtual address.
    .zero 8 * 2
    .quad (0x80000 << 10) | 0xcf
    .zero 8 * 508
    .quad (0x80000 << 10) | 0xcf
    .global boot_hartid
boot_hartid:
    .quad 0
    .global boot_dtb
boot_dtb:
    .quad 0
