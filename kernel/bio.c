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

#define MAX_CBlock 13

char hashlock_name[MAX_CBlock][18];
char sleeplock_name[NBUF][18];

struct Hash_bcache{
  struct spinlock hash_lock;
  struct buf head;
};

struct {
  struct buf buf[NBUF];
  struct Hash_bcache Hash_bcache[MAX_CBlock];
} bcache;

void
binit(void)
{	
  for(int i =0;i<MAX_CBlock;i++){
    snprintf(hashlock_name[i],11,"bcache_spinlock%d",i);
    initlock(&bcache.Hash_bcache[i].hash_lock, hashlock_name[i]);
    //头结点初始化
	bcache.Hash_bcache[i].head.next=0;
  }
  
  for(int i = 0; i < NBUF; i++){  //修改成哈希表插入
    struct buf *b=&(bcache.buf[i]);
	b->next = bcache.Hash_bcache[0].head.next;
	bcache.Hash_bcache[0].head.next=b;
	snprintf(sleeplock_name[i],12,"bcache_sleeplock%d",i);
    initsleeplock(&b->lock, sleeplock_name[i]);
	b->buf_tick=ticks;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  
  int key=blockno%MAX_CBlock;

  acquire(&bcache.Hash_bcache[key].hash_lock);

  //遍历桶内看是否存在缓存块
  for(b = bcache.Hash_bcache[key].head.next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.Hash_bcache[key].hash_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  uint min_ticks=0xffffffff;
  int find_least;
  release(&bcache.Hash_bcache[key].hash_lock);
  //没有缓存块就要遍历其他桶，标记LRU
  int least_i=-1;
  struct buf* pre_leastbuf=0;
  for(int i=0;i<MAX_CBlock;i++){
	acquire(&bcache.Hash_bcache[i].hash_lock);
	find_least=0;
	for(b= &(bcache.Hash_bcache[i].head);b->next!=0;b=b->next){
	  if(b->next->refcnt==0&&b->next->buf_tick<min_ticks){ //找到LRU块
	  	min_ticks=b->next->buf_tick;
		pre_leastbuf=b;
	  	find_least = 1;
	  }	
    }
	if(!find_least)
	  release(&bcache.Hash_bcache[i].hash_lock);
	else{
	  if(least_i!=-1)
		release(&bcache.Hash_bcache[least_i].hash_lock);
	  least_i=i;
	}
  }
  if(pre_leastbuf==0){
  	panic("No Buffer");
  }
  struct buf* leastbuf=pre_leastbuf->next;
  pre_leastbuf->next=leastbuf->next;
  release(&bcache.Hash_bcache[least_i].hash_lock);
  acquire(&bcache.Hash_bcache[key].hash_lock);
  
  for(b = bcache.Hash_bcache[key].head.next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
	  b->refcnt++;
	  release(&bcache.Hash_bcache[key].hash_lock);
	  acquire(&bcache.Hash_bcache[least_i].hash_lock);
	  leastbuf->next=bcache.Hash_bcache[least_i].head.next;
	  bcache.Hash_bcache[least_i].head.next=leastbuf;
	  release(&bcache.Hash_bcache[least_i].hash_lock);
	  acquiresleep(&b->lock);
	  return b;
	}
  }
  
  leastbuf->dev = dev;
  leastbuf->blockno = blockno;
  leastbuf->valid = 0;
  leastbuf->refcnt = 1;
  leastbuf->next=bcache.Hash_bcache[key].head.next;
  bcache.Hash_bcache[key].head.next=leastbuf;
  release(&bcache.Hash_bcache[key].hash_lock);
  acquiresleep(&leastbuf->lock);
  return leastbuf;
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

  int key=b->blockno%MAX_CBlock; 
  acquire(&bcache.Hash_bcache[key].hash_lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    //b->next->prev = b->pre;
    //b->prev->next = b->next;
    //b->next = bcache.head.next;
    //b->prev = &bcache.head;
    //bcache.head.next->prev = b;
    //bcache.head.next = b;
  	b->buf_tick=ticks;
  } 
  release(&bcache.Hash_bcache[key].hash_lock);
}

void
bpin(struct buf *b) {
  int key=b->blockno%MAX_CBlock; 
  acquire(&bcache.Hash_bcache[key].hash_lock);
  b->refcnt++;
  release(&bcache.Hash_bcache[key].hash_lock);
}

void
bunpin(struct buf *b) {
  int key=b->blockno%MAX_CBlock; 
  acquire(&bcache.Hash_bcache[key].hash_lock);
  b->refcnt--;
  release(&bcache.Hash_bcache[key].hash_lock);
}


