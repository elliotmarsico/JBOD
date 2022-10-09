#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 1;
static int num_queries = 0;
static int num_hits = 0;

bool isEmptyCache = false;
bool activeCache = false;


/* Returns 1 on success and -1 on failure. Should allocate a space for
 * |num_entries| cache entries, each of type cache_entry_t. Calling it again
 * without first calling cache_destroy (see below) should fail. */
int cache_create(int num_entries) {
  if((activeCache == true)||(num_entries < 2)||(num_entries > 4096)){
    return -1;
  }
  
  isEmptyCache = true;
  activeCache = true;
  cache = malloc(sizeof(cache_entry_t)*num_entries);//allocates enough memory for num_entries number of cache_entry_t
  cache_size = num_entries;
  
  return 1;
}

/* Returns 1 on success and -1 on failure. Frees the space allocated by
 * cache_create function above. */
int cache_destroy(void) {
  if(activeCache == false){
    return -1;
  }
  
  activeCache = false;
  free(cache);
  cache = NULL;
  cache_size = 0;
  
  return 1;
}

/*If cache is valid, finds the index of the least recently updated 
 *entry in the cache. Otherwise returns -1. */
int cache_find_LRU(){
  int lowestAccessTime = cache[0].access_time;
  int leastRecentlyUsedIndex = 0;
  for(int x = 0; x < cache_size; x++){
    if((cache[x].access_time < lowestAccessTime)){
      lowestAccessTime = cache[x].access_time;
      leastRecentlyUsedIndex = x;
    }
  }

  return leastRecentlyUsedIndex;
}

/* Returns 1 on success and -1 on failure. Looks up the block located at
 * |disk_num| and |block_num| in cache and if found, copies the corresponding
 * block to |buf|, which must not be NULL. */
int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  clock += 1;
  if((cache == NULL)||(isEmptyCache == true)){//empty cache failure
    return -1;
  }
  
  if((buf == NULL)||(activeCache == false)||(disk_num > 16)||(block_num > 256)||(disk_num < 0)||(block_num < 0)){//Invalid boundaries failure
    return -1;
  }
  
  num_queries += 1;
  
  for(int x = 0; x < cache_size; x++){
    if((cache[x].disk_num == disk_num)&&(cache[x].block_num == block_num)&&(cache[x].access_time != 0)){//Cache hit!
      memcpy(((char *)buf), cache[x].block, 256);
      cache[x].access_time = clock;
      num_hits += 1;
      return 1;
    }
  }

  return -1;
}

/* If the entry with |disk_num| and |block_num| exists, updates the
 * corresponding block with data from |buf| */
void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  clock += 1;
  if((buf == NULL)||(activeCache == false)||(disk_num > 16)||(block_num > 256)||(disk_num < 0)||(block_num < 0)){//Invalid boundaries failure
    return;
  }
  
  for(int x = 0; x < cache_size; x++){
    if((cache[x].disk_num == disk_num)&&(cache[x].block_num == block_num)){//Matching disk and block found, replace current data
      memcpy(&(cache[x].block), buf, 256);
      cache[x].access_time = clock;
      return;
    }
  }
}

/* Returns 1 if the given disk_num
 * and block_num is already an entry in the cache. */
int already_in_cache(int disk_num, int block_num){
  for(int x = 0; x < cache_size; x++){
    if((cache[x].disk_num == disk_num)&&(cache[x].block_num == block_num)&&(cache[x].valid == true)){
      if(cache[x].access_time == 0){
	return -1;
      }
      return 1;
    }
  }
  return -1;
}

/* Returns the earliest empty index in the cache.
 * Returns -1 if the cache is full. */
int first_empty_index(){
  for(int x = 0; x < cache_size; x++){
    if(cache[x].access_time == 0){
      return x;
    }
  }
  return -1;
}

/* Returns 1 on success and -1 on failure. Inserts an entry for |disk_num| and
 * |block_num| into cache. Returns -1 if there is already an existing entry in the cache
 * with |disk_num| and |block_num|.If there cache is full, should evict least
 * recently used entry and insert the new entry. */
int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  clock += 1;
  if((buf == NULL)||(activeCache == false)||(disk_num > 16)||(block_num > 256)||(disk_num < 0)||(block_num < 0)){//Invalid boundaries failure
    return -1;
  }  

  if(already_in_cache(disk_num, block_num) == 1){// Already in cache failure
    return -1;
  }

  int indexForInsertion = first_empty_index();
  if(indexForInsertion == -1){//cache is full
    indexForInsertion = cache_find_LRU();
  } 

  //fill entry with desired content
  cache[indexForInsertion].valid = true;
  cache[indexForInsertion].access_time = clock;
  cache[indexForInsertion].block_num = block_num;
  cache[indexForInsertion].disk_num = disk_num;
  memcpy(&(cache[indexForInsertion].block), buf, 256);
  
  isEmptyCache = false;
  return 1;
}

/* Returns true if cache is enabled and false if not. */
bool cache_enabled(void) {
  return activeCache;
}

/* Prints the hit rate of the cache. */
void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
