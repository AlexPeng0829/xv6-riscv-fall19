//
// network system calls.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "net.h"

struct sock {
  struct sock *next; // the next socket in the list
  uint32 raddr;      // the remote IPv4 address
  uint16 lport;      // the local UDP port number
  uint16 rport;      // the remote UDP port number
  struct spinlock lock; // protects the rxq
  struct mbufq rxq;  // a queue of packets waiting to be received
};

static struct spinlock lock;
static struct sock *sockets;

void
sockinit(void)
{
  initlock(&lock, "socktbl");
}

int
sockalloc(struct file **f, uint32 raddr, uint16 lport, uint16 rport)
{
  struct sock *si, *pos;
  si = 0;
  *f = 0;
  if ((*f = filealloc()) == 0)
    goto bad;
  if ((si = (struct sock*)kalloc()) == 0)
    goto bad;
  // initialize objects
  si->raddr = raddr;
  si->lport = lport;
  si->rport = rport;
  initlock(&si->lock, "sock");
  mbufq_init(&si->rxq);
  (*f)->type = FD_SOCK;
  (*f)->readable = 1;
  (*f)->writable = 1;
  (*f)->sock = si;
  // add to list of sockets
  acquire(&lock);
  pos = sockets;
  while (pos) {
    if (pos->raddr == raddr &&
        pos->lport == lport &&
	pos->rport == rport) {
      release(&lock);
      goto bad;
    }
    pos = pos->next;
  }
  si->next = sockets;
  sockets = si;
  release(&lock);
  return 0;

bad:
  if (si)
    kfree((char*)si);
  if (*f)
    fileclose(*f);
  return -1;
}

int
sockread(struct sock *soc, uint64 addr, int n)
{
  acquire(&soc->lock);
  // sleep if rxq is empty
  while(mbufq_empty(&soc->rxq) == 1)
  {
    sleep(soc, &soc->lock);
  }
  struct mbuf* buf = mbufq_pophead(&soc->rxq);
  int max_read = n < buf->len? n : buf->len;
  if(copyout(myproc()->pagetable, addr, (char*)buf->head, max_read) == -1)
  {
    release(&soc->lock);
    return -1;
  }
  mbuffree(buf);
  release(&soc->lock);
  return max_read;
}

int
sockwrite(struct sock *soc, uint64 addr, int n)
{
  struct mbuf* m;
  m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  char* dst = mbufput(m, (unsigned int)n);
  copyin(myproc()->pagetable, dst, addr, (uint64)n);
  acquire(&soc->lock);
  net_tx_udp(m, soc->raddr, soc->lport, soc->rport);
  release(&soc->lock);
  return n;

}

int
sockclose(struct sock *soc)
{
  struct sock* sock_cur = sockets;
  struct sock* sock_prev = 0;
  acquire(&lock);
  while(sock_cur)
  {
    if(sock_cur == soc)
    {
      if(sock_prev != (struct sock*)0)
      {
        sock_prev->next = sock_cur->next;
      }
      // if sock_cur is head of linked-list, update socket to new head
      else
      {
        sockets = sock_cur->next;
      }
      break;
    }
    sock_prev = sock_cur;
    sock_cur = sock_cur->next;
  }
  // target sock not found in sock list
  if(sock_cur == (struct sock*)0)
  {
    return -1;
  }
  while(mbufq_empty(&sock_cur->rxq) != 1)
  {
    struct mbuf* buf = mbufq_pophead(&sock_cur->rxq);
    mbuffree(buf);
  }
  kfree((char*)soc);
  release(&lock);
  return 0;
}

// called by protocol handler layer to deliver UDP packets
void
sockrecvudp(struct mbuf *m, uint32 raddr, uint16 lport, uint16 rport)
{
  // Find the socket that handles this mbuf and deliver it, waking
  // any sleeping reader. Free the mbuf if there are no sockets
  // registered to handle it.
  struct sock* sock_cur = sockets;
  acquire(&lock);
  while(sock_cur)
  {
    if(sock_cur->raddr == raddr &&
       sock_cur->lport == lport &&
       sock_cur->rport == rport)
    {
      break;
    }
    sock_cur = sock_cur->next;
  }
  release(&lock);
  if(sock_cur == (struct sock*)0)
  {
    mbuffree(m);
    return;
  }
  mbufq_pushtail(&sock_cur->rxq, m);
  wakeup(sock_cur);
  return;
}
