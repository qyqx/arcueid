/* 
  Copyright (C) 2009 Rafael R. Sevilla

  This file is part of CArc

  CArc is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 3 of the
  License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA
  02110-1301 USA.
*/
/* This default memory allocator and garbage collector uses a simple
   free-list to manage memory blocks, and the VCGC garbage collector
   by Huelsbergen and Winterbottom (ISMM 1998).  Someone's been reading
   the sources for Inferno emu and the OCaml bytecode interpreter!
   We'll try using a more advanced memory allocator later on. */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include "carc.h"
#include "alloc.h"
#include "../config.h"

static Bhdr *fl_head = NULL;
static Hhdr *heaps = NULL;

#define GC_QUANTA 50
#define MAX_GC_QUANTA 15*GC_QUANTA

static int quanta = GC_QUANTA;
static int visit;
static Hhdr *gchptr = NULL;
static Bhdr *gcptr = NULL;
static int gce = 0, gct = 1;
unsigned long long gcepochs = 0;
static unsigned long long gccolor = 3;
static unsigned long long gcnruns = 0;
int nprop = 0;
static int mutator = 0;
static int marker = 1;
static int sweeper = 2;
#define propagator PROPAGATOR_COLOR
#define MAX_MARK_RECURSION 64

/* Allocate memory for the heap.  This uses the low level memory allocator
   function specified in the carc structure.  Takes care of filling in the
   heap header information and adding the heap to the list of heaps. */
static void *alloc_for_heap(carc *c, size_t req)
{
  char *mem;
  void *block;
  Hhdr *oldheaps;

  mem = (char *)c->mem_alloc(req + sizeof(Hhdr), sizeof(Hhdr), &block);
  if (mem == NULL)
    return(NULL);
  oldheaps = heaps;
  heaps = (Hhdr *)mem;
  mem += sizeof(Hhdr);
  HHDR_SIZE(mem) = req;
  HHDR_BLOCK(mem) = block;
  HHDR_NEXT(mem) = oldheaps;
  return(mem);
}

/* Add a block to the free list.  The block's header fields are filled
   in by this function.  This also works to add a newly created block
   from expand_heap to the free list. */
static void fl_free_block(Bhdr *blk)
{
  Bhdr *prev, **prevnext, *cur;
  int inserted;

  prev = NULL;
  prevnext = &fl_head;
  cur = fl_head;
  inserted = 0;
  while (cur != NULL) {
    if (B2NB(blk) == cur) {
      /* end of the block itself coincides with the start of the current
	 block.  Coalesce with the current block. */
      FBNEXT(blk) = FBNEXT(cur);
      *prevnext = blk;
      blk->size += cur->size + BHDRSIZE;
      inserted = 1;
      cur = FBNEXT(cur);
      continue;
    }

    if (B2NB(cur) == blk) {
      /* end of the current block coincides with the start of the block
	 to be freed.  Coalesce them. */
      cur->size += blk->size + BHDRSIZE;
      blk = cur;
      inserted = 1;
      cur = FBNEXT(cur);
      continue;
    }

    if (prev < blk && cur > blk) {
      /* Cannot coalesce, just plain insert */
      inserted = 1;
      *prevnext = blk;
      FBNEXT(blk) = cur;
      return;
    }
    prev = cur;
    prevnext = &FBNEXT(prev);
    cur = FBNEXT(cur);
  }

  if (inserted)
    return;
  /* If we get here, we have reached the end of the free list.  The block
     must have a higher address than any other block already present in
     the free list.  Tack it onto the end. */
  assert(prev <= blk);
  FBNEXT(blk) = *prevnext;
  *prevnext = blk;
}

static void free_block(struct carc *c, void *blk)
{
  Bhdr *h;

  D2B(h, blk);
  h->magic = MAGIC_F;
  fl_free_block(h);
}

#define ALLOC_BLOCK(blk) { (blk)->magic = MAGIC_A; (blk)->color = mutator; }

/* Get a block from the free list of at least [size].  Return NULL if
   there are no suitable blocks in the free list. */
static void *fl_alloc(size_t size)
{
  Bhdr *prev, *cur;

  if (fl_head == NULL)
    return(NULL);

  /* If the size of the head block is the size of the block or larger by
     at most the size of a block header, just use the block as is.  If the
     slack is less than that of a block header, we can't use the spare
     space since a block header can't be added to it. */
  if (fl_head->size >= size && fl_head->size <= size + BHDRSIZE) {
    cur = fl_head;
    fl_head = FBNEXT(cur);
    ALLOC_BLOCK(cur);
    return(B2D(cur));
  }

  /* If the head block is larger than that, carve out the right
     hand side and use that. */
  if (fl_head->size > size + BHDRSIZE) {
    fl_head->size -= size + BHDRSIZE;
    cur = B2NB(fl_head);
    cur->size = size;
    ALLOC_BLOCK(cur);
    return(B2D(cur));
  }

  /* The head is too small.  Traverse the free list to find the
     first block which is suitable. */
  prev = fl_head;
  cur = FBNEXT(prev);
  while (cur != NULL) {
    if (cur->size >= size && cur->size <= size + BHDRSIZE) {
      /* Unlink */
      FBNEXT(prev) = FBNEXT(cur);
      ALLOC_BLOCK(cur);
      return(B2D(cur));
    }

    if (cur->size > size + BHDRSIZE) {
      /* Carve */
      cur->size -= size + BHDRSIZE;
      cur = B2NB(cur);
      cur->size = size;
      ALLOC_BLOCK(cur);
      return(B2D(cur));
    }
  }
  return(NULL);
}

/* Allocate more memory using malloc/mmap.  This creates a new heap chunk,
   links it into the heap chunk list, and creates two block headers, one
   marking the new free block that fills the entire heap chunk and a second
   marking the end of the heap chunk.

   The heap expansion algorithm basically works by allocating
   [c->over_percent] more space than requested, plus the size of two
   block headers.  If the heap is smaller than the minimum expansion
   size, clamp it to the minimum expansion size.  It then rounds
   up the size of the chunk to the next larger multiple of the page
   size.
 */
static Bhdr *expand_heap(carc *c, size_t request)
{
  Bhdr *mem, *tail;
  size_t over_request, rounded_request;

  /* Allocate c->over_percent more beyond the requested amount plus
     space for the two headers, one for the header of the new free block
     and another for the tail block marking the end of the arena. */
  over_request = request + ((request / 100) * c->over_percent) + 2*BHDRSIZE;
  /* If less than minimum, expand to the minimum */
  if (over_request < c->minexp)
    over_request = c->minexp;
  ROUNDHEAP(rounded_request, over_request);
  mem = (Bhdr *)alloc_for_heap(c, rounded_request);
  if (mem == NULL) {
    c->signal_error(c, "No room for growing heap");
    return(NULL);
  }
  mem->magic = MAGIC_F;
  mem->size = rounded_request - BHDRSIZE;
  mem->color = mutator;
  /* Add a tail block to this new heap chunk so that the sweeper knows
     that it has reached the end of the heap chunk and should begin
     sweeping the next one, if any. */
  tail = B2NB(mem);
  tail->magic = MAGIC_E;
  tail->size = 0;
  return(mem);
}

static void *alloc(carc *c, size_t osize)
{
  void *blk;
  size_t size;
  Bhdr *nblk;

  /* Adjust the size of the allocated block so that we maintain
     alignment of at least 16 bytes. */
  ROUNDSIZE(size, osize);
  blk = fl_alloc(size);
  if (blk == NULL) {
    nblk = expand_heap(c, size);
    if (nblk == NULL) {
      c->signal_error(c, "Fatal error: out of memory!");
      return(NULL);
    }
    fl_free_block(nblk);
    blk = fl_alloc(size);
  }
  return(blk);
}

static value get_cell(carc *c)
{
  void *cellptr;

  cellptr = alloc(c, sizeof(struct cell));
  if (cellptr == NULL)
    return(CNIL);
  return((value)cellptr);
}

Hhdr *__carc_get_heap_start(void)
{
  return(heaps);
}

#define SETMARK(h) if ((h)->color != mutator) { (h)->color = propagator; nprop=1; }


static void mark(carc *c, value v, int reclevel)
{
  Bhdr *b;
  void *ctx;
  value val;
  int i;

  /* Do not try to mark an immediate value! */
  if (IMMEDIATE_P(v) || v == CNIL || v == CTRUE || v == CUNDEF)
    return;

  D2B(b, (void *)v);
  SETMARK(b);

  if (--visit >= 0 && reclevel < MAX_MARK_RECURSION) {
    gce--;
    b->color = mutator;

    switch (TYPE(v)) {
    case T_CONS:
      mark(c, car(v), reclevel+1);
      mark(c, cdr(v), reclevel+1);
      break;
    case T_TABLE:
      ctx = NULL;
      while ((val = carc_hash_iter(c, v, &ctx)) != CNIL)
	mark(c, val, reclevel+1);
      break;
    case T_VECTOR:
    case T_CLOS:
    case T_CONT:
      for (i=0; i<REP(v)._vector.length; i++)
	mark(c, REP(v)._vector.data[i], reclevel+1);
      break;
      /* XXX fill in with other composite types as they are defined */
    default:
      /* The other types do not contain further pointers inside them
	 and do not require recursion. */
      break;
    }
  }
}

static void sweep(carc *c, value v)
{
  if (IMMEDIATE_P(v))
    return;

  switch (TYPE(v)) {
  case T_STRING:
    c->free_block(c, REP(v)._str.str); /* a string's actual data is marked T_IMMUTABLE */
    c->free_block(c, (void *)v);
    break;
#ifdef HAVE_GMP_H
  case T_BIGNUM:
    mpz_clear(REP(v)._bignum);
    c->free_block(c, (void *)v);
    break;
  case T_RATIONAL:
    mpq_clear(REP(v)._rational);
    c->free_block(c, (void *)v);
    break;
#endif
  case T_FLONUM:
  case T_COMPLEX:
  case T_CONS:
  case T_VECTOR:
  case T_CLOS:
  case T_CONT:
  case T_CODE:
    c->free_block(c, (void *)v);
    break;
  case T_TABLE:
    c->free_block(c, REP(v)._hash.table); /* free the immutable memory of the hash table */
    c->free_block(c, (void *)v);
    break;
  }
}

static void rootset(carc *c)
{
  mutator = gccolor % 3;
  marker = (gccolor-1)%3;
  sweeper = (gccolor-2)%3;

  /* Mark the threads, symbol tables, and global environment with
     propagators so that the virtual machine considers them part
     of the rootset. */
  MARKPROP(c->vmthreads);
  MARKPROP(c->symtable);
  MARKPROP(c->rsymtable);
  MARKPROP(c->genv);
}

static void rungc(carc *c)
{
  value h;

  gcnruns++;

  for (visit = quanta; visit > 0;) {
    if (gchptr == NULL) {
      gchptr = heaps;
      if (gchptr == NULL)
	return;
    }
    if (gcptr == NULL)
      gcptr = (Bhdr *)((char *)gchptr + sizeof(Hhdr));
    if (gcptr->magic == MAGIC_A) {
      visit--;
      gct++;
      h = (value)B2D(gcptr);
      if (gcptr->color == propagator) {
	gce--;
	gcptr->color = mutator;
	mark(c, h, 0);
      } else if (gcptr->color == sweeper) {
	gce++;
	sweep(c, h);
      }
    }
    gcptr = B2NB(gcptr);
    if (gcptr->magic == MAGIC_E) {
      /* reached the end of the heap block, go to the next one */
      gchptr = (Hhdr *)gchptr->next;
      gcptr = NULL;
      if (gchptr == NULL)
	break; 			/* stop if we finished the last heap block */
    }
  }

  quanta = (MAX_GC_QUANTA + GC_QUANTA)/2
    + ((MAX_GC_QUANTA-GC_QUANTA)/20)*((100*gce)/gct);
  if (quanta < GC_QUANTA)
    quanta = GC_QUANTA;
  if (quanta > MAX_GC_QUANTA)
    quanta = MAX_GC_QUANTA;

  if (gchptr != NULL)		/* completed this iteration? */
    return;

  if (nprop == 0) {		/* completed the epoch? */
    gcepochs++;
    gccolor++;
    rootset(c);
    gce = 0;
    gct = 1;
    return;
  }
  nprop = 0;
}

void carc_set_memmgr(carc *c)
{
  c->get_cell = get_cell;
  c->get_block = alloc;
  c->free_block = free_block;
#ifdef HAVE_MMAP
  c->mem_alloc = __carc_aligned_mmap;
  c->mem_free = __carc_aligned_munmap;
#else
  c->mem_alloc = __carc_aligned_malloc;
  c->mem_free = __carc_aligned_free;
#endif
  c->rungc = rungc;

  gcepochs = 0;
  gccolor = 3;
  mutator = 0;
  marker = 1;
  sweeper = 2;

  gce = 0;
  gct = 1;

  /* Set default parameters for heap expansion policy */
  c->minexp = DFL_MIN_EXP;
  c->over_percent = DFL_OVER_PERCENT;
}
