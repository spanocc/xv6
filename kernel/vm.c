#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S


/*
 * create a direct-map page table for the kernel.
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // uint64 b = *walk(kernel_pagetable, VIRTIO0 ,0);
  // printf("k:%p\n",b);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
// 找到最深层的表项（leaf),没有就申请内存创建,只有leaf表项才有可读，可写，可执行的标志
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  // 不应该使用 kernel_pagetable了，应该使用自己的pagetable,卡我一下午
  pagetable_t pagetable = my_kernel_pagetable();

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
// 只有leaf mapping才有PTE_R|PTE_W|PTE_X的标志
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;  //if(va == VIRTIO0) printf("%p\n",*pte);
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);


}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

// 要向上取整  
  if(PGROUNDUP(newsz) > PLIC) return 0; //虚拟内存  限制不要超过PLIC

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
// 只要求leaf mapping也就是最后一级的表项都是0
// 释放页表所有内存，释放完leaf mapping就结束
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){ 
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){  //只有leaf mapping才可能有PTE_R|PTE_W|PTE_X，到这里说明叶子表项没有被清0
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);  // 标志是1 物理内存也被释放了
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{

  return copyin_new(pagetable, dst, srcva, len);

  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{

  copyinstr_new(pagetable, dst, srcva, max);

  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}


// 递归打印页表
static void recursive_vmprint(pagetable_t pagetable, int depth) {
  if(depth > 3) return;    // 经过实验，页表只有三级
  for(int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];
    if(pte & PTE_V){
      uint64 child = PTE2PA(pte);
      for(int j = 0; j < depth; ++j) {
        if(j > 0) printf(" ");
        printf("..");
      }
      printf("%d: pte %p pa %p\n", i, pte, child);

      recursive_vmprint((pagetable_t)child, depth+1);
    
    } /*else if(pte & PTE_V){
      panic("freewalk: leaf");
    }*/
  }
}
// 一个页表占4096个字节，一个pte表项占8字节，所以有512个表项
void vmprint(pagetable_t pagetable) {
  printf("page table %p\n",pagetable);
  recursive_vmprint(pagetable, 1);
}
// kvmmap的进程内核页表版本
int proc_kvmmap(pagetable_t kp, uint64 va, uint64 pa, uint64 sz, int perm)
{                                            
  if(mappages(kp, va, sz, pa, perm) != 0) return -1;
    //panic("proc_kvmmap");
  else  return 0;
}
/*
pagetable_t copy_kernel_pagetable() {
  return kernel_pagetable;
}
*/

pagetable_t kernel_pagetable_create() {

  pagetable_t new_kernel_pagetable = (pagetable_t) kalloc();

  if(new_kernel_pagetable == 0) {   // printf("noooooooooo\n");
    return 0;
  }
  memset(new_kernel_pagetable, 0, PGSIZE);

  // uart registers
  if(proc_kvmmap(new_kernel_pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W) != 0) return 0;

  // virtio mmio disk interface
  if(proc_kvmmap(new_kernel_pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W)!= 0) return 0;

  // CLINT
  //if(proc_kvmmap(new_kernel_pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W)!= 0) return 0;

  // PLIC
  if(proc_kvmmap(new_kernel_pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W)!= 0) return 0;

  // map kernel text executable and read-only.
  if(proc_kvmmap(new_kernel_pagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X)!= 0) return 0;

  // map kernel data and the physical RAM we'll make use of.
  if(proc_kvmmap(new_kernel_pagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W)!= 0) return 0;

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  if(proc_kvmmap(new_kernel_pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X)!= 0) return 0;

/*
  uint64 a = *walk(new_kernel_pagetable,TRAMPOLINE, 0);
  uint64 b = *walk(kernel_pagetable, TRAMPOLINE ,0);
  uint64 c = TRAMPOLINE;
  c = (c>>12)<<10;
  printf("%p %p %p\n", a, b, c);
  if(a!=b) {
    //printf("%p %p %p", a, b, UART0);
    panic("error!!!!!!!!!!");
  }
*/
  return new_kernel_pagetable;
}

// kvminithart的进程内核栈版本
void proc_kvminithart(pagetable_t proc_kernel_pagetable) {
  w_satp(MAKE_SATP(proc_kernel_pagetable));
  sfence_vma();
}



// 仿照 proc_freepagetable 但不释放物理内存页
void kernel_freepagetable(pagetable_t pagetable) {      //return;
  
  // there are 2^9 = 512 PTEs in a page table.

for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V)){
      pagetable[i] = 0;
      if ((pte & (PTE_R|PTE_W|PTE_X)) == 0)   // 对于非叶子节点就递归
      {
        uint64 child = PTE2PA(pte);
        kernel_freepagetable((pagetable_t)child);

      }

    } /*else if(pte & PTE_V){
        panic("proc free kpt: leaf");
    }*/

 }
 kfree((void*)pagetable);


// 如果这么挨个解除映射的话太费时间，就直接自己写个类似freewalk的函数来释放
/*
  uvmunmap(pagetable, UART0, 1, 0);
  uvmunmap(pagetable, VIRTIO0, 1, 0);
  uvmunmap(pagetable, CLINT, 16, 0);
  uvmunmap(pagetable, PLIC, 1024, 0);
  uvmunmap(pagetable, KERNBASE, ((uint64)etext-KERNBASE) / PGSIZE, 0);
  uvmunmap(pagetable, (uint64)etext, (PHYSTOP-(uint64)etext) / PGSIZE, 0);
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);

  freewalk(pagetable);

*/
}

uint64 kvmalloc(pagetable_t pagetable, pagetable_t k_pagetable, uint64 oldsz, uint64 newsz) {
  uint64 a;
  /*if(newsz < oldsz)
    return -1;*/
  if (PGROUNDUP(newsz) > PLIC)  return -1;
  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    pte_t* pte = walk(pagetable, a, 0);
    if(pte == 0) panic("kvmalloc: walk");
    if(((*pte) & PTE_V) == 0) panic("kvmalloc: page not present");
    uint64 pa = PTE2PA(*pte);
    uint flags = PTE_FLAGS(*pte);
    flags &= (~PTE_U); 
    if(mappages(k_pagetable, a, PGSIZE, pa, flags) != 0) {
      kvmdealloc(k_pagetable, a, oldsz);
      return -1;
      //panic("kvmalloc: mappages");
    }
  }
  return 0;
}

uint64 kvmdealloc(pagetable_t k_pagetable, uint64 oldsz, uint64 newsz) {
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    //pte_t* pte = walk(pagetable, PGROUNDUP(newsz), 0);
    uvmunmap(k_pagetable, PGROUNDUP(newsz), npages, 0);
  }

  return newsz;   
}

//PTE_U
