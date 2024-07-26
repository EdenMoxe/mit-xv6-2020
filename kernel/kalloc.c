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
} kmem[NCPU];

char lock_name[NCPU][9];

void
kinit()
{
  for(int i = 0;i<NCPU;i++){
    snprintf(lock_name[i],9,"kmem_cpu%d",i);   
  	initlock(&kmem[i].lock,lock_name[i]);
  }
  //initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  int cpu_id=cpuid();
  acquire(&kmem[cpu_id].lock);
  r->next = kmem[cpu_id].freelist;
  kmem[cpu_id].freelist = r;
  release(&kmem[cpu_id].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  push_off();
  int cpu_id=cpuid();
  acquire(&kmem[cpu_id].lock);
  //acquire(&kmem.lock);
  struct run *r;
  r = kmem[cpu_id].freelist;
  if(r){
    kmem[cpu_id].freelist = r->next;
    release(&kmem[cpu_id].lock);
  }
  else{
    release(&kmem[cpu_id].lock);
	int i;
	struct run *steal_page;
 	for(i=0;i<NCPU;i++){
	  if(i==cpu_id)
		continue;
      acquire(&kmem[i].lock);
	  steal_page=kmem[i].freelist; //
	  if(steal_page&&steal_page->next){
	  	kmem[i].freelist=steal_page->next;
	    release(&kmem[i].lock);
	    break;
	  }
	  else if(steal_page){
	    kmem[i].freelist=0;
	    release(&kmem[i].lock);
	    break;
	  }
	  else{
	    steal_page=0;	
	    release(&kmem[i].lock); 
	  }
	}
	if(steal_page){
	  r=steal_page;
	}
  	else{
	  r=0;
	}    
  }
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk 
  pop_off();
  return (void*)r;
}
