// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  struct run *freelist;
} mykmem[NCPU];

void
kinit()
{
  //initlock(&kmem.lock, "kmem");

  initlock(&mykmem[0].lock, "kmem");
  initlock(&mykmem[1].lock, "kmem");
  initlock(&mykmem[2].lock, "kmem");
  initlock(&mykmem[3].lock, "kmem");
  initlock(&mykmem[4].lock, "kmem");
  initlock(&mykmem[5].lock, "kmem");
  initlock(&mykmem[6].lock, "kmem");
  initlock(&mykmem[7].lock, "kmem");


  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  int i = 0;
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    struct run *r;

    if(((uint64)p % PGSIZE) != 0 || (char*)p < end || (uint64)p >= PHYSTOP)
      panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(p, 1, PGSIZE);

    r = (struct run*)p;

    acquire(&mykmem[i].lock);
    r->next = mykmem[i].freelist;
    mykmem[i].freelist = r;
    release(&mykmem[i].lock);

    i = (i+1) % NCPU;
  }
  //  kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  push_off();
  int cpu_id = cpuid();
  pop_off();


  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&mykmem[cpu_id].lock);
  r->next = mykmem[cpu_id].freelist;
  mykmem[cpu_id].freelist = r;
  release(&mykmem[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{

  push_off();
  int cpu_id = cpuid();
  pop_off();

  struct run *r;

  for(int i = 0; i < NCPU; ++i) {
    acquire(&mykmem[cpu_id].lock);
    r = mykmem[cpu_id].freelist;
    if(r)
      mykmem[cpu_id].freelist = r->next;
    release(&mykmem[cpu_id].lock);
    if(r) break;
    cpu_id = (cpu_id + 1) % NCPU;
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  
  return (void*)r;
}
