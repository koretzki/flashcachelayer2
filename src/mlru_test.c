#include <stdio.h>
#include "list.h"
#include "disksim_fcl_cache.h"

int main(){	
	struct cache_manager **mlru_manager;
	char str[128];	
	int i;
	int j;
	int lru_num = 10;

	mlru_manager = mlru_init("MLRU", lru_num, 500);
	
	for(i =0;i < 1000000;i++){
		struct lru_node *ln = NULL, *ln_new = NULL;
		listnode *node;
		unsigned int blkno = RND(700);
		
		mlru_search(mlru_manager, lru_num, blkno, 1, 1, NULL);
	}	

	mlru_exit(mlru_manager, lru_num);


	return 0;
}

