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

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
} kmem;

// physical page reference counter
#define PAGENUM (PHYSTOP - KERNBASE) / PGSIZE
// map physical address to count idx
#define PA2IDX(pa) ((((uint64)pa) - KERNBASE) / PGSIZE)
struct
{
  struct spinlock lock;
  uint count[PAGENUM];
} refcounter;



// increase ref counter by 1
// if incr != 0, increse
// else decrese
int incr_count(uint64 pa) {
  int idx = PA2IDX(pa);
  acquire(&refcounter.lock);
  refcounter.count[idx] ++;
  release(&refcounter.lock);
  return refcounter.count[idx];
}

int decr_count(uint64 pa) {
  int idx = PA2IDX(pa);
  int res = 0;
  acquire(&refcounter.lock);
  if (refcounter.count[idx] > 0) res = --refcounter.count[idx];
  else res = -1;
  release(&refcounter.lock);
  return res;
}

int get_count(uint64 pa) {
  return refcounter.count[PA2IDX(pa)];
}

void kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refcounter.lock, "refcounter");

  acquire(&refcounter.lock);
  for (int i = 0; i < PAGENUM; i++) {
    refcounter.count[i] = 1;
  }
  release(&refcounter.lock);
  freerange(end, (void *)PHYSTOP);
  
  // No need to init global variable to zero
  // memset(refcounter.count, 0, sizeof(refcounter.count));
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
  
  // decr count
  int count = decr_count((uint64)pa);
  
  // return if there are other processes using pa
  if (count > 0) return;
  else if (count < 0) {
    printf("decr_count apply to a zero count");
    panic("kfree");
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) {
    kmem.freelist = r->next;
    incr_count((uint64)r);
    if (get_count((uint64)r) != 1) {
      printf("pa = %x, counter = %d != 1\n", (uint64)r, get_count((uint64)r));
      //panic("kalloc ref counter > 1");
    }
  }
  release(&kmem.lock);

  if (r) {
    memset((char *)r, 5, PGSIZE); // fill with junk
    // init r's ref counter
  }
    
  return (void *)r;
}

