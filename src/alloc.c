/* 
  Copyright (C) 2010 Rafael R. Sevilla

  This file is part of Arcueid

  Arcueid is free software; you can redistribute it and/or modify it
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
#include "arcueid.h"
#include "alloc.h"
#include "arith.h"
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
static unsigned long long gc_milliseconds = 0ULL;

/* Allocate memory for the heap.  This uses the low level memory allocator
   function specified in the arc structure.  Takes care of filling in the
   heap header information and adding the heap to the list of heaps. */
static void *alloc_for_heap(arc *c, size_t req)
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

static void free_block(struct arc *c, void *blk)
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
    cur = FBNEXT(cur);
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
static Bhdr *expand_heap(arc *c, size_t request)
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
    arc_err_cstrfmt(c, "No room for growing heap");
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

static void *alloc(arc *c, size_t osize)
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
      arc_err_cstrfmt(c, "Fatal error: out of memory!");
      return(NULL);
    }
    fl_free_block(nblk);
    blk = fl_alloc(size);
  }
  return(blk);
}

static value get_cell(arc *c)
{
  void *cellptr;

  cellptr = alloc(c, sizeof(struct cell));
  if (cellptr == NULL)
    return(CNIL);
  return((value)cellptr);
}

Hhdr *__arc_get_heap_start(void)
{
  return(heaps);
}

#define SETMARK(h) if ((h)->color != mutator) { (h)->color = propagator; nprop=1; }


static void mark(arc *c, value v, int reclevel)
{
  Bhdr *b;
  int ctx;
  value val, *vptr;
  int i;

  /* If we find a symbol here, find its hash buckets in the symbol
     tables and mark those.  This provides symbol garbage collection,
     leaving only symbols which are actually in active use. */
  if (TYPE(v) == T_SYMBOL) {
    val = arc_hash_lookup2(c, c->rsymtable, v);
    /* avoid double-marking symbols whose buckets are already set
       to mutator color. */
    D2B(b, (void *)val);
    if (b->color != mutator)
      mark(c, val, reclevel);
    val = arc_hash_lookup2(c, c->symtable, REP(val)._hashbucket.val);
    D2B(b, (void *)val);
    if (b->color != mutator)
      mark(c, val, reclevel);
    return;
  }

  /* Do not try to mark an immediate value! */
  if (IMMEDIATE_P(v))
    return;

  D2B(b, (void *)v);
  SETMARK(b);

  if (--visit >= 0 && reclevel < MAX_MARK_RECURSION) {
    gce--;
    b->color = mutator;

    switch (TYPE(v)) {
    case T_CONS:
    case T_CLOS:
    case T_ENV:
      mark(c, car(v), reclevel+1);
      mark(c, cdr(v), reclevel+1);
      break;
    case T_TABLE:
      ctx = 0;
      while ((val = arc_hash_iter(c, v, &ctx)) != CUNBOUND)
	mark(c, val, reclevel+1);
      break;
    case T_TBUCKET:
      mark(c, REP(v)._hashbucket.key, reclevel+1);
      mark(c, REP(v)._hashbucket.val, reclevel+1);
      break;
    case T_THREAD:
      /* mark the registers inside of the thread */
      mark(c, TFUNR(v), reclevel+1); /* function register */
      mark(c, TENVR(v), reclevel+1); /* environment register */
      mark(c, TVALR(v), reclevel+1); /* value register */
      mark(c, TCONR(v), reclevel+1); /* continuation register */
      mark(c, TECONT(v), reclevel+1); /* error continuation */
      mark(c, TEXC(v), reclevel+1);   /* current exception */
      mark(c, TSTDH(v), reclevel+1);  /* standard handles */
      /* Mark the stack of this thread (used portions only) */
      for (vptr = TSP(v); vptr == TSTOP(v); vptr++)
	mark(c, *vptr, reclevel+1);
      break;
    case T_VECTOR:
    case T_CODE:
    case T_CONT:
      for (i=0; i<REP(v)._vector.length; i++)
	mark(c, REP(v)._vector.data[i], reclevel+1);
      break;
    case T_PORT:
    case T_CUSTOM:
      /* for custom data types (including ports), call the marker
	 function defined for it, and pass ourselves as the next
	 level mark. */
      REP(v)._custom.marker(c, v, reclevel+1, mark);
      break;
      /* XXX fill in with other composite types as they are defined */
    default:
      /* The other types do not contain further pointers inside them
	 and do not require recursion. */
      break;
    }
  }
}

static void sweep(arc *c, value v)
{
  /* The only special cases here are for those data types which point to
     immutable memory blocks which are otherwise invisible to the sweeper
     or allocate memory blocks not known to the allocator.  These include
     strings and hash tables (immutable memory) and bignums and rationals
     (use malloc/free directly I believe). */
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
  case T_TABLE:
    c->free_block(c, REP(v)._hash.table); /* free the immutable memory of the hash table */
    c->free_block(c, (void *)v);
    break;
  case T_TBUCKET:
    /* Make the cell in the parent hash table unbound as well. */
    REP(REP(v)._hashbucket.hash)._hash.table[REP(v)._hashbucket.index] = CUNBOUND;
    c->free_block(c, (void *)v);
    break;
  case T_PORT:
  case T_CUSTOM:
    REP(v)._custom.sweeper(c, v);
    break;
  default:
    /* this should do for almost everything else */
    c->free_block(c, (void *)v);
    break;
  }
}

static void rootset(arc *c)
{
  mutator = gccolor % 3;
  marker = (gccolor-1)%3;
  sweeper = (gccolor-2)%3;

  /* Mark the threads and global environment with propagators so that
     the virtual machine considers them part of the rootset.  Note that
     the symbol tables are *not* part of the rootset.  The symbol tables
     themselves are marked as immutable, so they are not subject to
     garbage collection, but the symbols inside them must be referenced
     elsewhere to prevent their being GCed. */
  MARKPROP(c->vmthreads);
  MARKPROP(c->genv);
  MARKPROP(c->builtin);
  MARKPROP(c->splforms);
  MARKPROP(c->inlfuncs);
  MARKPROP(c->iowaittbl);
}

static void rungc(arc *c)
{
  value h;
  unsigned long long gcst, gcet;

  gcst = __arc_milliseconds();
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
    goto endgc;

  if (nprop == 0) {		/* completed the epoch? */
    gcepochs++;
    gccolor++;
    rootset(c);
    gce = 0;
    gct = 1;
    goto endgc;
  }
  nprop = 0;
 endgc:
  gcet = __arc_milliseconds();
  gc_milliseconds += (gcet - gcst);
}

void arc_set_memmgr(arc *c)
{
  c->get_cell = get_cell;
  c->get_block = alloc;
  c->free_block = free_block;
#ifdef HAVE_MMAP
  c->mem_alloc = __arc_aligned_mmap;
  c->mem_free = __arc_aligned_munmap;
#else
  c->mem_alloc = __arc_aligned_malloc;
  c->mem_free = __arc_aligned_free;
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

value arc_current_gc_milliseconds(arc *c)
{
  unsigned long long ms;

  ms = gc_milliseconds;
  if (ms < FIXNUM_MAX) {
    return(INT2FIX(ms));
  } else {
#ifdef HAVE_GMP_H
    value msbn;

#if SIZEOF_UNSIGNED_LONG_LONG == 8
    /* feed value into the bignum 32 bits at a time */
    msbn = arc_mkbignuml(c, (ms >> 32)&0xffffffff);
    mpz_mul_2exp(REP(msbn)._bignum, REP(msbn)._bignum, 32);
    mpz_add_ui(REP(msbn)._bignum, REP(msbn)._bignum, ms & 0xffffffff);
#else
    int i;

    msbn = arc_mkbignuml(c, 0);
    for (i=SIZEOF_UNSIGNED_LONG_LONG-1; i>=0; i--) {
      mpz_mul_2exp(REP(msbn)._bignum, REP(msbn)._bignum, 8);
      mpz_add_ui(REP(msbn)._bignum, REP(msbn)._bignum, (ms >> (i*8)) & 0xff);
    }
#endif
    return(msbn);
#else
    /* floating point */
    return(arc_mkflonum(c, (double)ms));
#endif
  }
  return(CNIL);
}
