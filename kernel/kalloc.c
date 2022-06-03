// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

extern uint64 cas(volatile void *addr, int expected, int newval);

#define NUM_PYS_PAGES ((PHYSTOP-KERNBASE) / PGSIZE)

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
  uint ref_count_arr[NUM_PYS_PAGES];
} kmem;

// gets the reference count for the given physical address

int get_ref_count(uint64 pa)
{
  return kmem.ref_count_arr[(pa - KERNBASE) / PGSIZE];
}

void
increase_ref(uint64 pa)
{
  int ref;
  do{
    ref = kmem.ref_count_arr[(pa - KERNBASE) / PGSIZE];
  }while(cas(kmem.ref_count_arr + ((pa - KERNBASE) / PGSIZE), ref , ref + 1));
}

void
decrease_ref(uint64 pa)
{
  int ref;
  do{
    ref = kmem.ref_count_arr[(pa - KERNBASE) / PGSIZE];
  }while(cas(kmem.ref_count_arr + ((pa - KERNBASE) / PGSIZE), ref , ref - 1));
}


void kinit()
{
  initlock(&kmem.lock, "kmem");
  // set all reference counts to zero
  memset(kmem.ref_count_arr, 0, sizeof(kmem.ref_count_arr));
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
  {
    // cas(kmem.ref_count_arr + ((uint64)p - KERNBASE / PGSIZE), 0 , 1);
    kmem.ref_count_arr[(((uint64)p - KERNBASE) / PGSIZE)] = 1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;
  decrease_ref((uint64)pa);
  if(get_ref_count((uint64)pa) > 0)   
    return;
  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

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
  if (r)
    kmem.freelist = r->next;

  release(&kmem.lock);
  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
                                  // set the newly allocated page's reference count to 1
  if (r)
    increase_ref((uint64)r);

  return (void *)r;
}
