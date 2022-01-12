#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *pgdir, uint64 addr, struct inode *ip, uint offset, uint sz);

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG+1], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  // namei打开二进制文件的路径
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // 读取elf头
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  // 是否是ELF格式的二进制文件
  if(elf.magic != ELF_MAGIC)
    goto bad;

  // 初始化页表（包括给根页表分配物理地址，给trapframe和trampoline建立映射）
  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // 读取了elf头、初始化了用户页表后，就可以根据elf头中记录的程序头表的偏移量（phoff），
  // 遍历程序头表中的条目
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    // readi从文件中读取对应的程序头表条目
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    // 只有类型为LOAD的节才能装入内存
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    uint64 sz1;
    // 在装入内存之前，我们先要分配足够的空间。调用uvmalloc分配物理页，在页表中建立映射
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    sz = sz1;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    // 将指定路径ip文件的offset处的、大小为filesz的节，加载到页表的vaddr处
    // 先通过walkaddr找到vaddr对应的物理地址，再将文件的中指定大小的内容写入到物理地址处
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
    // 按照memsz分配内存（物理内存和虚拟内存），按照filesz向文件中读取数据，装载入内存
    // 它们之间的空隙用0来填充
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate two pages at the next page boundary.
  // Use the second as the user stack.
  sz = PGROUNDUP(sz);
  uint64 sz1;
  // 代码和数据装入完后，在下一页的界限处再分配两页，使用第二页作为用户栈，第一页作为guardpage
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  sz = sz1;
  // 将作为guardpage的第一页的标志位PTE_U置为0，防止用户访问
  uvmclear(pagetable, sz-2*PGSIZE);
  // sp指向栈的顶部（高地址），xv6的栈是从上向下，从高地址到低地址扩张的
  sp = sz;
  stackbase = sp - PGSIZE;

  // Push argument strings, prepare rest of stack in ustack.
  // 将exec的参数放入栈中
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push the array of argv[] pointers.
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // Commit to the user image.
  // exec调用时要将指定文件的内容装入内存，覆盖原来进程的页表。
  // xv6的实现是创建一个新的页表，将指定文件的内容装入物理内存，建立映射后，
  // 将原来进程的旧的页表释放掉，将进程控制块中的页表指向新的页表
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  // 将原来旧的页表释放掉（释放映射、物理内存、页表页）
  proc_freepagetable(oldpagetable, oldsz);
  
  // lab3
  if(p->pid == 1){
    vmprint(p->pagetable);
  }
  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// Load a program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  if((va % PGSIZE) != 0)
    panic("loadseg: va must be page aligned");

  for(i = 0; i < sz; i += PGSIZE){
    // 返回虚拟地址对应的物理地址
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    // 如果剩下的小于一页就读取剩下的大小，否则一页一页地读取
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    // 调用readi从文件中读取一页的内容到指定的物理地址
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return;
}
