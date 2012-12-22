    
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

struct fcl_mapping_table fcl_map[FCL_MAX_MAPTABLE];

int reverse_get_blk(int devno, int ssdblk){	
	struct fcl_mapping_table *fm = &fcl_map[devno];
	return fm->fm_reverse_map[ssdblk];
}

void reverse_map_create(int devno,int max){
	struct fcl_mapping_table *fm = &fcl_map[devno];
	int i;

	memset ( fm, 0x00, sizeof ( struct fcl_mapping_table ) );

	fm->fm_reverse_max_pages = max;

	fm->fm_reverse_map = (int *)malloc(sizeof(int) * fm->fm_reverse_max_pages);
	if(fm->fm_reverse_map == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(0);
	}

	for(i = 0;i < fm->fm_reverse_max_pages;i++){
		fm->fm_reverse_map[i] = MAP_TRIMMED;
	}
	fm->fm_reverse_used = 0;
	fm->fm_reverse_free = fm->fm_reverse_max_pages-1;
	fm->fm_reverse_alloc = 1;

	fprintf(stdout, " Reverse Map Allocation = %.2fKB\n", (double)sizeof(int)*fm->fm_reverse_max_pages/1024);

	ll_create(&fm->fm_reverse_freeq);
}




int reverse_map_alloc_blk(int devno, int hdd_blk){
	struct fcl_mapping_table *fm = &fcl_map[devno];
	int i;
	int alloc_blk = -1;

	
	if(ll_get_size(fm->fm_reverse_freeq)){
		fm->fm_reverse_alloc = (int)ll_get_tail(fm->fm_reverse_freeq);		
		ll_release_tail(fm->fm_reverse_freeq);
	}

	for(i = 1;i < fm->fm_reverse_max_pages;i++){

		if( fm->fm_reverse_map[fm->fm_reverse_alloc] < 0 ){
			alloc_blk = fm->fm_reverse_alloc;
			break;
		}

		fm->fm_reverse_alloc++;
		if(fm->fm_reverse_alloc == fm->fm_reverse_max_pages)
			fm->fm_reverse_alloc = 1;
	}


	if(alloc_blk > 0){
		fm->fm_reverse_free--;
		if(fm->fm_reverse_free < 0){
			fprintf(stderr, " check reverse_map_alloc_blk \n");
			fm->fm_reverse_free = fm->fm_reverse_free;
		}
		fm->fm_reverse_used++;

		if ( fm->fm_reverse_map[fm->fm_reverse_alloc] == MAP_RELEASED )
			fm->fm_reverse_wait_pages --;

		ASSERT ( fm->fm_reverse_wait_pages >= 0 );

		fm->fm_reverse_map[fm->fm_reverse_alloc] = hdd_blk;
	}


	if(alloc_blk == -1){
		//printf( " Cannot allocate block .. free num = %d, wait num = %d\n", fm_reverse_free, fm_reverse_wait_pages);
		printf( " Cannot allocate block .. \n");
		//ASSERT ( alloc_blk != -1 );
	}


	//if(fm_reverse_free == 0){
	//	fm_reverse_free = reverse_free;
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


int reverse_map_release_blk(int devno, int ssd_blk){
	struct fcl_mapping_table *fm = &fcl_map[devno];
	int i;	

	
	if(ssd_blk < 1 || ssd_blk >= fm->fm_reverse_max_pages){
		fprintf(stderr, " invalid ssd blkno = %d \n", ssd_blk);
		return -1;
	}


	fm->fm_reverse_free++;
	fm->fm_reverse_used--;
	fm->fm_reverse_map[ssd_blk] = MAP_RELEASED;
	fm->fm_reverse_alloc = ssd_blk;

	fm->fm_reverse_wait_pages ++;

	ll_insert_at_head(fm->fm_reverse_freeq, (void *)ssd_blk);

	return ssd_blk;
}

void reverse_map_discard_freeblk (int devno) {
	struct fcl_mapping_table *fm = &fcl_map[devno];

	listnode *del_node;
	int freecount = ll_get_size ( fm->fm_reverse_freeq );
	int i;
	int ssd_page_size;
	ssd_t *currssd = getssd (SSD);
	ssd_page_size = currssd->params.page_size;

	if ( fm->fm_reverse_wait_pages <= 0 )
		return; 

	del_node = fm->fm_reverse_freeq->next;
	for ( i = 0; i < freecount; i ++ ) {
		int blkno = (int) del_node->data;
		
		if ( fm->fm_reverse_map [ blkno ] == MAP_RELEASED ) {
			if ( ssd_page_size == 8) { // 4kb
				ssd_trim_command ( SSD, (int) blkno * FCL_PAGE_SIZE );
				fm->fm_reverse_map [ blkno ] = MAP_TRIMMED;
				fm->fm_reverse_wait_pages --;
				ASSERT ( fm->fm_reverse_wait_pages >= 0 );
			} else {
				int high_sector, low_sector;
				int high_page, low_page;

				low_sector = 16 * ( blkno/16 ) ;
				high_sector = low_sector + 8 ;

				low_page = low_sector / FCL_PAGE_SIZE ;
				high_page = high_sector / FCL_PAGE_SIZE ;


				if ( fm->fm_reverse_map [ low_page ] == MAP_RELEASED &&
					fm->fm_reverse_map [ high_page ] == MAP_RELEASED ) {

					ssd_trim_command ( devno, (int) low_sector ) ;
					fm->fm_reverse_map [ low_page ] = MAP_TRIMMED;
					fm->fm_reverse_map [ high_page ] = MAP_TRIMMED;
					fm->fm_reverse_wait_pages --;
					fm->fm_reverse_wait_pages --;
				}
			}
		}

		del_node = del_node->next;
	}

}
void reverse_map_free(int devno){
	struct fcl_mapping_table *fm = &fcl_map[devno];

	free(fm->fm_reverse_map);
	ll_release(fm->fm_reverse_freeq);
}

