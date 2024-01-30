/*
 * cache.c
 */


#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>

#include "cache.h"
#include "main.h"

#define log_float(x) ((float)( log((double)(x)) / log(2) ))

/* cache configuration parameters */
static int cache_split = 0;
static int cache_usize = DEFAULT_CACHE_SIZE;
static int cache_isize = DEFAULT_CACHE_SIZE; 
static int cache_dsize = DEFAULT_CACHE_SIZE;
static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;
static int cache_assoc = DEFAULT_CACHE_ASSOC;
static int cache_writeback = DEFAULT_CACHE_WRITEBACK;
static int cache_writealloc = DEFAULT_CACHE_WRITEALLOC;

/* cache model data structures */
static Pcache icache;
static Pcache dcache;
static cache c1;
static cache c2;
static cache_stat cache_stat_inst;
static cache_stat cache_stat_data;

/************************************************************/
void set_cache_param(param, value)
  int param;
  int value;
{

  switch (param) {
  case CACHE_PARAM_BLOCK_SIZE:
    cache_block_size = value;
    words_per_block = value / WORD_SIZE;
    break;
  case CACHE_PARAM_USIZE:
    cache_split = FALSE;
    cache_usize = value;
    break;
  case CACHE_PARAM_ISIZE:
    cache_split = TRUE;
    cache_isize = value;
    break;
  case CACHE_PARAM_DSIZE:
    cache_split = TRUE;
    cache_dsize = value;
    break;
  case CACHE_PARAM_ASSOC:
    cache_assoc = value;
    break;
  case CACHE_PARAM_WRITEBACK:
    cache_writeback = TRUE;
    break;
  case CACHE_PARAM_WRITETHROUGH:
    cache_writeback = FALSE;
    break;
  case CACHE_PARAM_WRITEALLOC:
    cache_writealloc = TRUE;
    break;
  case CACHE_PARAM_NOWRITEALLOC:
    cache_writealloc = FALSE;
    break;
  default:
    printf("error set_cache_param: bad parameter value\n");
    exit(-1);
  }

}

cache init_cache_helper(cache ch){
	ch.associativity = cache_assoc;                      
	ch.n_sets = ch.size / (cache_block_size * cache_assoc);
	ch.index_mask_offset = LOG2(cache_block_size);
	int index_plus_offset_bits = (int) ceil( log_float(ch.n_sets)) + LOG2(cache_block_size);
	ch.index_mask = ((1 << index_plus_offset_bits) - 1) & ~(cache_block_size - 1);
	return ch;
}

cache init_cache_memset(cache ch){
	ch.LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line) * ch.n_sets);
	ch.LRU_tail = (Pcache_line *)malloc(sizeof(Pcache_line) * ch.n_sets);
	ch.set_contents = (int *)malloc(sizeof(int) * ch.n_sets);

	memset(ch.LRU_head, 0, sizeof(Pcache_line) * ch.n_sets);
	memset(ch.LRU_tail, 0, sizeof(Pcache_line) * ch.n_sets);
	memset(ch.set_contents, 0, sizeof(int) * ch.n_sets);
	return ch;
}

void init_cache()
{
  /* initialize the cache, and cache statistics data structures */
	cache u_i_cache, d_cache;
	if (cache_split) {
		u_i_cache.size = cache_isize;
		d_cache.size = cache_dsize;
		
		u_i_cache = init_cache_helper(u_i_cache);
		u_i_cache = init_cache_memset(u_i_cache);
		
		d_cache = init_cache_helper(d_cache);
		d_cache = init_cache_memset(d_cache);
	} else {
		u_i_cache.size = d_cache.size = cache_usize;
		u_i_cache = init_cache_helper(u_i_cache);
		u_i_cache = init_cache_memset(u_i_cache);
		
		d_cache = init_cache_helper(d_cache);
		d_cache.LRU_head = u_i_cache.LRU_head;
		d_cache.LRU_tail = u_i_cache.LRU_tail;
		d_cache.set_contents = u_i_cache.set_contents;
	}
	c1 = u_i_cache;
	c2 = d_cache;
}

void calcDataLoadRefs(int d_isHit, int d_index, int d_tag){
	Pcache_line n_line, looping;
	n_line = (Pcache_line)malloc(sizeof(cache_line));
	memset(n_line, 0, sizeof(cache_line));
	if (d_isHit != 0) {
		// hit possible
		bool isFound = false;
		looping = (Pcache_line)c2.LRU_head[d_index];
		while (looping) {
			if (d_tag == looping->tag) {
				isFound = 1;
				break;
			}
			looping = looping->LRU_next;
		}
		if (isFound) {
			if (d_isHit > 1) {
				// keeping the least recently used refernece at the top
				delete(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], looping);
				insert(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], looping);
			}
		} else {
			if (d_isHit < cache_assoc) {
				// when the item can be insertable
				insert(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], n_line);
				if (cache_writeback && n_line->dirty){
					cache_stat_data.copies_back += (cache_block_size>>2);
					n_line->dirty = 0;
				}
				n_line->tag = d_tag;
				cache_stat_data.demand_fetches += (cache_block_size>>2);
				c2.set_contents[d_index]++;
				cache_stat_data.misses++;
			} else {
				// the item is not insertable therefore replacing in LRU is needed
				int old_dirty = c2.LRU_tail[d_index]->dirty;
				delete(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], c2.LRU_tail[d_index]);
				insert(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], n_line);
				if (cache_writeback && old_dirty){
					cache_stat_data.copies_back += (cache_block_size>>2);
					n_line->dirty = 0;
				}
				cache_stat_data.demand_fetches += (cache_block_size>>2);
				cache_stat_data.replacements++;
				cache_stat_data.misses++;
				n_line->tag = d_tag;
			}
		}
	} else {
		// a miss and insert
		insert(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], n_line);
		n_line->tag = d_tag;
		cache_stat_data.demand_fetches += (cache_block_size>>2);
		c2.set_contents[d_index]++;
		cache_stat_data.misses++;
	}
	cache_stat_data.accesses++;
}

void calcInsLoadRefs(int u_i_isHit, int u_i_index, int u_i_tag){
	Pcache_line n_line, looping;
	n_line = (Pcache_line)malloc(sizeof(cache_line));
	memset(n_line, 0, sizeof(cache_line));
	
	if (u_i_isHit != 0) {
		// Hit posibility
		bool isFound = false;
		looping = (Pcache_line)c1.LRU_head[u_i_index];
		while (looping) {
			if (u_i_tag == looping->tag) {
				isFound = 1;
				break;
			}
			looping = looping->LRU_next;
		}
		if (isFound) {
			if (u_i_isHit > 1) {
				// keeping the last used reference at the top and delete the older one.
				delete(&c1.LRU_head[u_i_index], &c1.LRU_tail[u_i_index], looping);
				insert(&c1.LRU_head[u_i_index], &c1.LRU_tail[u_i_index], looping);
			}
		} else {
			if (u_i_isHit < cache_assoc) {
				insert(&c1.LRU_head[u_i_index], &c1.LRU_tail[u_i_index], n_line);
				if (cache_writeback && n_line->dirty){
					cache_stat_data.copies_back += (cache_block_size>>2);
					n_line->dirty= 0;
				}
				n_line->tag = u_i_tag;
				cache_stat_inst.demand_fetches += (cache_block_size>>2);
				cache_stat_inst.misses++;
				c1.set_contents[u_i_index]++;
			} else {
				// deletaing the tail as it is not insertable, LRU replacement required
				int old_dirty = c1.LRU_tail[u_i_index]->dirty;
				delete(&c1.LRU_head[u_i_index], &c1.LRU_tail[u_i_index], c1.LRU_tail[u_i_index]);
				insert(&c1.LRU_head[u_i_index], &c1.LRU_tail[u_i_index], n_line);
				if (cache_writeback && old_dirty){
					cache_stat_data.copies_back += (cache_block_size>>2);
					n_line->dirty= 0;
				}
				n_line->tag = u_i_tag;
				cache_stat_inst.demand_fetches += (cache_block_size>>2);
				cache_stat_inst.replacements++;
				cache_stat_inst.misses++;
			}
		}
	} else {
		// a miss
		insert(&c1.LRU_head[u_i_index], &c1.LRU_tail[u_i_index], n_line);
		n_line->tag = u_i_tag;

		cache_stat_inst.demand_fetches += (cache_block_size>>2);
		c1.set_contents[u_i_index]++;
		cache_stat_inst.misses++;
	}
	cache_stat_inst.accesses++;
}

void calcDataStoreRefs(int d_isHit, int d_index, int d_tag){
	Pcache_line n_line, looping;
	if (cache_writealloc) {
		n_line = (Pcache_line)malloc(sizeof(cache_line));
		memset(n_line, 0, sizeof(cache_line));
		if (d_isHit != 0) {
			// hit possibility
			bool isFound = false;
			looping = (Pcache_line)c2.LRU_head[d_index];
			while (looping) {
				if (d_tag == looping->tag) {
					isFound = true;
					break;
				}
				looping = looping->LRU_next;
			}
			if (isFound) {
				if (d_isHit > 1) {
					// keepting the most recently used reference at the froont and deleting the previous occurence.
					delete(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], looping);
					insert(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], looping);
				}
				// d wrt hit, write throught 1 word cache block
				if (cache_writeback) {
					looping->dirty = 1;
				}else {
					cache_stat_data.copies_back++;
					looping->dirty = 0;
				}
			} else {
				if (d_isHit < cache_assoc) {
					insert(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], n_line);
					c2.set_contents[d_index]++;
					if (!cache_writealloc) {
						cache_stat_data.copies_back++;
					} else {
						n_line->tag = d_tag;
						if (cache_writeback && n_line->dirty){
							cache_stat_data.copies_back += (cache_block_size>>2);
							n_line->dirty = 0;
						}
						else if (cache_writeback)
							n_line->dirty = 1;
						cache_stat_data.demand_fetches += (cache_block_size>>2);
					}
					if (!cache_writeback) {
						cache_stat_data.copies_back++;
					}
					cache_stat_data.misses++;
				} else {
					// replacing the LRU as it is not insertable
					int old_dirty = c2.LRU_tail[d_index]->dirty;
					delete(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], c2.LRU_tail[d_index]);
					insert(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], n_line);
					if (!cache_writealloc) {
						cache_stat_data.copies_back++;
					} else {
						n_line->tag = d_tag;
						if (cache_writeback && old_dirty){
							cache_stat_data.copies_back += (cache_block_size>>2);
							n_line->dirty = 0;
						}
						else if (cache_writeback)
							n_line->dirty = 1;
						cache_stat_data.demand_fetches += (cache_block_size>>2);
						cache_stat_data.replacements++;
					}
					if (!cache_writeback) {
						cache_stat_data.copies_back++;
					}
					cache_stat_data.misses++;
				}
			}
		} else {
			// misses, therefore need to insert
			insert(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], n_line);
			c2.set_contents[d_index]++;
			if (!cache_writealloc) {
				cache_stat_data.copies_back++;
			} else {
				n_line->tag = d_tag;
				if (cache_writeback)
					n_line->dirty = 1;
				cache_stat_data.demand_fetches += (cache_block_size>>2);
			}
			if (!cache_writeback)
				cache_stat_data.copies_back++;
			cache_stat_data.misses++;
		}
	} else {
		bool isFound = false;
		looping = (Pcache_line)c2.LRU_head[d_index];
		while (looping) {
			if (d_tag == looping->tag) {
				isFound = true;
				break;
			}
			looping = looping->LRU_next;
		}
		if (isFound) {
			if (d_isHit > 1) {
				delete(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], looping);
				insert(&c2.LRU_head[d_index], &c2.LRU_tail[d_index], looping);
			}
			// d wrt hit, write throught for 1 wrod cache block
			if (cache_writeback) {
				looping->dirty = 1;
			} else{
				cache_stat_data.copies_back++;
				looping->dirty = 0;
			}
		} else {
			if (!cache_writealloc) {
				cache_stat_data.copies_back++;
			} else {
				cache_stat_data.demand_fetches += (cache_block_size>>2);
			}
			if (!cache_writeback) {
				cache_stat_data.copies_back++;
			}
			cache_stat_data.misses++;
		}
	}
	cache_stat_data.accesses++;
}

void fetchInfo(int* tag, int* idx, int* h, unsigned addr, cache ch){
	int index_offset = (int) ceil(log_float(ch.n_sets)) + LOG2(cache_block_size);
	*tag = addr >> index_offset;
	*idx = ((addr & ch.index_mask) >> ch.index_mask_offset) % ch.n_sets;
	*h = ch.set_contents[*idx];
}

void perform_access(addr, access_type)
  unsigned addr, access_type;
{
  /* handle an access to the cache */
	int u_i_isHit = 0, d_isHit = 0;
	int u_i_idx = 0, d_idx = 0;
	int u_i_tag = 0, d_tag = 0;

	fetchInfo(&u_i_tag, &u_i_idx, &u_i_isHit, addr, c1);
	if (cache_split) {
		fetchInfo(&d_tag, &d_idx, &d_isHit, addr, c2);
	} else {
		d_tag = u_i_tag;
		d_idx = u_i_idx;
		d_isHit = u_i_isHit;
	}

	if(access_type == 0){
		calcDataLoadRefs(d_isHit, d_idx, d_tag);
	}
	else if (access_type == 1){
		calcDataStoreRefs(d_isHit, d_idx, d_tag);
	}
	else if (access_type == 2){
		calcInsLoadRefs(u_i_isHit, u_i_idx, u_i_tag);
	}
}

void flush_helper(cache* ch){
	Pcache_line looping;
	int i;
	for (i=0; i<ch->n_sets; i++) {
		looping = ch->LRU_head[i];
		while (looping) {
			if (looping->dirty){
				cache_stat_data.copies_back += (cache_block_size>>2);
				ch->LRU_head[i]->dirty= 0;
			}
			looping = looping->LRU_next;
		}
	}
}

void flush()
{
	flush_helper(&c1);
	flush_helper(&c2);
}

void delete(head, tail, item)
  Pcache_line *head, *tail;
  Pcache_line item;
{
  if (item->LRU_prev) {
    item->LRU_prev->LRU_next = item->LRU_next;
  } else {
    /* item at head */
    *head = item->LRU_next;
  }

  if (item->LRU_next) {
    item->LRU_next->LRU_prev = item->LRU_prev;
  } else {
    /* item at tail */
    *tail = item->LRU_prev;
  }
}
/************************************************************/

/************************************************************/
/* inserts at the head of the list */
void insert(head, tail, item)
  Pcache_line *head, *tail;
  Pcache_line item;
{
  item->LRU_next = *head;
  item->LRU_prev = (Pcache_line)NULL;

  if (item->LRU_next)
    item->LRU_next->LRU_prev = item;
  else
    *tail = item;

  *head = item;
}
/************************************************************/

/************************************************************/
void dump_settings()
{
  printf("*** CACHE SETTINGS ***\n");
  if (cache_split) {
    printf("  Split I- D-cache\n");
    printf("  I-cache size: \t%d\n", cache_isize);
    printf("  D-cache size: \t%d\n", cache_dsize);
  } else {
    printf("  Unified I- D-cache\n");
    printf("  Size: \t%d\n", cache_usize);
  }
  printf("  Associativity: \t%d\n", cache_assoc);
  printf("  Block size: \t%d\n", cache_block_size);
  printf("  Write policy: \t%s\n", 
	 cache_writeback ? "WRITE BACK" : "WRITE THROUGH");
  printf("  Allocation policy: \t%s\n",
	 cache_writealloc ? "WRITE ALLOCATE" : "WRITE NO ALLOCATE");
}
/************************************************************/

/************************************************************/
void print_stats()
{
  printf("\n*** CACHE STATISTICS ***\n");

  printf(" INSTRUCTIONS\n");
  printf("  accesses:  %d\n", cache_stat_inst.accesses);
  printf("  misses:    %d\n", cache_stat_inst.misses);
  if (!cache_stat_inst.accesses)
    printf("  miss rate: 0 (0)\n"); 
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
	 (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses,
	 1.0 - (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses);
  printf("  replace:   %d\n", cache_stat_inst.replacements);

  printf(" DATA\n");
  printf("  accesses:  %d\n", cache_stat_data.accesses);
  printf("  misses:    %d\n", cache_stat_data.misses);
  if (!cache_stat_data.accesses)
    printf("  miss rate: 0 (0)\n"); 
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
	 (float)cache_stat_data.misses / (float)cache_stat_data.accesses,
	 1.0 - (float)cache_stat_data.misses / (float)cache_stat_data.accesses);
  printf("  replace:   %d\n", cache_stat_data.replacements);

  printf(" TRAFFIC (in words)\n");
  printf("  demand fetch:  %d\n", cache_stat_inst.demand_fetches + 
	 cache_stat_data.demand_fetches);
  printf("  copies back:   %d\n", cache_stat_inst.copies_back +
	 cache_stat_data.copies_back);
}
/************************************************************/
