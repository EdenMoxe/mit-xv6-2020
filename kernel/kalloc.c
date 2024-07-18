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

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

//for cow test
#define PA2CNT(pa) (((uint64)pa-KERNBASE)/PGSIZE)
struct {
  int cow_num[PA2CNT(PHYSTOP)+1];                
  struct spinlock cow_lock;
}cow_count;

int P(uint64 pa){
  acquire(&cow_count.cow_lock);
  if(pa<KERNBASE||pa>=PHYSTOP)
    panic("P pa error\n");
  int use_num=--cow_count.cow_num[PA2CNT(pa)];
  release(&cow_count.cow_lock);
  return use_num;
}

int V(uint64 pa){
  acquire(&cow_count.cow_lock);
  if(pa<KERNBASE||pa>=PHYSTOP)
    panic("V pa error\n");
  int use_num=++cow_count.cow_num[PA2CNT(pa)];
  release(&cow_count.cow_lock);
  return use_num;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&cow_count.cow_lock,"cow_count");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(int i = 0; i < sizeof(cow_count.cow_num)/ sizeof(int); ++ i)
  	cow_count.cow_num[i]=1;
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
 
  int num=P((uint64)pa);
  if(num<0)
	panic("kfree P<0");
  if(num>0)
	return;
   
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE); // fill with junk
	//printf("%p\n",r);
    cow_count.cow_num[PA2CNT(r)]=1;
	//printf("%x\n",PA2CNT(r));
  }
  return (void*)r;
}
