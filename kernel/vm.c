#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

void print(pagetable_t);

/*
 * create a direct-map page table for the kernel and
 * turn on paging. called early, in supervisor mode.
 * the page allocator is already initialized.
 */
void kvminit()
{
    kernel_pagetable = (pagetable_t)kalloc();
    memset(kernel_pagetable, 0, PGSIZE);

    // uart registers
    kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

    // virtio mmio disk interface 0
    kvmmap(VIRTION(0), VIRTION(0), PGSIZE, PTE_R | PTE_W);

    // virtio mmio disk interface 1
    kvmmap(VIRTION(1), VIRTION(1), PGSIZE, PTE_R | PTE_W);

    // CLINT
    kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

    // PLIC
    kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

    // map kernel text executable and read-only.
    kvmmap(KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

    // map kernel data and the physical RAM we'll make use of.
    kvmmap((uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

    // map the trampoline for trap entry/exit to
    // the highest virtual address in the kernel.
    kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart()
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
//   21..39 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..12 -- 12 bits of byte offset within the page.
static pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
    if (va >= MAXVA)
        panic("walk");

    for (int level = 2; level > 0; level--)
    {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V)
        {
            pagetable = (pagetable_t)PTE2PA(*pte);
        }
        else
        {
            if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }
    return &pagetable[PX(0, va)];
}

void vmprint_one_level(pagetable_t pagetable, int level)
{
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = pagetable[i];
        if (pte & PTE_V)
        {
            uint64 child = PTE2PA(pte);
            for (int j = 0; j <= level; j++)
            {
                printf(" ..");
            }
            // printf("%d: pte %p pa %p\n", i, pte, child);
            printf("%d: pte %p pa %p [ref:%d] [flag:%b]\n", i, pte, child, get_ref_count((uint8 *)child), PTE_FLAGS(pte));
            if (level < 2)
            {
                vmprint_one_level((pagetable_t)child, level + 1);
            }
        }
    }
}

// Print the page table
void vmprint(pagetable_t pagetable)
{
    printf("page table %p\n", pagetable);
    vmprint_one_level(pagetable, 0);
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped. -1 to indicate invalid memory access attemp (below the heap)
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;
    uint64 pa;

    // va too big
    if (va >= MAXVA)
        return 0;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
    {
        return 0;
    }
    if ((*pte & PTE_V) == 0)
    {
        return 0;
    }
    pa = PTE2PA(*pte);
    return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
    if (mappages(kernel_pagetable, va, sz, pa, perm) != 0)
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

    pte = walk(kernel_pagetable, va, 0);
    if (pte == 0)
        panic("kvmpa");
    if ((*pte & PTE_V) == 0)
        panic("kvmpa");
    pa = PTE2PA(*pte);
    return pa + off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
    uint64 a, last;
    pte_t *pte;

    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);
    for (;;)
    {
        if ((pte = walk(pagetable, a, 1)) == 0)
            return -1;
        if (*pte & PTE_V)
        {
            if (!(*pte & PTE_U)) // offending process trying to access va below stack
            {
                myproc()->killed = 1;
                exit(-1);
            }
            else
            {
                panic("remap");
            }
        }
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

// Remove mappings from a page table. The mappings in
// the given range must exist. Optionally free the
// physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 size, int do_free)
{
    uint64 a, last;
    pte_t *pte;
    uint64 pa;

    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);
    for (;;)
    {
        if ((pte = walk(pagetable, a, 0)) == 0)
        {
            if (a == last)
                break;
            a += PGSIZE;
            pa += PGSIZE;
            continue;
        }
        if ((*pte & PTE_V) == 0)
        {
            *pte = 0;
            if (a == last)
                break;
            a += PGSIZE;
            pa += PGSIZE;
            continue;
        }
        // if (PTE_FLAGS(*pte) == PTE_V)
        //     panic("uvmunmap: not a leaf");
        if (do_free)
        {
            pa = PTE2PA(*pte);
            kfree((void *)pa);
        }
        *pte = 0;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
}

// create an empty user page table.
pagetable_t
uvmcreate()
{
    pagetable_t pagetable;
    pagetable = (pagetable_t)kalloc();
    if (pagetable == 0)
        panic("uvmcreate: out of memory");
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
    char *mem;

    if (sz >= PGSIZE)
        panic("inituvm: more than a page");
    mem = kalloc();
    memset(mem, 0, PGSIZE);
    mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
    memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
    char *mem;
    uint64 a;

    if (newsz < oldsz)
        return oldsz;

    oldsz = PGROUNDUP(oldsz);
    a = oldsz;
    for (; a < newsz; a += PGSIZE)
    {
        mem = kalloc();
        if (mem == 0)
        {
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0)
        {
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
    if (newsz >= oldsz)
        return oldsz;

    uint64 newup = PGROUNDUP(newsz);
    if (newup < PGROUNDUP(oldsz))
    {
        printf("in uvmdealloc\n");
        uvmunmap(pagetable, newup, oldsz - newup, 1);
    }

    return newsz;
}

// free the page-table's entry since va
void free_pagetable(pagetable_t pagetable, uint64 va)
{
    pagetable_t pagetable_entry_start[3];
    int rm_parent_link = 0;

    if (va >= MAXVA)
        panic("free_pagetable");
    pagetable_entry_start[2] = pagetable;
    for (int level = 2; level > 0; level--)
    {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V)
        {
            pagetable = (pagetable_t)PTE2PA(*pte);
            pagetable_entry_start[level - 1] = pagetable;
        }
        else
        {
            //TODO: should return or not ?
            return;
        }
    }

    for (int i = 0; i < 512; i++)
    {
        if (i == PX(0, va))
        {
            memset(pagetable_entry_start[0] + i, 0, sizeof(pagetable_t) / sizeof(int) * (512 - i));
            if (i == 0)
            {
                rm_parent_link = 1;
            }
            break;
        }
    }

    for (int level = 1; level < 3; level++)
    {
        int idx_end = 512;
        if (level == 2)
        {
            idx_end = PX(2, TRAPFRAME);
        }
        for (int i = 0; i < 512; i++)
        {
            if (i == PX(level, va))
            {
                if (rm_parent_link) // remove starts from current entry
                {
                    memset(pagetable_entry_start[level] + i, 0, sizeof(pagetable_t) * (idx_end - i));
                }
                else // remove starts from next entry
                {
                    if (i < 511)
                    {
                        memset(pagetable_entry_start[level] + i + 1, 0, sizeof(pagetable_t) * (idx_end - i - 1));
                    }
                }
                if (i == 0 && rm_parent_link)
                {
                    rm_parent_link = 1;
                }
                else
                {
                    rm_parent_link = 0;
                }
                break;
            }
        }
    }

    return;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
static void
freewalk(pagetable_t pagetable)
{
    // there are 2^9 = 512 PTEs in a page table.
    for (int i = 0; i < 512; i++)
    {
        pte_t pte = pagetable[i];
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
        {
            // this PTE points to a lower-level page table.
            uint64 child = PTE2PA(pte);
            freewalk((pagetable_t)child);
            pagetable[i] = 0;
        }
        else if (pte & PTE_V)
        {
            // printf("pte: %d, pte addr: %p\n", pte, &pagetable[i]);
            // panic("freewalk: leaf");
        }
    }
    kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz)
{
    uvmunmap(pagetable, 0, sz, 1);
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
  uint8 count_old;
  uint perm;
  uint64 pte_w_clear;


  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0){
    //   panic("uvmcopy: pte should exist");
        continue;
    }
    if((*pte & PTE_V) == 0){
    //   panic("uvmcopy: page not present");
        continue;
    }
    pa = PTE2PA(*pte);
    pte_w_clear = ~0 - PTE_W;
    perm = PTE_FLAGS(*pte);
    perm |= PTE_COW;         // set COW bit
    perm &= pte_w_clear;     // clear the PTE_W
    *pte = ((*pte >> 8) << 8) | perm; // change the parent process's permission bits

    count_old = get_ref_count((uint8 *)pa);
    set_ref_count((uint8 *)pa, count_old +1); // increment the ref count for each page
    if(mappages(new, i, PGSIZE, (uint64)pa, perm) != 0)
    {
      return -1;
    }
    }
    return 0;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        panic("uvmclear");
    *pte &= ~PTE_U;
}

// Map the PTE to newly allocated physical pages, set the PTE_W and clear PTE_COW
// @return 0 on success -1 if no valid pte is found
int map_cow_page(pagetable_t page_table, uint64 va, uint64 pa)
{
  pte_t *pte;
  uint pte_cow_clear = ~0 - PTE_COW;
  uint perm;

  if ((pte = walk(page_table, va, 0)) == 0)
    return -1;
  if (!(*pte & PTE_V)){
    return -1;
  }
  perm = PTE_FLAGS(*pte) | PTE_W;     // set the PTE_W for newly allocated page
  perm = perm & pte_cow_clear;             // remove COW bit for newly allocated page
  *pte = PA2PTE(pa) | perm | PTE_V;
  return 0;
}

/**
 * @brief handle copy-on-write page, allocate page for the page faulted and clear the
 * copy-on-write bit for it.
 * @param p process that has the copy-on-write page
 * @param va_faulted page copy-on-write bit of which is set
 * @param pte pte for the faulted page
 * @return int 0 on success -1 on failure
 */
int handle_cow_page(struct proc* p, uint64 va_faulted, pte_t *pte)
{
    uint8 ref_count;
    uint64 pa = PTE2PA(*pte);
    ref_count = get_ref_count((uint8 *)pa);
    // only one process is using this page, clear COW bit and restore W bit
    if(ref_count == 1)
    {
      uint perm = PTE_FLAGS(*pte);
      uint pte_cow_clear = ~0 - PTE_COW;
      perm |= PTE_W;          // restore the PTE_W
      perm &= pte_cow_clear;  // clear the PTE_COW
      *pte = PA2PTE(pa) | perm | PTE_V;
    }
    else
    {
      char *mem;
      if ((mem = kalloc()) == 0)
      {
        printf("handle_cow_page(): failed to allocate more physical memory for copy-on-write page!\n");
        return -1;
      }
      set_ref_count((uint8 *)pa, ref_count - 1);
      memmove(mem, (char *)pa, PGSIZE);
      if (map_cow_page(p->pagetable, va_faulted, (uint64)mem) != 0)
      {
          kfree(mem);
          printf("handle_cow_page(): failed to map pages for copy-on-write page!\n");
          return -1;
      }
    }
    return 0;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
    uint64 n, va0, pa0;
    while (len > 0)
    {
        if (dstva > TRAPFRAME)
        {
            return -1;
        }
        va0 = PGROUNDDOWN(dstva);
        pte_t *pte;
        if ((pte = walk(pagetable, dstva, 0)) != 0)
        {
            if ((*pte & PTE_COW) && (*pte & PTE_V))
            {
                if(handle_cow_page(myproc(), va0, pte) != 0)
                {
                    return -1;
                }
            }

        }
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == -1)
        {
            return - 1;
        }
        if (pa0 == 0)
        {
            if(handle_lazy_allocation(myproc(), va0) != 0)
            {
                return -1;
            }
            pa0 = walkaddr(pagetable, va0);
        }
        n = PGSIZE - (dstva - va0);
        if (n > len)
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
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
    uint64 n, va0, pa0;

    while (len > 0)
    {
        if (srcva > myproc()->sz)
        {
            return -1;
        }
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
        {
            handle_lazy_allocation(myproc(), va0);
            pa0 = walkaddr(pagetable, va0);
        }
        n = PGSIZE - (srcva - va0);
        if (n > len)
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
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
    uint64 n, va0, pa0;
    int got_null = 0;

    while (got_null == 0 && max > 0)
    {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (srcva - va0);
        if (n > max)
            n = max;

        char *p = (char *)(pa0 + (srcva - va0));
        while (n > 0)
        {
            if (*p == '\0')
            {
                *dst = '\0';
                got_null = 1;
                break;
            }
            else
            {
                *dst = *p;
            }
            --n;
            --max;
            p++;
            dst++;
        }

        srcva = va0 + PGSIZE;
    }
    if (got_null)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

/**
 * @brief handle store fault, which would be caused by both copy-on-write and
 * lazy allocation
 * @param p process that has store fault
 * @param va_faulted page that's store faulted
 * @return int 0 on success -1 on failure
 */
int handle_store_fault(struct proc* p, uint64 va_faulted)
{
    pte_t *pte;
    if(va_faulted > MAXVA)
    {
      return -1;
    }
    if ((pte = walk(p->pagetable, va_faulted, 0)) == 0)
    {
        return handle_lazy_allocation(p, va_faulted);
    }
    if ((*pte & PTE_V) == 0)
    {
        return handle_lazy_allocation(p, va_faulted);
    }
    if (*pte & PTE_COW)
    {
        return handle_cow_page(p, va_faulted, pte);
    }
    return -1;
}

/**
 * @brief Handle page fault caused by lazy allocation, used in both usertrap and some other syscall functions
 *
 * @param p process has page fault
 * @param va_faulted page that's load/store faulted
 * @return int 0 on success -1 on failure
 */
int handle_lazy_allocation(struct proc *p, uint64 va_faulted)
{
    uint64 va_page;
    char *mem;
    if (va_faulted > p->sz)
    {
        printf("Invalid memory access, try to access memory: %p higher than proc->sz:%p\n", va_faulted, p->sz);
        return -1;
    }

    va_page = PGROUNDDOWN(va_faulted);
    mem = kalloc();
    if (mem == 0)
    {
        printf("Running out of physical memory!\n");
        return -1;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(p->pagetable, va_page, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0)
    {
        kfree(mem);
        printf("mapages failed!\n");
        return -1;
    }
    return 0;
}