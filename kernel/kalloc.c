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

struct kmem {
  struct spinlock lock;
  struct run *freelist;
  int page_num;
};

struct kmem_collection{
  struct kmem item[NCPU];
  struct spinlock lock;
  int cyclic_iter;
} kmems;


void
kinit()
{
  initlock(&kmems.lock, "kmems");
  kmems.cyclic_iter = 0;
  for(int i = 0; i < NCPU; ++i)
  {
    initlock(&kmems.item[i].lock, "kmem");
    kmems.item[i].page_num = 0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
  {
    kfree(p);
  }
}

int
borrow_mem(int cpu_id)
{
  int page_borrowed = 0;
  acquire(&kmems.lock);
  for(int loop_count = 0; loop_count < NCPU; ++loop_count)
  {
    // Skip current cpu's kmem to avoid deadlock
    if(kmems.cyclic_iter == cpu_id)
    {
      kmems.cyclic_iter = (kmems.cyclic_iter + 1) % NCPU;
      continue;
    }

    acquire(&kmems.item[kmems.cyclic_iter].lock);
    if(kmems.item[kmems.cyclic_iter].page_num > 1)
    {
      acquire(&kmems.item[cpu_id].lock);
      page_borrowed = kmems.item[kmems.cyclic_iter].page_num/2;
      for(int i = 0; i < page_borrowed; ++i)
      {
        struct run* r = kmems.item[kmems.cyclic_iter].freelist;
        kmems.item[kmems.cyclic_iter].freelist = r->next;
        kmems.item[kmems.cyclic_iter].page_num--;

        r->next = kmems.item[cpu_id].freelist;
        kmems.item[cpu_id].freelist = r;
        kmems.item[cpu_id].page_num++;
      }
      release(&kmems.item[cpu_id].lock);
      release(&kmems.item[kmems.cyclic_iter].lock);

      kmems.cyclic_iter = (kmems.cyclic_iter + 1) % NCPU;
      release(&kmems.lock);
      return page_borrowed;
    }
    else
    {
      release(&kmems.item[kmems.cyclic_iter].lock);
      kmems.cyclic_iter = (kmems.cyclic_iter + 1) % NCPU;
    }

  }
  release(&kmems.lock);
  return page_borrowed;

}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int cpu_id;

  push_off();
  cpu_id = cpuid();
  pop_off();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmems.item[cpu_id].lock);
  r->next = kmems.item[cpu_id].freelist;
  kmems.item[cpu_id].freelist = r;
  kmems.item[cpu_id].page_num++;
  release(&kmems.item[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int cpu_id;

  push_off();
  cpu_id = cpuid();
  pop_off();

  acquire(&kmems.item[cpu_id].lock);
  r = kmems.item[cpu_id].freelist;
  if(r)
  {
    kmems.item[cpu_id].freelist = r->next;
    kmems.item[cpu_id].page_num--;
    release(&kmems.item[cpu_id].lock);
  }
  else
  {
    release(&kmems.item[cpu_id].lock);
    if(borrow_mem(cpu_id))
    {
      acquire(&kmems.item[cpu_id].lock);
      r = kmems.item[cpu_id].freelist;
      kmems.item[cpu_id].freelist = r->next;
      kmems.item[cpu_id].page_num--;
      release(&kmems.item[cpu_id].lock);
    }
  }
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}