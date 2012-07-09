#include <stdio.h>
#include "list.h"
#include "disksim_fcl_cache.h"
#include "disksim_fcl.h"

int main(){	
	struct cache_manager *lru_manager;
	int i;

	lru_init(&lru_manager,"LRU", 500, 500, 1, 0);

	for(i =0;i < 1000000;i++){
		struct lru_node *ln = NULL;
		unsigned int blkno = RND(10000);

		ln = CACHE_SEARCH(lru_manager, blkno);
		if(!ln){
			
			ln = CACHE_REPLACE(lru_manager, 0, FCL_REPLACE_ANY);
			ln = CACHE_ALLOC(lru_manager, ln, blkno);
			CACHE_INSERT(lru_manager, ln);
		}else{
			ln = CACHE_REMOVE(lru_manager, ln);
			CACHE_INSERT(lru_manager, ln);
		}
	}	

	CACHE_PRINT(lru_manager, stdout);
	CACHE_CLOSE(lru_manager);

	return 0;
}

