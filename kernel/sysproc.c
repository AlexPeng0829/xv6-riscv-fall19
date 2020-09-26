#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if (argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if (argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr_old;
  int n;
  if (argint(0, &n) < 0)
    return -1;
  addr_old = myproc()->sz;
  if (n < 0)
  {
    int addr_free_start = PGROUNDUP(addr_old + n);
    int addr_freed;
    // printf("---------------------before free_pagetable---------------------\n");
    // vmprint(myproc()->pagetable);

    // after round up if addr_free_start goes to another page, skip
    if (addr_free_start < addr_old)
    {
      addr_freed = addr_old - addr_free_start;

      uvmunmap(myproc()->pagetable, addr_free_start, addr_freed, 1);
      free_pagetable(myproc()->pagetable, addr_free_start);

      // printf("---------------------after free_pagetable---------------------\n");
      // vmprint(myproc()->pagetable);
    }
  }
  myproc()->sz += n;
  return addr_old;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (myproc()->killed)
    {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
