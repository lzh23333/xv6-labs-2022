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
void initrange(uint id, void *pa_start, void *pa_end);
int stealpage(uint cpu);
void checkpa(void *p);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct mem {
  struct spinlock lock;
  struct run *freelist;
  uint pagenum;
};

struct mem kmem[NCPU];

void
kinit()
{
  char buf[10];
  for (int i = 0; i < NCPU; i++) {
    snprintf(buf, 10, "kmem%d", i);
    initlock(&kmem[i].lock, buf);
    kmem[i].pagenum = 0;
  }
  // initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p_start;
  p_start = (char*)PGROUNDUP((uint64)pa_start);
  uint64 totalpages = (uint64)(pa_end - (void *)pa_start) / PGSIZE;
  // get offset
  uint64 offset = totalpages / NCPU;
  for (uint i = 0; i < NCPU; i++) {
    initrange(i, p_start + i*offset*PGSIZE, p_start + (i+1)*offset*PGSIZE);
  }
  p_start += NCPU * offset * PGSIZE;
  if ((char *)pa_end > p_start && (char *)pa_end >= p_start + PGSIZE) {
    initrange(NCPU-1, p_start, pa_end);
  }
}

void initrange(uint id, void *pa_start, void *pa_end) {
  if (pa_start >= pa_end) 
    panic("pa_start >= pa_end");
  struct run *r;
  for (void *p = pa_start; p < pa_end; p += PGSIZE) {
    memset(p, 1, PGSIZE);
  }
  acquire(&kmem[id].lock);
  for (void *p = pa_start; p < pa_end; p += PGSIZE) {
    checkpa(p);
    r = (struct run *)p;
    r->next = kmem[id].freelist;
    kmem[id].freelist = r;
    kmem[id].pagenum++;
  }
  release(&kmem[id].lock);
  
}

void checkpa(void *pa) {
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP) {
    printf("invalid pa = %p\n", pa);
    panic("checkpa");
  }
    
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  checkpa(pa);
  
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();
  uint cpu = cpuid();
  acquire(&kmem[cpu].lock);
  r->next = kmem[cpu].freelist;
  kmem[cpu].freelist = r;
  kmem[cpu].pagenum++;
  release(&kmem[cpu].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  push_off();
  uint cpu = cpuid();
  acquire(&kmem[cpu].lock);
  if (kmem[cpu].pagenum == 0) {
    // need other pages
    release(&kmem[cpu].lock);
    stealpage(cpu);
    acquire(&kmem[cpu].lock);
  }
  r = kmem[cpu].freelist;
  if(r) {
    kmem[cpu].freelist = r->next;
    kmem[cpu].pagenum--;
  }
    
  release(&kmem[cpu].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  pop_off();
  return (void*)r;
}

int stealpage(uint cpu) {
  for (int i = 0; i < NCPU; i++) {
    acquire(&kmem[i].lock);
  }
  // realloc pages to cpu's freelist
  uint argmax = 0, maxpages = 0;
  for (int i = 0; i < NCPU; i++) {
    if (i == cpu) continue;
    if (kmem[i].pagenum > maxpages) {
      argmax = i;
      maxpages = kmem[i].pagenum;
    }
  }
  if (maxpages != 0) {
    uint stealpages = maxpages / 2;
    if (stealpages == 0) stealpages = maxpages;
    kmem[cpu].freelist = kmem[argmax].freelist;
    struct run *r = kmem[argmax].freelist;
    for (int i = 0; i < stealpages-1; i++) {
      r = r->next;
    }
    kmem[argmax].freelist = r->next;
    r->next = 0;
    kmem[argmax].pagenum -= stealpages;
    kmem[cpu].pagenum = stealpages;
  }
  

  for (int i = NCPU-1; i >= 0; i--) {
    release(&kmem[i].lock);
  }
  return maxpages;
}
