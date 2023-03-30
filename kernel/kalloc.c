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

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  uint64 *usage;
  struct spinlock lock;
  struct run *freelist;
} kmem;

int change_usage(uint64 pa, int k) {
  int result, idx = get_idx(pa);
  acquire(&kmem.lock);
  if (kmem.usage[idx] == 0 && k < 0) {
    panic("change_usage: usage zero");
  }
  kmem.usage[idx] += k;
  result = kmem.usage[idx];
  release(&kmem.lock);
  return result;
}

int dec_usage(uint64 pa) { return change_usage(pa, -1); }

void inc_usage(uint64 pa) { change_usage(pa, 1); }

void kinit() {
  uint64 addr = PGROUNDUP((uint64)end);
  int pages = 0;
  kmem.usage = (uint64 *)addr;

  while (addr < PHYSTOP) {
    kmem.usage[get_idx(addr)] = 1;
    addr += PGSIZE;
    pages++;
  }

  initlock(&kmem.lock, "kmem");
  freerange(kmem.usage + pages, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  if (dec_usage((uint64)pa) != 0) return;

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
void *kalloc(void) {
  struct run *r;
  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r) {
    kmem.freelist = r->next;
    kmem.usage[get_idx((uint64)r)]++;
  }
  release(&kmem.lock);

  if (r) memset((char *)r, 5, PGSIZE);  // fill with junk
  return (void *)r;
}