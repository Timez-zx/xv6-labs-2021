// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end, int id);
int  free_realloc(int realloc_id);
void kfree_init(void *pa, int id);
void* kalloc_id(int id);
extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  char *name="kmem";
  uint64 part = (PHYSTOP-(uint64) end)/NCPU;
  uint64 start = (uint64) end;
  for(int i = 0; i <  NCPU; i++){
    // snprintf(name, 10, "kmem%d", i);
    initlock(&kmem[i].lock, name);
    if(i <= NCPU - 1)
      freerange((void*) start+i*part, (void*) start+(i+1)*part, i);
  }
}

void
freerange(void *pa_start, void *pa_end, int id)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  char *p_end =  (char*)PGROUNDUP((uint64)pa_end);
  for(; p + PGSIZE <= (char*)p_end; p += PGSIZE)
    kfree_init(p, id);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree_init(void *pa, int id)
{
  struct run *r;
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
}

void
kfree(void *pa)
{
  struct run *r;
  int id;
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off(); 
  id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int id;
  int status;
  push_off(); 
  id = cpuid();
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  else{
    status = free_realloc(id);
    if(status){
      r = kmem[id].freelist;
      kmem[id].freelist = r->next;
    }
  }
  release(&kmem[id].lock);
  pop_off();
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

int
free_realloc(int realloc_id)
{
  struct run *r;
  void * pa;
  for(int i = 0; i< NCPU; i++){
    if(i != realloc_id){
      pa = kalloc_id(i);
      if(pa){
        break;
      }
      else{
        if(i == NCPU-1)
          return 0;
        else
          continue;
      }
    }
  }
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  // acquire(&kmem[realloc_id].lock);
  r->next = kmem[realloc_id].freelist;
  kmem[realloc_id].freelist = r;
  // release(&kmem[realloc_id].lock);
  return 1;
}

void *
kalloc_id(int id)
{
  struct run *r;
  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}


