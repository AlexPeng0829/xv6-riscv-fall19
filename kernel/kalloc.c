// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define REF_COUNT(addr) *(ref_count_start + ((uint8 *)(addr)-ref_count_start)/PGSIZE)

void freerange(void *pa_start, void *pa_end);

// ref_count manages the RAM of 128MB, i.e. 32768 Pages
// since NPROC=64, so the memory needed for ref count = 32768/4096 = 8
void init_ref_count(void *pa_start, uint num);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

static uint8* ref_count_start;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  init_ref_count(end, 8*PGSIZE);
  ref_count_start = (uint8 *)end;
  freerange((void *)(ref_count_start + 8*PGSIZE), (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

void init_ref_count(void *pa_start, uint num)
{
  memset(pa_start, 1, num); // tricky, we need to call kfree in the beginning
}

uint8 get_ref_count(uint8 *addr)
{
  return REF_COUNT(addr);
}

void set_ref_count(uint8 *addr, uint8 count)
{
  REF_COUNT(addr) = count;
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end + 8 * PGSIZE || (uint64)pa >= PHYSTOP)
    panic("kfree");
  // printf("ref_count(%p): %d\n", pa, REF_COUNT(pa));
  REF_COUNT(pa) -= 1;
  if (REF_COUNT(pa) == 0)
  {
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run*)pa;
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  // printf("kalloc called!\n");
  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
  {
    kmem.freelist = r->next;
    REF_COUNT(r) = 1;
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
