#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    // 初始化内核页表，将内核中不同的段映射到指定的物理地址
    kvminit();       // create kernel page table
    // 将当前的核的SATP设为内核根页表的物理地址，并启用分页
    kvminithart();   // turn on paging
    // 给进程表中的所有进程分配内核栈
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode cache
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    // 上面的操作只有第一个核需要做，其他的核不需要重复操作
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    // 所有核的内核页表是一样的，中断向量也是一样的
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}
