// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define BUCKET_SIZE 13
#define BLOCK_NUM 5000

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
  // Linked list of all buffers, through prev/next.
  // head.next is most recently used.
  struct buf head;
} bcache;

struct bucket {
  struct spinlock lock;
  struct buf *data[BUCKET_SIZE];
};


struct hashtable {
  struct bucket item[BLOCK_NUM];
} hash_table;

void hash(uint dev, uint blockno, struct buf* b)
{
  // This is safe since currently dev is relatively small
  int key = dev*2000 + blockno;
  int bucket_idx = key/BUCKET_SIZE;
  int offset = key%BUCKET_SIZE;
  // printf("[hash] dev:%d, blockno:%d to buf:%p\n", dev, blockno, b);
  hash_table.item[bucket_idx].data[offset] = b;
}

struct buf* get_val(uint dev, uint blockno)
{
  int key = dev * 2000 + blockno;
  int bucket_idx = key / BUCKET_SIZE;
  int offset = key % BUCKET_SIZE;
  // printf("[get_val] dev:%d, blockno:%d to buf:%p\n", dev, blockno, hash_table.item[bucket_idx].data[offset]);
  return hash_table.item[bucket_idx].data[offset];
}

void init_hash_table()
{
  for(int i = 0; i < BLOCK_NUM; ++i)
  {
    for(int j = 0; j < BUCKET_SIZE; ++j)
    {
      hash_table.item[i].data[j] = (struct buf*)0;
    }
  }
}

void
binit(void)
{
  struct buf* b;
  init_hash_table();
  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    b->ticks_recently_touched = ticks;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct buf* b_oldest = (struct buf*) 0;
  uint ticks_min = (uint)-1;

  if((b = get_val(dev, blockno)) != (struct buf*)0)
  {
    // printf("b->dev:%d, b->blockno:%d,         dev:%d, blockno:%d\n", b->dev, b->blockno, dev, blockno);
    acquiresleep(&b->lock);
    b->refcnt++;
    b->ticks_recently_touched = ticks;
    return b;
  }


  acquire(&bcache.lock);
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev)
  {
    if(b->refcnt == 0)
    {
      if(b->ticks_recently_touched < ticks_min)
      {
        ticks_min = b->ticks_recently_touched;
        b_oldest = b;
      }
    }
  }
  if (ticks_min != (uint)-1) // found one buf to evict
  {
    hash(b_oldest->dev, b_oldest->blockno, (struct buf *)0);
    b_oldest->dev = dev;
    b_oldest->blockno = blockno;
    b_oldest->valid = 0;
    b_oldest->refcnt = 1;
    hash(dev, blockno, b_oldest);
    release(&bcache.lock);
    acquiresleep(&b_oldest->lock);
    return b_oldest;
  }
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b->dev, b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b->dev, b, 1);
}

// Release a locked buffer.
// Move to the head of the MRU list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  b->refcnt--;
  releasesleep(&b->lock);
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


