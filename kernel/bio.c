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

#define HASHSIZE 13

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  
  //struct buf head;
} bcache;

struct {
  struct spinlock lock;
  struct buf head;          //每个hash桶维护一个链表，所有buf总大小是NBUF
  //struct buf head;
} mybcache[HASHSIZE];

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers
  //bcache.head.prev = &bcache.head;
  //bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    //b->next = bcache.head.next;
    //b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    //bcache.head.next->prev = b;
    //bcache.head.next = b;
    b->ticks = 0;
  }

  for(int i = 0; i <NBUF; ++i) {
    int key = i % HASHSIZE;
    bcache.buf[i].next = mybcache[key].head.next;
    mybcache[key].head.next = &bcache.buf[i];
  }

  for(int i = 0; i < HASHSIZE; ++i) {
    initlock(&mybcache[i].lock, "bcache_bucket");
    //for(b = mybcache[i].head.next; b ; b = b->next){
    //  initsleeplock(&b->lock, "buffer");
    //  b->ticks = 0;
    //}
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  int key = blockno % HASHSIZE;
  //acquire(&bcache.lock);
  acquire(&mybcache[key].lock);

  // Is the block already cached?
  /*for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }*/
  for(b = mybcache[key].head.next; b ; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;

      acquire(&tickslock);
      b->ticks = ticks;
      release(&tickslock);

      release(&mybcache[key].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 没找到缓存，找一个其他缓存来替换（可能是自己桶内的，也可能是其他桶的）
  //先释放自己的锁，防止其他正在寻找替换缓存的进程遍历到这个桶时造成死锁
  release(&mybcache[key].lock);   
  //一个时间段只能有一个进程正在寻找替换的缓存，否则释放桶的锁时，可能也有其他进程进入这个桶里找缓存，且同样没找到，也会寻找其他缓存来替代，就可能导致
  //一个缓存重复分配
  acquire(&bcache.lock);        

  // 还要再找一遍，因为刚才释放了这个桶的锁，就像上面说的那样，可能其他进程也可能刚才没找到而再次分配，所以让他再找一遍，没找到才再分配
  
  for(b = mybcache[key].head.next; b ; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      acquire(&mybcache[key].lock);
      b->refcnt++;

      acquire(&tickslock);
      b->ticks = ticks;
      release(&tickslock);

      release(&mybcache[key].lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }  
  //release(&mybcache[key].lock);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  /*for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }*/
  //没有缓存，找一个替换，而且如果在自己的桶没有找到了，就要去别人的桶找
  struct buf *p = 0;
  int i = 0, cur_key = key;
  for(; i < HASHSIZE; ++i, cur_key = (cur_key + 1) % HASHSIZE) {

    acquire(&mybcache[cur_key].lock);

    struct buf* b_prev = &mybcache[cur_key].head;
    struct buf* p_prev = &mybcache[cur_key].head;
    for(b = mybcache[cur_key].head.next; b ; b = b->next){
      if((b->refcnt == 0) && (p == 0 || p->ticks > b->ticks)) {
        p = b;
        p_prev = b_prev;
      }
      b_prev = b;
    }
    if(p) {
      if(cur_key != key) acquire(&mybcache[key].lock);
      p_prev->next = p->next;
      p->dev = dev;
      p->blockno = blockno;
      p->valid = 0;
      p->refcnt = 1;
      p->next = mybcache[key].head.next;
      mybcache[key].head.next = p;
      if(cur_key != key) release(&mybcache[key].lock);
      release(&mybcache[cur_key].lock);
      release(&bcache.lock);
      acquiresleep(&p->lock);
      return p;
    }  
    release(&mybcache[cur_key].lock);
    //release(&bcache.lock);
  }
  release(&bcache.lock);
  panic("bget: no buffers");
  
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
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
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int key = b->blockno % HASHSIZE; //获取blockno号是安全的,除非refcnt==0,该缓存的blockno才可能改变，但此时获得blockno时还没refcnt--，所以此时refcnt不会等于0

  //acquire(&bcache.lock);
  acquire(&mybcache[key].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    /*b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;*/
    acquire(&tickslock);
    b->ticks = ticks;  //因为原代码中释放的缓存是放到了head->next,也就是最近使用的，所以我们的ticks也要是当前ticks,而不是最小的ticks
    release(&tickslock);
  }
  
  //release(&bcache.lock);
  release(&mybcache[key].lock);
}

void
bpin(struct buf *b) {
  int key = b->blockno % HASHSIZE;
  //acquire(&bcache.lock);
  acquire(&mybcache[key].lock);
  b->refcnt++;
  //release(&bcache.lock);
  release(&mybcache[key].lock);
}

void
bunpin(struct buf *b) {
  int key = b->blockno % HASHSIZE;
  //acquire(&bcache.lock);
  acquire(&mybcache[key].lock);
  b->refcnt--;
  //release(&bcache.lock);
  release(&mybcache[key].lock);
}


