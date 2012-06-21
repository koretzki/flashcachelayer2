    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: 
*
*/

#include "disksim_iosim.h"
#include "modules/modules.h"
#include "disksim_fcl.h"
#include "disksim_ioqueue.h"
#include "disksim_fcl_cache.h"
#include "disksim_fcl_map.h"

/* Global variables */ 
struct ioq			 *fcl_global_queue = NULL;
struct cache_manager *fcl_cache_manager;
int 				 fcl_opid = 0;

int 				 flash_max_pages = 50000;
int 				 flash_max_sectors = 50000 * FCL_PAGE_SIZE;
int 				 hdd_max_pages = 50000;
int 				 hdd_max_sectors = 50000 * FCL_PAGE_SIZE;

#define fprintf 


ioreq_event *fcl_create_child (ioreq_event *parent, int devno, int blkno, int bcount, unsigned int flags){
	ioreq_event *child = NULL;

	child = ioreq_copy ( parent );
//	child  = (ioreq_event *) getfromextraq(); // DO NOT Use !!	

	child->time = parent->time;
	child->devno = devno;
	child->blkno = blkno;
	child->bcount = bcount;
	child->flags = flags;
	child->fcl_event_next = NULL;

	return child;
}

void fcl_attach_child (ioreq_event **fcl_event_list, int *fcl_event_count, int list_index,  ioreq_event *child){
	ioreq_event *last;

	int debug_count = 0;

	last = fcl_event_list[list_index];

	if ( last == NULL ) {
		fcl_event_list[list_index]= child;
	}else{
		while ( last->fcl_event_next != NULL ){
			last = last->fcl_event_next;
		}	

		if (!(child->devno == last->devno && child->flags == last->flags)) {
			printf(" list index = %d, dev = %d, flags = %d, %d %d \n", list_index,
					child->devno, child->flags, last->devno, last->flags);

		}
		ASSERT ( child->devno == last->devno && child->flags == last->flags );

		last->fcl_event_next = child;
	}

	child->fcl_event_next = NULL;
	//child->fcl_parent = parent;
	//child->fcl_event_ptr = list_index;

	fcl_event_count[list_index] += 1;

	/* Debug */ 
	last = fcl_event_list[list_index];

	fprintf ( stdout, " Debug print \n" );

	while ( last != NULL ) {
		fprintf ( stdout, " List[%d]: %p %d %d dev = %d, flag = %d  \n", list_index, last, last->blkno, 
							last->bcount, last->devno, last->flags );
		last = last->fcl_event_next;
		debug_count ++;
	}

	ASSERT ( debug_count == fcl_event_count[list_index] );
}

void fcl_parent_init (ioreq_event *parent) {
	int i; 

	fcl_opid ++; 

	parent->opid = fcl_opid;
	parent->fcl_event_ptr = 0;
	parent->fcl_event_num = 0;

	for ( i = 0; i < FCL_EVENT_MAX; i++) {
		parent->fcl_event_count[i] = 0;
		parent->fcl_event_list[i] = NULL;
	}

}

void fcl_issue_next_child ( ioreq_event *parent ){
	ioreq_event *req;
	int flags = -1;
	int devno = -1;

	fprintf ( stdout, " issue next = %d \n", parent->fcl_event_ptr );

	req = parent->fcl_event_list[parent->fcl_event_ptr];

	ASSERT ( parent->fcl_event_count[parent->fcl_event_ptr] != 0 );
	ASSERT ( req != NULL);

	flags = req->flags;
	devno = req->devno;

	while ( req != NULL ){
		
		ASSERT ( req->flags == flags && req->devno == devno );
		fprintf ( stdout, " req blkno = %d, dev = %d, bcount = %d \n", req->blkno, req->devno, req->bcount);

		addtointq((event *) req);

		if ( req ) {
			flags = req->flags;
			devno = req->devno;
		}

		req = req->fcl_event_next;

	}

	parent->fcl_event_ptr++;

}

void fcl_generate_child_request ( ioreq_event *parent, int devno, int blkno, int flags, int list_index )
{
	ioreq_event *child = NULL;

	ASSERT ( list_index != FCL_EVENT_MAX );
	if ( devno == SSD ) {
		blkno = blkno * FCL_PAGE_SIZE;
	}	

	child = fcl_create_child (  parent, 
								devno, 
								blkno, 
								FCL_PAGE_SIZE, 
								flags ); 

	child->devno = devno;
	child->type = IO_REQUEST_ARRIVE2;
	child->time = simtime + 0.000;
	child->fcl_parent = parent;

	fcl_attach_child (  parent->fcl_event_list, 
						parent->fcl_event_count, 
						list_index,
						child ); 
}

void fcl_distribute_request (ioreq_event *parent, int blkno) {
	int	list_index = 0;
	int	devno = 0;

	struct lru_node *ln = NULL;
	listnode *node;

	node = CACHE_SEARCH(fcl_cache_manager, blkno);

	// hit case  
	if(node){
		// remove this node to move the MRU position
		ln = CACHE_REMOVE(fcl_cache_manager, node);


		// TODO: this child request must be blocked  
		if ( ln->cn_flag == FCL_CACHE_FLAG_FILLING ) {
			ASSERT ( ln->cn_flag == FCL_CACHE_FLAG_SEALED );	

		}

	}else{ // miss case 
		// evict the LRU position node from the LRU list
		ln = CACHE_REPLACE(fcl_cache_manager, 0);

		if ( ln && ln->cn_ssd_blk > 0 ) {
			// move dirty data from SSD to HDD
			if ( ln->cn_dirty ) {
				fcl_generate_child_request ( parent, SSD, ln->cn_ssd_blk, READ, 0);
				fcl_generate_child_request ( parent, HDD, reverse_get_blk(ln->cn_ssd_blk), WRITE, 1);
				fcl_cache_manager->cm_destage_count++;
			}
			reverse_map_release_blk ( ln->cn_ssd_blk );
		}

		ln = CACHE_ALLOC(fcl_cache_manager, ln, blkno);
		ln->cn_flag = FCL_CACHE_FLAG_FILLING;
		ln->cn_ssd_blk = reverse_map_alloc_blk( blkno );

		// miss penalty request  
		if ( parent->flags & READ ) { // read clean data 
			fcl_generate_child_request ( parent, HDD, blkno, READ, 2);
			fcl_generate_child_request ( parent, SSD, ln->cn_ssd_blk, WRITE, 3);
			fcl_cache_manager->cm_stage_count++;
		} else {
			ln->cn_dirty = 1;
		}
	}

	fcl_generate_child_request ( parent, SSD, ln->cn_ssd_blk, parent->flags, FCL_EVENT_MAX-1);

	CACHE_INSERT(fcl_cache_manager, ln);

}

void fcl_split_parent_request (ioreq_event *parent) {

	int page_count = parent->bcount / FCL_PAGE_SIZE;
	int i;
	int blkno;

	ASSERT ( parent->bcount % FCL_PAGE_SIZE == 0 );

	for (i = 0; i < page_count; i++){
		blkno = parent->blkno + i * FCL_PAGE_SIZE;
		fcl_distribute_request ( parent, blkno ); 

		//printf (" blkno = %d, bcount = %d \n", blkno, parent->bcount );
	} 

}


int fcl_req_is_consecutive( ioreq_event *req1, ioreq_event *req2 ){

	if ( req1->blkno + req1->bcount == req2->blkno ) {
		return 1;	
	}
	return 0;
		
}

void fcl_make_merge_next_request (ioreq_event **fcl_event_list, int *fcl_event_count) {
	ioreq_event *req;
	ioreq_event *merged_req;
	int 		i;

	for ( i = 0; i < FCL_EVENT_MAX; i++ ) {

		if ( fcl_event_count[i] == 0 )  {
			continue;
		}

		req = fcl_event_list[i];

		ASSERT ( req != NULL);

		while ( req != NULL ){

			if (req->fcl_event_next && 
				fcl_req_is_consecutive ( req, req->fcl_event_next )) {
				
				// assign next request to remove pointer
				merged_req = req->fcl_event_next;

				// merge two consecutive requests
				req->bcount += merged_req->bcount;	

				// remove next request 
				req->fcl_event_next = merged_req->fcl_event_next;

				addtoextraq((event *)merged_req);
				
				fcl_event_count[i] --;

			} else {
				req = req->fcl_event_next;
			}
		}
	}
}	

void fcl_make_child_request (ioreq_event *parent) {

	int i, j;

	fcl_split_parent_request ( parent );

	parent->fcl_event_ptr = 0;
	parent->fcl_event_num = 0;

	fprintf ( stdout, " Merge .. \n");

	// Before: [a][empty][empty][d][e]
	// After: [a][d][e][empty][empty]

	for ( i = 0; i < FCL_EVENT_MAX; i++){
		for ( j = 1; j < FCL_EVENT_MAX-i; j++) {

			if ( parent->fcl_event_count [j-1] == 0 ) {
				parent->fcl_event_count [j-1] = parent->fcl_event_count [j];
				parent->fcl_event_list [j-1] = parent->fcl_event_list [j];

				parent->fcl_event_count [j] = 0;
				parent->fcl_event_list [j] = NULL;
				fprintf ( stdout, " [%d] %p %d \n", j, 
						parent->fcl_event_list[j], parent->fcl_event_count[j]);
			}

		}
	}


	fprintf ( stdout, " Count .. \n");

	for ( i = 0; i < FCL_EVENT_MAX; i++){
		
		if ( parent->fcl_event_count [i] ) {
			parent->fcl_event_num ++ ;
			fprintf ( stdout, " [%d] %p %d \n", i, 
					parent->fcl_event_list[i], parent->fcl_event_count[i]);
		}
	}

	fcl_make_merge_next_request ( parent->fcl_event_list, parent->fcl_event_count );

	
}

void fcl_request_arrive (ioreq_event *parent){
	ioreq_event *child = NULL;
	ioreq_event *temp = NULL;

	fprintf ( stdout, " FCL Req Arrive time = %f, blkno = %d, bcount = %d \n", 
										simtime, parent->blkno, parent->bcount);

	parent->blkno = parent->blkno % (flash_max_sectors);

	parent->blkno = (parent->blkno / FCL_PAGE_SIZE) * FCL_PAGE_SIZE;
	if ( parent->bcount % FCL_PAGE_SIZE ) {
		parent->bcount += FCL_PAGE_SIZE - ( parent->bcount % FCL_PAGE_SIZE);
	}

	//printf ( " %d %d \n", parent->bcount, parent->blkno);

	ASSERT ( parent->blkno % FCL_PAGE_SIZE == 0 && 
			 parent->bcount % FCL_PAGE_SIZE == 0 );

	fcl_parent_init ( parent );
	
	// parent request will be splited and distributed into SSD and HDD 
	fcl_make_child_request ( parent );
	
	// issue requests to IODRIVER
	fcl_issue_next_child ( parent ); 

	// insert parent req into FCL Overall Queue 
	ioqueue_add_new_request ( fcl_global_queue, parent );
/* 
	printf ( " %f add new req .. \n", simtime );
	printf ( " Queue # of reqs = %d \n", ioqueue_get_number_of_requests ( fcl_global_queue ));
	printf ( " Queue # of outstanding reqs = %d \n", ioqueue_get_reqoutstanding ( fcl_global_queue ));
	printf ( " Queue # of reqs in queue = %d \n", ioqueue_get_number_in_queue ( fcl_global_queue ));
	printf ( " Queue # of pending reqs = %d \n", ioqueue_get_number_pending ( fcl_global_queue ));
	printf ( " Cost int arr time = %f \n", constintarrtime );
*/

	temp = ioqueue_get_next_request ( fcl_global_queue );
	ASSERT ( temp == parent );

/*
	printf ( " get next .. \n" );
	printf ( " Queue # of reqs = %d \n", ioqueue_get_number_of_requests ( fcl_global_queue ));
	printf ( " Queue # of outstanding reqs = %d \n", ioqueue_get_reqoutstanding ( fcl_global_queue ));
	printf ( " Queue # of reqs in queue = %d \n", ioqueue_get_number_in_queue ( fcl_global_queue ));
	printf ( " Queue # of pending reqs = %d \n", ioqueue_get_number_pending ( fcl_global_queue ));
*/


	//fflush ( stdout );
}
void fcl_seal_complete_request ( ioreq_event *parent ) {
	listnode *node;
	struct lru_node *ln;
	int page_count = parent->bcount / FCL_PAGE_SIZE;
	int i;
	int blkno;
	
	//printf ( " Complete ...%d %d %d \n", parent->bcount, parent->blkno, page_count );

	//ASSERT ( parent->bcount % FCL_PAGE_SIZE == 0 );

	for (i = 0; i < page_count; i++){
		blkno = parent->blkno + i * FCL_PAGE_SIZE;
		node = CACHE_PRESEARCH(fcl_cache_manager, blkno);


		//printf ( " seal blkno = %d, bcount = %d \n", blkno, parent->bcount );
		ASSERT ( node != NULL );
	
		//continue;
		if ( node ) {
			ln = ( struct lru_node *) node->data;
			//ASSERT ( ln->cn_flag == FCL_CACHE_FLAG_FILLING );
			ln->cn_flag = FCL_CACHE_FLAG_SEALED;

		}
	} 

}
void fcl_request_complete (ioreq_event *child){
	ioreq_event *parent, *req2;
	int total_req = 0;	
	int i;

	parent = (ioreq_event *)child->fcl_parent;

	//ASSERT ( parent->fcl_event_ptr-1 == child->fcl_event_ptr );

	//fprintf ( stdout, " Complete %d %d %d \n", parent->fcl_event_ptr-1, parent->fcl_event_count[parent->fcl_event_ptr-1],
	//		child->blkno);
	//fprintf ( stdout, " Num = %d \n",  parent->fcl_event_num );

	parent->fcl_event_count[parent->fcl_event_ptr-1] -- ;

	// next events are remaining. 
	if ( parent->fcl_event_count[parent->fcl_event_ptr-1] == 0 &&
			parent->fcl_event_ptr < parent->fcl_event_num ) 
	{
		//fprintf ( stdout, " **Issue next Requst .. \n" );
		fcl_issue_next_child ( parent );
	}

	addtoextraq ((event *) child);

	for ( i = 0; i < parent->fcl_event_num; i++) {
		total_req += parent->fcl_event_count[i];
	}

	// all child requests are complete 
	if ( total_req  == 0 ) { 
		fcl_seal_complete_request ( parent );
		req2 = ioqueue_physical_access_done (fcl_global_queue, parent);
		ASSERT (req2 != NULL);
		addtoextraq ((event *) parent);

		fprintf ( stdout, " ** Complete parent req ... %d %d %d \n", parent->blkno, parent->bcount, parent->flags);
	}

}

void fcl_init () {
	int lru_size = 50000;
	ssd_t *ssd = getssd ( SSD );
	ssd_t *hdd = getssd ( HDD );

	fprintf ( stdout, " Flash Cache Layer is initializing ... \n");

	flash_max_pages = ssd_elem_export_size2 ( ssd );
	flash_max_sectors = flash_max_pages * FCL_PAGE_SIZE;

	printf (" FCL: Flash Cache Usable Size = %.2fGB \n", (double)flash_max_pages / 256 / 1024);

	hdd_max_pages = ssd_elem_export_size2 ( hdd );
	hdd_max_sectors = hdd_max_pages * FCL_PAGE_SIZE;

	printf (" FCL: Hard Disk Usable Size = %.2fGB \n", (double)hdd_max_pages / 256 / 1024);

	lru_init ( &fcl_cache_manager, "LRU", lru_size, lru_size, 1, 0);
	reverse_map_create ( lru_size+1 );

	// alloc queue memory 
	//fcl_global_queue = malloc(sizeof(ioqueue));
	fcl_global_queue = ioqueue_createdefaultqueue();
	ioqueue_initialize (fcl_global_queue, 0);

	constintarrtime = 1.0;

}

void fcl_exit () {

	fprintf ( stdout, " Flash Cache Layer is finalizing ... \n"); 

	reverse_map_free();

	CACHE_CLOSE(fcl_cache_manager, 1);

	fcl_global_queue->printqueuestats = TRUE;
	ioqueue_printstats( &fcl_global_queue, 1, " FCL : ");

	// free queue memory
	free (fcl_global_queue);

}

/*
ioreq_event *fcl_make_child_request (ioreq_event *parent) {
	ioreq_event *child = NULL;
	int i, j;

	// make request list 
	for ( i = 0; i < FCL_EVENT_MAX-1; i++) { 
		for ( j = 0; j < 1; j++) {
			child = fcl_create_child ( parent, parent->devno, parent->blkno, parent->bcount, parent->flags ); 

			child->devno = i % 2;
			child->type = IO_REQUEST_ARRIVE2;
			child->time = simtime + 0.000;

			fcl_attach_child_to_parent ( parent, child ); 
		}

		parent->fcl_event_num++;
		parent->fcl_event_ptr++;

	}

	parent->fcl_event_ptr = 0;

}
*/


/*

	// make request list 
	for ( i = 0; i < FCL_EVENT_MAX-1; i++) { 
		for ( j = 0; j < 1; j++) {

			child = fcl_create_child ( parent, 
									   parent->devno, 
									   parent->blkno, 
									   parent->bcount, 
									   parent->flags ); 

			child->devno = i % 2;
			child->type = IO_REQUEST_ARRIVE2;
			child->time = simtime + 0.000;
			child->fcl_parent = parent;

			list_index = i;

			fcl_attach_child ( fcl_event_list, 
										 fcl_event_count, 
										 list_index,
										 child ); 
		}

		parent->fcl_event_num++;
		parent->fcl_event_ptr++;
	}
*/

/*
void fcl_request_complete (ioreq_event *child){
	ioreq_event *parent, *req2;
	
	parent = (ioreq_event *)child->fcl_parent;

	//fprintf ( stdout, " %d %d %d %d  \n", parent->blkno, child->blkno, parent->flags, child->flags);

	parent->fcl_req_num--;
	if (parent->fcl_req_num == 0 ) { 
		req2 = ioqueue_physical_access_done (fcl_global_queue, parent);
		ASSERT (req2 != NULL);
		addtoextraq ((event *) parent);
		exit(0);
	}

	child->tempptr2 = NULL;
	addtoextraq ((event *) child);

}
*/

/*
	parent = ioqueue_get_specific_request (fcl_global_queue, child);
	req2 = ioqueue_physical_access_done (fcl_global_queue, parent);
	ASSERT (req2 != NULL);
	addtoextraq ((event *) parent);
*/


#if 0 
void fcl_request_arrive (ioreq_event *parent){
	ioreq_event *child = NULL;
	ioreq_event *temp = NULL;

	fcl_opid ++; 
	parent->opid = fcl_opid;
	parent->fcl_req_num = 1;

	child = ioreq_copy (parent);

	child->fcl_parent = parent;
	child->type = IO_REQUEST_ARRIVE2;
	child->time = simtime + 0.0000;

	ioqueue_add_new_request (fcl_global_queue, parent);
	temp = ioqueue_get_next_request (fcl_global_queue);

	ASSERT ( temp == parent );

	addtointq((event *) child);

	//fprintf ( stdout, " %f next = %p, prev = %p \n", simtime, child->next, child->prev);
	//fprintf ( stdout, " fcl arrive %d, %p \n", child->blkno, child);

	return;
}
#endif 
