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
#define BLOCK_NUM 100

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
} bcache;

struct bucket {
  struct spinlock lock;
  struct buf *data[BUCKET_SIZE];
};


struct hashtable {
  struct bucket item[BLOCK_NUM];
} hash_table;

void hash_set(uint dev, uint blockno, struct buf* b)
{
  // This is safe since currently dev is relatively small
  int key = dev * 2000 + blockno;
  int bucket_idx = key / BUCKET_SIZE;
  struct bucket* buck = &hash_table.item[bucket_idx];
  static int count = 0;
  acquire(&buck->lock);
  for(int idx = 0; idx < BUCKET_SIZE; ++idx)
  {
    if(buck->data[idx] == (struct buf*) 0)
    {
      buck->data[idx] = b;
      count++;
      break;

    }
  }
  release(&buck->lock);
}

void hash_clear(uint dev, uint blockno)
{
  int key = dev * 2000 + blockno;
  int bucket_idx = key / BUCKET_SIZE;
  struct bucket* buck = &hash_table.item[bucket_idx];
  static int count = 0;
  acquire(&buck->lock);
  for (int idx = 0; idx < BUCKET_SIZE; ++idx)
  {
    if (buck->data[idx] != (struct buf*)0 && buck->data[idx]->dev == dev && buck->data[idx]->blockno == blockno)
    {
      count++;
      buck->data[idx] = (struct buf*)0;
      break;
    }
  }
  release(&buck->lock);
}

struct buf* get_val(uint dev, uint blockno)
{
  int key = dev * 2000 + blockno;
  int bucket_idx = key / BUCKET_SIZE;
  int find_buf = 0;
  struct bucket* buck = &hash_table.item[bucket_idx];
  struct buf* b = (struct buf*) 0;
  static int count = 0;
  // acquire(&buck->lock);
  for (int idx = 0; idx < BUCKET_SIZE; ++idx)
  {
    b = buck->data[idx];
    if (b != (struct buf *)0)
    {
      if(b->dev == dev && b->blockno == blockno)
      {
        find_buf = 1;
        count++;
        break;
      }
    }
  }
  // release(&buck->lock);
  if(!find_buf)
  {
    return (struct buf*)0;
  }
  return b;
}

void init_hash_table()
{
  for(int i = 0; i < BLOCK_NUM; ++i)
  {
    initlock(&hash_table.item[i].lock, "bcache.bucket");
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

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->ticks_recently_touched = ticks;
    initsleeplock(&b->lock, "buffer");
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
    acquiresleep(&b->lock);
    b->refcnt++;
    b->ticks_recently_touched = ticks;
    return b;
  }


  acquire(&bcache.lock);
  for(b = bcache.buf; b < bcache.buf+NBUF; b++ )
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
    // clear the mapping from (b_oldest->dev, b_oldest->blockno) so that no double mapping to b_oldest
    hash_clear(b_oldest->dev, b_oldest->blockno);
    b_oldest->dev = dev;
    b_oldest->blockno = blockno;
    b_oldest->valid = 0;
    b_oldest->refcnt = 1;
    hash_set(dev, blockno, b_oldest);
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

