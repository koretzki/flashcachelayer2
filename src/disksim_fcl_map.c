    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: The mapping table
* File Name		: disksim_fcl_map.c
*/

#include "disksim_iosim.h"
#include "modules/modules.h"
#include "disksim_fcl.h"
#include "disksim_ioqueue.h"
#include "disksim_fcl_cache.h"
#include "disksim_fcl_map.h"
#include "../ssdmodel/ssd.h"

#define MAP_TRIMMED -1
#define MAP_RELEASED -2

static int *reverse_map;
static int reverse_free = 0;
static int reverse_used = 0;
static int reverse_alloc = 0;
static int reverse_max_pages = 0;
static int reverse_wait_pages = 0;
listnode *reverse_freeq;


int reverse_get_blk(int ssdblk){	
	return reverse_map[ssdblk];
}

void reverse_map_create(int max){
	int i;

	reverse_max_pages = max;

	reverse_map = (int *)malloc(sizeof(int) * reverse_max_pages);
	if(reverse_map == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(0);
	}

	for(i = 0;i < reverse_max_pages;i++){
		reverse_map[i] = MAP_TRIMMED;
	}
	reverse_used = 0;
	reverse_free = reverse_max_pages-1;
	reverse_alloc = 1;

	fprintf(stdout, " Reverse Map Allocation = %.2fKB\n", (double)sizeof(int)*reverse_max_pages/1024);

	ll_create(&reverse_freeq);
}




int reverse_map_alloc_blk(int hdd_blk){
	int i;
	int alloc_blk = -1;

	
	if(ll_get_size(reverse_freeq)){
		reverse_alloc = (int)ll_get_tail(reverse_freeq);		
		ll_release_tail(reverse_freeq);
	}

	for(i = 1;i < reverse_max_pages;i++){

		if( reverse_map[reverse_alloc] < 0 ){
			alloc_blk = reverse_alloc;
			break;
		}

		reverse_alloc++;
		if(reverse_alloc == reverse_max_pages)
			reverse_alloc = 1;
	}


	if(alloc_blk > 0){
		reverse_free--;
		if(reverse_free < 0){
			fprintf(stderr, " check reverse_map_alloc_blk \n");
			reverse_free = reverse_free;
		}
		reverse_used++;

		if ( reverse_map[reverse_alloc] == MAP_RELEASED )
			reverse_wait_pages --;

		ASSERT ( reverse_wait_pages >= 0 );

		reverse_map[reverse_alloc] = hdd_blk;
	}


	if(alloc_blk == -1){
		printf( " Cannot allocate block .. free num = %d, wait num = %d\n", reverse_free, reverse_wait_pages);
		printf( " Cannot allocate block .. \n");
		//ASSERT ( alloc_blk != -1 );
	}


	//if(reverse_free == 0){
	//	reverse_free = reverse_free;
	//}

	//fprintf ( stdout, " Revermap Alloc = %d \n", alloc_blk);

	//if(alloc_blk == 3866)
	//	alloc_blk = alloc_blk;
	return alloc_blk;
}


int reverse_map_check_sync(int hdd_blk){
	//int i;
	
	return 0;
}


int reverse_map_release_blk(int ssd_blk){
	int i;	

	
	if(ssd_blk < 1 || ssd_blk >= reverse_max_pages){
		fprintf(stderr, " invalid ssd blkno = %d \n", ssd_blk);
		return -1;
	}

	//if(ssd_blk == 9630){
	//		printf(" release = 9630\n");
	//}
//	ssd_trim_command(SSD, ssd_blk * BLOCK2SECTOR);


	reverse_free++;
	reverse_used--;
	reverse_map[ssd_blk] = MAP_RELEASED;
	reverse_alloc = ssd_blk;

	reverse_wait_pages ++;

	ll_insert_at_head(reverse_freeq, (void *)ssd_blk);

	return ssd_blk;
}

void reverse_map_discard_freeblk () {
	listnode *del_node;
	int freecount = ll_get_size ( reverse_freeq );
	int i;

	if ( reverse_wait_pages <= 0 )
		return; 

	del_node = reverse_freeq->next;
	for ( i = 0; i < freecount; i ++ ) {
		int blkno = (int) del_node->data;
		
		if ( reverse_map [ blkno ] == MAP_RELEASED ) {
			ssd_trim_command ( SSD, (int) blkno * FCL_PAGE_SIZE );
			reverse_map [ blkno ] = MAP_TRIMMED;
			reverse_wait_pages --;
			ASSERT ( reverse_wait_pages >= 0 );
		}

		del_node = del_node->next;
	}

}
void reverse_map_free(){
	free(reverse_map);
	ll_release(reverse_freeq);
}

