    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: The main source file of Flash Cache Layer
* File Name		: disksim_fcl.c 
*/

#include "disksim_iosim.h"
#include "modules/modules.h"
#include "disksim_fcl.h"
#include "disksim_ioqueue.h"
#include "disksim_fcl_cache.h"
#include "disksim_fcl_map.h"
#include "disksim_fcl_cost.h"
#include "disksim_fcl_seq_detect.h"

#include "../ssdmodel/ssd_clean.h"
#include "../ssdmodel/ssd_init.h"



/* Global variables */ 

struct ioq			 	*fcl_fore_q = NULL;
struct ioq			 	*fcl_back_q = NULL;

struct cache_manager	*fcl_cache_mgr;
struct cache_manager	*fcl_active_block_mgr;
struct cache_manager	*fcl_pending_mgr; 

struct cache_manager	**fcl_write_hit_tracker;
struct cache_manager	**fcl_read_hit_tracker;

struct fcl_parameters 	*fcl_params;
struct fcl_statistics	*fcl_stat;

void (*fcl_timer_func)(struct timer_ev *);

int 				 fcl_opid = 0;


// RW-FCL and OP-FCL
int					 fcl_optimal_read_pages = 0;
int					 fcl_optimal_write_pages = 0;
int					 fcl_resize_trigger = 0;

int					 debug_max_size = 0;


ioreq_event *fcl_create_child ( ioreq_event *parent, int devno, int blkno, int bcount, 
																	unsigned int flags )
{
	ioreq_event *child = NULL;

	//child = ioreq_copy ( parent );
	child  = (ioreq_event *) getfromextraq(); // DO NOT Use !!	

	child->time = simtime;
	child->devno = devno;
	child->blkno = blkno;
	child->bcount = bcount;
	child->flags = flags;
	child->fcl_event_next = NULL;

	child->buf = 0;
	child->busno = 0;
	child->cause = 0;
	
	return child;
}

void fcl_attach_child ( ioreq_event **fcl_event_list, int *fcl_event_count, int list_index, 
																	ioreq_event *child )
{
	ioreq_event *last;

	int debug_count = 0;

	last = fcl_event_list[list_index];


	if ( last == NULL ) {
		fcl_event_list[list_index]= child;
	}else{

		while ( last->fcl_event_next != NULL ){
			last = last->fcl_event_next;
		}	


		//if (!(child->devno == last->devno && child->flags == last->flags)) {
		//	printf(" list index = %d, dev = %d, flags = %d, %d %d \n", list_index,
		//			child->devno, child->flags, last->devno, last->flags);
		//} 

		ASSERT ( child->devno == last->devno && child->flags == last->flags );

		last->fcl_event_next = child;
	}

	child->fcl_event_next = NULL;
	//child->fcl_parent = parent;
	//child->fcl_event_ptr = list_index;

	fcl_event_count[list_index] += 1;

#if 0 
	/* Debug */ 
	last = fcl_event_list[list_index];

	//printf ( " Debug print \n" );

	while ( last != NULL ) {
	//	printf ( " List[%d]: %p %d %d dev = %d, flag = %d  \n", list_index, last, last->blkno, 
	//						last->bcount, last->devno, last->flags );
		last = last->fcl_event_next;
		debug_count ++;
	}

	ASSERT ( debug_count == fcl_event_count[list_index] );
#endif 

}

void fcl_parent_init ( ioreq_event *parent ) {
	int i; 

	fcl_opid ++; 

	parent->opid = fcl_opid;
	parent->fcl_event_ptr = 0;
	parent->fcl_event_num = 0;

	//parent->tempint1 = 0;
	//parent->tempint2 = 0;

	for ( i = 0; i < FCL_EVENT_MAX; i++) {
		parent->fcl_event_count[i] = 0;
		parent->fcl_event_list[i] = NULL;
	}
	
	INIT_LIST_HEAD ( &parent->fcl_complete_list ) ;
	INIT_LIST_HEAD ( &parent->fcl_active_list );
	INIT_LIST_HEAD ( &parent->fcl_inactive_list );
	INIT_LIST_HEAD ( &parent->fcl_pending_list );

}

void fcl_parent_release ( ioreq_event *parent ) {

	fcl_remove_complete_list ( parent );

	ASSERT ( list_empty ( &parent->fcl_complete_list ) );
	ASSERT ( list_empty ( &parent->fcl_active_list ) );
	ASSERT ( list_empty ( &parent->fcl_inactive_list ) );
	ASSERT ( list_empty ( &parent->fcl_pending_list ) );


}
void fcl_issue_next_child ( ioreq_event *parent ){
	ioreq_event *req;
	int flags = -1;
	int devno = -1;
	double delay = 0.05;

	//fprintf ( stdout, " issue next = %d, blkno = %d, opid = %d \n", parent->fcl_event_ptr, parent->blkno, parent->opid );

	ASSERT ( parent->fcl_event_count[parent->fcl_event_ptr] != 0 );

	req = parent->fcl_event_list[parent->fcl_event_ptr];
	ASSERT ( req != NULL);

	flags = req->flags;
	devno = req->devno;

	while ( req != NULL ){
		
		ASSERT ( req->flags == flags && req->devno == devno );
		//fprintf ( stdout, " req blkno = %d, dev = %d, bcount = %d \n", req->blkno, req->devno, req->bcount);


		if ( req->bcount >= FCL_MAX_REQ_SIZE  && req->devno == HDD ) {
			debug_max_size = req->bcount;
			///printf (" simtime = %f, maxsize = %d, devno = %d  blkno = %d \n", simtime, debug_max_size, req->devno, req->blkno  );
		}

		ASSERT ( req->bcount <= FCL_MAX_REQ_SIZE );

		req->time = simtime;

		addtointq((event *) req);

		if ( req ) {
			flags = req->flags;
			devno = req->devno;
		}

		req = req->fcl_event_next;

	}

	parent->fcl_event_list[parent->fcl_event_ptr] = NULL;
	parent->fcl_event_ptr++;

}

void fcl_generate_child_request ( ioreq_event *parent, int devno, int blkno, int flags, 
															 int list_index, int data_class )
{
	ioreq_event *child = NULL;

	ASSERT ( list_index < FCL_EVENT_MAX );

	if ( devno == SSD ) {
		blkno = blkno * FCL_PAGE_SIZE;
	}	

	child = fcl_create_child (  parent, 
								devno, 
								blkno, 
								FCL_PAGE_SIZE, 
								flags ); 

	if ( ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_RW || 
		   fcl_params->fpa_partitioning_scheme == FCL_CACHE_OPTIMAL) 
		&& devno == SSD
		) 
	{
		child->fcl_data_class = data_class; 
	} else {
		child->fcl_data_class = 0; 
	}

	child->devno = devno;
	child->type = IO_REQUEST_ARRIVE;
	child->time = simtime + 0.000;
	child->fcl_parent = parent;

	fcl_attach_child (  parent->fcl_event_list, 
						parent->fcl_event_count, 
						list_index,
						child ); 
}



int _fcl_replace_cache ( ioreq_event *parent, int watermark, int replace_type ) {
	struct lru_node *remove_ln;
	struct lru_node *active_ln;
	int victim = 0;

	// evict the LRU position node from the LRU list
	remove_ln = CACHE_REPLACE(fcl_cache_mgr, watermark, replace_type);

	if ( remove_ln == NULL ) 
		return victim;

	if ( remove_ln ) {
		ASSERT ( remove_ln->cn_flag == FCL_CACHE_FLAG_SEALED );
	}

	// XXX : active block would be replaced !! 
	active_ln = fcl_lookup_active_list ( remove_ln->cn_blkno );
	if ( active_ln && active_ln->cn_time != simtime ) {
		printf ( " Queue length = %d \n", ioqueue_get_number_in_queue ( fcl_fore_q )) ; 
		ASSERT ( 0 );
	}			
	ASSERT ( active_ln == NULL );

	// move dirty data from SSD to HDD
	if ( remove_ln->cn_dirty ) {
		_fcl_make_destage_req ( parent, remove_ln, 0 );
		ASSERT ( fcl_cache_mgr->cm_dirty_count >= 0 );
	}

	// There are available clean pages 
	if ( fcl_cache_mgr->cm_clean_free > 1 && 
		 fcl_params->fpa_partitioning_scheme != FCL_CACHE_FIXED ) 
	{ 
		// move dirty page to clean list 
		remove_ln->cn_dirty = 0;
		CACHE_INSERT ( fcl_cache_mgr, remove_ln );

	} else {

		reverse_map_release_blk ( SSD, remove_ln->cn_ssd_blk );
		free( remove_ln );

	}

	victim = 1;


	return victim;
}




struct lru_node * fcl_replace_cache (ioreq_event *parent, int blkno, struct lru_node *ln) { 	
	int replace_type; //  = (parent->flags & READ) ? 0 : 1;
	int low_watermark = 0;
	int debug = 0;

	if ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_FIXED ) 
		replace_type = FCL_REPLACE_ANY;
	else // FCL_CACHE_RW, FCL_CACHE_FIXED
		replace_type = parent->flags & READ;


	if ( !( parent->flags & READ )  			// Write Request 
		&& replace_type != FCL_REPLACE_ANY		// Dynamic Partitioning 
		&& fcl_cache_mgr->cm_dirty_free == 0	// High Watermark
		&& fcl_params->fpa_ondemand_group_destage )   
	{
		low_watermark = FCL_MAX_DESTAGE - 1;
		debug = 1;
	} else if ( !(parent->flags & READ ) 
			&& replace_type == FCL_REPLACE_ANY 
			&& fcl_cache_mgr->cm_free == 0 	 
			&& fcl_params->fpa_ondemand_group_destage )   
	{ 
		low_watermark = FCL_MAX_DESTAGE - 1;
	} else {
		low_watermark = 0;
	}

	while ( 1 ) {
		int victim;

		victim = _fcl_replace_cache ( parent, low_watermark, replace_type );
		if ( !victim ) {
			break;
		}

	}

	/*if ( debug ) 
		printf ( " Clean free = %d, dirty free = %d flags = %d \n", 
								 fcl_cache_mgr->cm_clean_free,
								 fcl_cache_mgr->cm_dirty_free,
								 parent->flags);
								 */

	if ( ln == NULL ) {
		ln = CACHE_ALLOC(fcl_cache_mgr, NULL, blkno);
		ln->cn_flag = FCL_CACHE_FLAG_FILLING;
		ln->cn_ssd_blk = reverse_map_alloc_blk( SSD, blkno );
		ln->cn_dirty = 0;

		ASSERT ( ln->cn_ssd_blk != -1 );
	} 

	// miss penalty request  
	if ( parent->flags & READ ) { // read clean data 
		_fcl_make_stage_req ( parent, ln, 2);
	}

	return ln;
}

void fcl_make_bypass_req (ioreq_event *parent, int blkno) {

	if ( parent->flags & READ ) {
		fcl_generate_child_request ( parent, HDD, blkno, READ, 0, 0);
	 } else  { 
		fcl_generate_child_request ( parent, HDD, blkno, WRITE, 1, 0);
	 }
}

void fcl_make_seq_req (ioreq_event *parent, int blkno) {
	struct lru_node *ln = NULL;
	int hit = 0;

	ln = CACHE_SEARCH(fcl_cache_mgr, blkno);

	// hit case  
	if(ln){

		if ( (parent->flags & READ) ) {
			if ( ln->cn_dirty ) {
				hit = 1;
				_fcl_make_destage_req ( parent, ln, 0 );
			} else {
				hit = 0;
			}
	//		//printf ( " Read Sequential I/O for dirty data in ssd cache \n" ) ;
		}

		ln = CACHE_REMOVE(fcl_cache_mgr, ln);
		reverse_map_release_blk ( SSD, ln->cn_ssd_blk );
		free ( ln );
	}

	if ( parent->flags & READ ) {
		if ( !hit ) { 
			fcl_generate_child_request ( parent, HDD, blkno, READ, FCL_EVENT_MAX - 2, 0);
		}
	 } else  { 
		fcl_generate_child_request ( parent, HDD, blkno, WRITE, FCL_EVENT_MAX - 1, 0);
	 }
}

void fcl_make_normal_req (ioreq_event *parent, int blkno) {
	struct lru_node *ln = NULL;
	int hit = 0;

	/* Virtual Device */

	ln = CACHE_SEARCH(fcl_cache_mgr, blkno);

	// hit case  
	if(ln){
		hit = 1;
		// remove this node to move the MRU position
		ln = CACHE_REMOVE(fcl_cache_mgr, ln);

		// TODO: this child request must be blocked  
		if ( ln->cn_flag == FCL_CACHE_FLAG_FILLING ) {
			ASSERT ( ln->cn_flag == FCL_CACHE_FLAG_SEALED );	
		}

		// move to dirty list with replacement 
		if ( !(parent->flags & READ) && ln->cn_dirty == 0 ) {
			//	printf ( " clean data to dirty data \n");
			ln = fcl_replace_cache ( parent, blkno, ln );
		}

	}else{ // miss case 
		ln = fcl_replace_cache ( parent, blkno, NULL );
		ASSERT ( ln );
	}

	if ( parent->flags & READ ) {
		if ( hit ) {
			fcl_generate_child_request ( parent, SSD, ln->cn_ssd_blk, 
										 READ, FCL_EVENT_MAX - 2, 0 );
		}
	} else {
		fcl_generate_child_request ( parent, SSD, ln->cn_ssd_blk, 
				 					 WRITE, FCL_EVENT_MAX - 1, WRITE );

		if ( ln->cn_dirty == 0 ) {
			ln->cn_dirty = 1;
		}
	}

	CACHE_INSERT(fcl_cache_mgr, ln);

}


void fcl_make_stage_req (ioreq_event *parent, int blkno) {
	int	list_index = 0;
	int	devno = 0;
	int filling = 0;
	struct lru_node *ln = NULL;

	ln = CACHE_PRESEARCH(fcl_cache_mgr, blkno);

	// hit case  
	if( ln ){
		//printf ( " Stagine Hit .. blkno = %d \n", blkno );
		ASSERT ( ln  == NULL );	

	}else{ // miss case 
		//printf ( " stage miss \n" );
		ln = fcl_replace_cache( parent, blkno, NULL );
	}
	
	CACHE_INSERT(fcl_cache_mgr, ln);

}

void _fcl_make_stage_req ( ioreq_event *parent, struct lru_node *ln, int list_index ) {

	fcl_generate_child_request ( parent, HDD, reverse_get_blk(SSD, ln->cn_ssd_blk), 
														READ, list_index++, 0);
	fcl_generate_child_request ( parent, SSD, ln->cn_ssd_blk, 
														WRITE, list_index++, READ);

	fcl_cache_mgr->cm_stage_count++;

}

void _fcl_make_destage_req ( ioreq_event *parent, struct lru_node *ln, int list_index ) {

	//printf(" make destage = %f %d\n", simtime, reverse_get_blk(ln->cn_ssd_blk));

	fcl_generate_child_request ( parent, SSD, ln->cn_ssd_blk, 
														READ, list_index++, 0);
	fcl_generate_child_request ( parent, HDD, reverse_get_blk( SSD, ln->cn_ssd_blk), 
														WRITE, list_index++, 0);

	fcl_cache_mgr->cm_destage_count++;

}

#if 0 
void fcl_dirty_sync () {

}
#endif 

void fcl_make_destage_req (ioreq_event *parent, int blkno, int replace) {
	int	list_index = 0;
	struct lru_node *ln = NULL;

	ln = CACHE_SEARCH(fcl_cache_mgr, blkno);

	// miss  case  
	ASSERT ( ln != NULL );

	if ( replace ) {
		// remove this node to move the MRU position
		ln = CACHE_REMOVE(fcl_cache_mgr, ln);

		// TODO: this child request must be blocked  
		if ( ln->cn_flag == FCL_CACHE_FLAG_FILLING ) {
			ASSERT ( ln->cn_flag == FCL_CACHE_FLAG_SEALED );	
		}
	}

	ASSERT ( ln->cn_dirty ) ;

	_fcl_make_destage_req ( parent, ln, 0 );

	if ( replace ) {
		reverse_map_release_blk ( SSD, ln->cn_ssd_blk );
		free ( ln );
	} else {
		ln->cn_dirty = 0;
		lru_move_clean_list ( fcl_cache_mgr, ln );
	}

}


struct lru_node *fcl_lookup_active_list ( int blkno ) {
	struct lru_node *node;

	node = CACHE_PRESEARCH( fcl_active_block_mgr, blkno );
	if ( node ) {
		return node;
	}
	return NULL;
}

void fcl_insert_active_list ( ioreq_event *child ) {
	struct lru_node *ln = NULL;	

	ln = CACHE_ALLOC ( fcl_active_block_mgr, NULL, child->blkno );

	ln->cn_flag = 0;
	ln->cn_temp1 = (void *)child;
	ln->cn_time	= simtime;

	ll_create ( (listnode **) &ln->cn_temp2 );

	CACHE_INSERT ( fcl_active_block_mgr, ln );
}

void fcl_insert_inactive_list ( ioreq_event *child ) {
	struct lru_node *ln = NULL;	

	ln = CACHE_PRESEARCH ( fcl_active_block_mgr, child->blkno );

	ll_insert_at_tail ( ln->cn_temp2, child );
}

void fcl_classify_child_request ( ioreq_event *parent, ioreq_event *child, int blkno ) {
	struct lru_node *node;

	node = fcl_lookup_active_list ( blkno );

	if ( !node ) { // insert active list 
		//printf ( " %f insert active list blkno = %d, %d \n", simtime, blkno, child->blkno );
		fcl_insert_active_list ( child );
		list_add_tail ( &child->fcl_active_list, &parent->fcl_active_list );
	} else { // insert inactive list 
		//printf ( " %f insert inactive list blkno = %d, %d \n", simtime, blkno, child->blkno );
		fcl_insert_inactive_list ( child );
		list_add_tail ( &child->fcl_inactive_list, &parent->fcl_inactive_list );
	}

}


void fcl_make_pending_list ( ioreq_event *parent, int op_type ) {
	int page_count = parent->bcount / FCL_PAGE_SIZE;
	int devno = parent->devno;
	int flags = parent->flags;
	int blkno;
	int i;

	ioreq_event *child;
	listnode *node;

	parent->tempint1 = op_type;
	//printf ( " op type = %d \n", op_type );

	switch ( op_type ) {
		case FCL_OPERATION_NORMAL:
			break;
		case FCL_OPERATION_DESTAGING:
			parent->flags = WRITE;
			break;
		case FCL_OPERATION_STAGING:
			parent->flags = READ;
			break;
	}

	flags = parent->flags; 

	for (i = 0; i < page_count; i++){
		blkno = parent->blkno + i * FCL_PAGE_SIZE;
		
		child = fcl_create_child (  parent, devno, blkno, 
								FCL_PAGE_SIZE, flags ); 
	
		child->fcl_parent = parent;
		list_add_tail ( &child->fcl_pending_list, &parent->fcl_pending_list );
	} 

}

void fcl_make_request ( ioreq_event *parent, int blkno ) {
	switch ( parent->tempint1 ) {
		// child req => Hit: SSD Req
		//		   Read Miss: HDD Read, SSD Write, SSD Read(Can be ommited)
		//		   Write Miss : SSD Read, HDD Write, HDD Write 
		case FCL_OPERATION_NORMAL:
			//printf (" Normal Req blkno = %d \n", blkno);
			if ( sd_is_seq_io ( blkno ) )
				fcl_make_seq_req ( parent, blkno ); // sequential request 
			else {
				if ( fcl_params->fpa_partitioning_scheme != FCL_CACHE_BYPASS ){
					fcl_make_normal_req ( parent, blkno );  // non-sequential request
				} else {
					fcl_make_bypass_req ( parent, blkno );  // non-sequential request
				}
			}

			break;
			// child req => Move SSD data to HDD 
		case FCL_OPERATION_DESTAGING:
			//printf (" Destage Req blkno = %d \n", blkno);

			if ( fcl_cache_mgr->cm_clean_free > 1 ) 
				fcl_make_destage_req ( parent, blkno, 0 );
			else 
				fcl_make_destage_req ( parent, blkno, 1 );

			break;
			// child req => Move HDD data to SSD
		case FCL_OPERATION_STAGING:
			//printf (" Stage Req blkno = %d \n", blkno);
			fcl_make_stage_req ( parent, blkno );
			break;
	}

}

void fcl_split_parent_request (ioreq_event *parent) {
	
	int i;
	int blkno;
	struct list_head *head = &parent->fcl_active_list;
	struct list_head *ptr;
	ioreq_event *child;

	int total_req = 0;

	for ( i = 0; i < parent->fcl_event_num; i++) {
		total_req += parent->fcl_event_count[i];
		ASSERT ( parent->fcl_event_list[i] == NULL );
	}

	ASSERT ( total_req == 0 );
	ASSERT ( parent->bcount % FCL_PAGE_SIZE == 0 );
	ASSERT ( !list_empty ( &parent->fcl_active_list ) );

	list_for_each ( ptr, head ) {
		struct lru_node *ln;
		child = list_entry ( ptr, ioreq_event, fcl_active_list );
		ln = CACHE_PRESEARCH ( fcl_cache_mgr, child->blkno );
		if ( ln ) 
			CACHE_MOVEMRU ( fcl_cache_mgr, ln );
	}

	list_for_each ( ptr, head ) {
		child = list_entry ( ptr, ioreq_event, fcl_active_list );
		blkno = child->blkno;
		fcl_make_request ( parent, blkno );
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
				fcl_req_is_consecutive ( req, req->fcl_event_next )// ) { 
				&& req->bcount < FCL_MAX_REQ_SIZE ) {
				
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

void fcl_remove_empty_node ( ioreq_event *parent ) {
	int i, j;
	// Before: [a][empty][empty][d][e]
	// After: [a][d][e][empty][empty]

	for ( i = 0; i < FCL_EVENT_MAX; i++){
		for ( j = 1; j < FCL_EVENT_MAX-i; j++) {

			if ( parent->fcl_event_count [j-1] == 0 ) {
				parent->fcl_event_count [j-1] = parent->fcl_event_count [j];
				parent->fcl_event_list [j-1] = parent->fcl_event_list [j];

				parent->fcl_event_count [j] = 0;
				parent->fcl_event_list [j] = NULL;
				//fprintf ( stdout, " [%d] %p %d \n", j, 
				//		parent->fcl_event_list[j], parent->fcl_event_count[j]);
			}

		}
	}

	for ( i = 0; i < FCL_EVENT_MAX; i++){
		
		if ( parent->fcl_event_count [i] ) {
			parent->fcl_event_num ++ ;
			//fprintf ( stdout, " [%d] %p %d \n", i, 
			//		parent->fcl_event_list[i], parent->fcl_event_count[i]);
		}
	}

}

// pending childs => active childs 
// active childs => SSD, HDD childs 
void fcl_make_child_request (ioreq_event *parent) {

	int i, j;
	
	// pending requests go to Active or Inactive 
	fcl_issue_pending_child ( parent );

	if ( !list_empty ( &parent->fcl_active_list ) ) {
		// Active requests -> SSD, HDD
		fcl_split_parent_request ( parent );

		parent->fcl_event_ptr = 0;
		parent->fcl_event_num = 0;

		fcl_remove_empty_node ( parent );

		// consecutive requests will be merged 
		// [blkno: 1],[blkno: 2] => [blkno: 1-2]
		fcl_make_merge_next_request ( parent->fcl_event_list, parent->fcl_event_count );
	} 

/*else {

		//printf ( " Pending requests go to inactive list .. \n" );
		//ASSERT ( 0 );
	} */

	
}

int test_blk = 0;

void fcl_get_next_request ( int op_type ) {
	ioreq_event *req = NULL;

	// insert parent req into FCL Overall Queue 
	if ( op_type == FCL_OPERATION_NORMAL ) {
		req = ioqueue_get_next_request ( fcl_fore_q );
		
		sd_seq_detection ( req->blkno, req->bcount);

		if ( !sd_is_seq_io ( req->blkno ) )
			fcl_update_workload_tracker ( req );

	} else {
		req = ioqueue_get_next_request ( fcl_back_q );
		//printf ( " next back .. \n" );
	}

	ASSERT ( req != NULL );

	op_type = req->tempint1;
	fcl_make_pending_list ( req, op_type );

	// parent request will be splited and distributed into SSD and HDD 
	fcl_make_child_request ( req );
	

	if ( !list_empty ( &req->fcl_active_list ) ) {
		// issue requests to IODRIVER
		fcl_issue_next_child ( req ); 
	}

}

void fcl_add_new_request ( ioreq_event *parent, int op_type ) {

	//printf (" fcl add new request %.2f, %.2f, blkno = %d, bcount = %d \n", simtime, parent->time, parent->blkno, parent->bcount );

	//printf ( " %d %d \n", parent->tempint1, op_type );
	ASSERT ( parent->tempint1 == op_type );
	// insert parent req into FCL Overall Queue 
	if ( op_type == FCL_OPERATION_NORMAL ) {
		ioqueue_add_new_request ( fcl_fore_q, parent );

		fcl_stat->fstat_arrive_count ++ ;

	} else {
		ioqueue_add_new_request ( fcl_back_q, parent );
		//printf ( " insert Background Queue \n" );
	}

}

int fcl_update_hit_tracker(struct cache_manager **lru_manager,int lru_num, int blkno,
																		   int is_read )
{

	int hit_position = -1;
	struct lru_node *ln;

	ln = (struct lru_node *)mlru_search(lru_manager, lru_num, blkno, 1, 1, &hit_position);
	

	if(is_read){
		//if(hit_position > S_READ/(SSD_MAX_PAGES/lru_num))
		//	g_GHOST_HIT = 1;
	//	else
	//		g_GHOST_HIT = 0;

		if(ln){
			lru_manager[hit_position]->cm_read_hit++;
			lru_manager[0]->cm_read_ref++;
		}
	}else{
	//	if(hit_position > S_WRITE/(SSD_MAX_PAGES/lru_num))
	//		g_GHOST_HIT = 1;
	//	else
	//		g_GHOST_HIT = 0;
	}

	return hit_position;
}

double fcl_predict_hit_ratio(struct cache_manager **lru_manager,int lru_num, int size,
															int max_pages,int is_read )
{

	int j;
	double total1 = 0, total2 = 0;
	double rhit1 = 0, rhit2 = 0;
	int index =0;
	int window;
	double h_window;
	double r_window;
	double n;
	double hit;


	if ( size > max_pages ) 
		size = max_pages;

	window = max_pages/lru_num;
	index = size/window;
	if(size%window)
		index++;

	//printf (" index = %d \n", index );

	for(j = 0;j < index;j++){
		total1 = total2;
		rhit1 = rhit2;

		if(is_read == FCL_READ)
			total2 += lru_manager[j]->cm_hit;
		else if(is_read == FCL_WRITE)
			total2 += (lru_manager[j]->cm_hit);
		else if(is_read == FCL_READWRITE){
			total2 += (double)lru_manager[j]->cm_hit;
			rhit2 += (double)lru_manager[j]->cm_read_hit;
			//if(lru_manager[j]->cm_hit < lru_manager[j]->cm_read_hit)
			//	j = j;
		}
	//	printf (" hit count = %f \n", total2 );
	}
		
	if ( size % window ) 	
		n = (double)(size%window)/((double)window/10);
	else 
		n = (double)(window)/((double)window/10);
	
	h_window =(double)(total2 - total1)/10;


	if(is_read == FCL_READWRITE){
		
		if(total1 != 0.0)
			rhit1/=total1;
		else
			rhit1 = 0.0;


		if(total2 != 0.0)
			rhit2/=total2;
		else
			rhit2 = 0.0;

		h_window = (double)(rhit2-rhit1)/10;
		total1 = rhit1;

	}

	if(is_read == FCL_READ)
		hit = (double)(total1 + h_window * n)/lru_manager[0]->cm_ref;
	else if(is_read == FCL_WRITE)
		hit = (double)(total1 + h_window * n)/(lru_manager[0]->cm_ref);
	else if(is_read == FCL_READWRITE){
		hit =  (double)(total1 + h_window * n);

		//if(hit < 0.0 || hit > 1.0){
		//	hit = hit;
		//}
	}


	return hit;
}

void fcl_decay_hit_tracker(struct cache_manager **lru_manager,int lru_num){
	int i = 0;
	double decay_factor = fcl_params->fpa_hit_tracker_decayfactor;
	//double decay_factor = 0.5;
/*
	for(i = 0;i < lru_num;i++){		
		lru_manager[i]->cm_hit = (lru_manager[i]->cm_hit) / 2;
		lru_manager[i]->cm_read_hit = (lru_manager[i]->cm_read_hit) / 2;
		lru_manager[i]->cm_ref = (lru_manager[i]->cm_ref) / 2 ;
		lru_manager[i]->cm_read_ref = (lru_manager[i]->cm_read_ref) / 2;
	}
	*/
///*
	for(i = 0;i < lru_num;i++){		
		lru_manager[i]->cm_hit = (double)(lru_manager[i]->cm_hit) * decay_factor;
		lru_manager[i]->cm_read_hit = (double)(lru_manager[i]->cm_read_hit) * decay_factor;
		lru_manager[i]->cm_ref = (double)(lru_manager[i]->cm_ref) * decay_factor;
		lru_manager[i]->cm_read_ref = (double)(lru_manager[i]->cm_read_ref) * decay_factor;
	}
//	*/
}

void fcl_update_workload_tracker ( ioreq_event *parent ) {
	struct lru_node *ln;

	int page_count = parent->bcount/FCL_PAGE_SIZE;
	int start_blkno = parent->blkno/FCL_PAGE_SIZE;

	int i;

	for ( i = 0; i < page_count; i++ ) {
		int blkno = start_blkno + i;
		
		fcl_stat->fstat_io_total_pages ++ ;
		if ( parent->flags & READ ) {


			if(mlru_search( fcl_write_hit_tracker, fcl_hit_tracker_nsegment, blkno, 0, 0, NULL)){
				fcl_update_hit_tracker( fcl_write_hit_tracker, fcl_hit_tracker_nsegment, blkno, READ);
			}else{
				fcl_stat->fstat_io_read_pages++;
				fcl_update_hit_tracker ( fcl_read_hit_tracker, fcl_hit_tracker_nsegment, blkno, 0);
			}

		} else {

			fcl_stat->fstat_io_write_pages++;

			ln = mlru_search( fcl_read_hit_tracker, fcl_hit_tracker_nsegment, blkno, 0, 0, NULL);
			if(ln){
				mlru_remove( fcl_read_hit_tracker, fcl_hit_tracker_nsegment, blkno);
			}

			fcl_update_hit_tracker ( fcl_write_hit_tracker, fcl_hit_tracker_nsegment, blkno, 0);
		}
	}

}

void _fcl_request_arrive ( ioreq_event *parent, int op_type ) {
	ioreq_event *req;

	//parent->bcount = 2048;

	parent->blkno = parent->blkno % (hdd_total_sectors);

	parent->blkno = (parent->blkno / FCL_PAGE_SIZE) * FCL_PAGE_SIZE;
	if ( parent->bcount % FCL_PAGE_SIZE ) {
		parent->bcount += (FCL_PAGE_SIZE - ( parent->bcount % FCL_PAGE_SIZE));
	}

	if ( parent->blkno + parent->bcount >= hdd_total_sectors ) {
		int temp = parent->blkno + parent->bcount - hdd_total_sectors;
		//temp -= 1;
		parent->blkno -= temp;
	}
/*
	if ( op_type == FCL_OPERATION_NORMAL ) {

		sd_seq_detection ( parent->blkno, parent->bcount);

		if ( !sd_is_seq_io ( parent->blkno ) )
			fcl_update_workload_tracker ( parent );
	} */

	ASSERT ( parent->blkno < hdd_total_sectors );

	if ( fcl_timer_func && op_type == FCL_OPERATION_NORMAL ) {
		//printf (" Timer off !! \n " );
		fcl_timer_func == NULL;
	}


	ASSERT ( parent->blkno % FCL_PAGE_SIZE == 0 && 
			 parent->bcount % FCL_PAGE_SIZE == 0 );

	fcl_parent_init ( parent );

	parent->tempint1 = op_type;
	fcl_add_new_request( parent, op_type );

	if ( op_type == FCL_OPERATION_NORMAL ) {
		fcl_event_next_foreground_request ();
	} else {
		fcl_event_next_background_request ();
	}

	ASSERT ( ioqueue_get_reqoutstanding ( fcl_fore_q ) <= FCL_FORE_Q_DEPTH ) ;
	ASSERT ( ioqueue_get_reqoutstanding ( fcl_back_q ) <= FCL_BACK_Q_DEPTH ) ;

#if 0 
	printf ( " get next .. \n" );
//	printf ( " Queue # of reqs = %d \n", ioqueue_get_number_of_requests ( fcl_fore_q ));
	printf ( " Queue # of outstanding reqs = %d \n", ioqueue_get_reqoutstanding ( fcl_fore_q ));
	printf ( " Queue # of reqs in queue = %d \n", ioqueue_get_number_in_queue ( fcl_fore_q ));
	printf ( " Queue # of pending reqs = %d \n", ioqueue_get_number_pending ( fcl_fore_q ));
#endif 

	//printf ( " Arrive Queue # of outstanding reqs = %d \n", ioqueue_get_reqoutstanding ( fcl_fore_q ));

}



int fcl_resize_rwcache () {

	int dirty_reduce = fcl_cache_mgr->cm_dirty_size - fcl_optimal_write_pages;
	int clean_reduce = fcl_cache_mgr->cm_clean_size - fcl_optimal_read_pages;
	//int target_dirty = 0;
	//int target_clean = 0;
	int remain = 0;
	int max_dirty_reduce = 0;

	//printf ( " Resizing cache ...dirty reduce = %.2fMB clean reduce = %.2fMB \n", (double)dirty_reduce/256, (double)clean_reduce/256 );
	//printf ( " write size optimal = %.2f, curr = %.2f \n", (double)fcl_optimal_write_pages/256, (double)fcl_cache_mgr->cm_dirty_size/256 );
	//printf ( " Read size optimal = %.2f, curr = %.2f \n", (double)fcl_optimal_read_pages/256, (double)fcl_cache_mgr->cm_clean_size/256 );

	//printf ( " dirty diff = %d, clean diff %d \n", dirty_reduce, clean_reduce );
	//printf ( " dirty free = %d, clean free = %d \n",  fcl_cache_mgr->cm_dirty_free, fcl_cache_mgr->cm_clean_free); 
	//printf ( " dirty size = %d, clean size = %d \n", fcl_cache_mgr->cm_dirty_size, fcl_cache_mgr->cm_clean_size ); 


	if ( fcl_cache_mgr->cm_dirty_free > 0 ) {
		max_dirty_reduce = fcl_cache_mgr->cm_dirty_free;
	}
	max_dirty_reduce += FCL_MAX_RESIZE;

	if ( dirty_reduce > max_dirty_reduce  ) { 
		dirty_reduce = max_dirty_reduce;

		//ASSERT ( clean_reduce < 0 );

		if ( clean_reduce < 0 )
			clean_reduce = -max_dirty_reduce;

	} else if ( clean_reduce > FCL_MAX_RESIZE ) {
//		clean_reduce = FCL_MAX_RESIZE;
//		dirty_reduce = -FCL_MAX_RESIZE;
	}

	///printf ( " 2 dirty reduce = %d, clean reduce %d \n", dirty_reduce, clean_reduce );
	

	//lru_set_dirty_size ( fcl_cache_mgr, fcl_optimal_write_pages, fcl_optimal_read_pages );
	lru_set_dirty_size ( fcl_cache_mgr, fcl_cache_mgr->cm_dirty_size - dirty_reduce , 
			                            fcl_cache_mgr->cm_clean_size - clean_reduce );

	//if ( fcl_cache_mgr->cm_dirty_free >= 0 && fcl_cache_mgr->cm_clean_free >= 0 ) {
	if ( fcl_cache_mgr->cm_dirty_size == fcl_optimal_write_pages && 
		 fcl_cache_mgr->cm_clean_size == fcl_optimal_read_pages && 
		 fcl_cache_mgr->cm_dirty_free >= 0 && 
		 fcl_cache_mgr->cm_clean_free >= 0 
		 ) {
		//printf ( " * Resize done \n ");
		return remain;
	}

	if ( fcl_cache_mgr->cm_dirty_free <  0 ) {

		dirty_reduce = fcl_cache_mgr->cm_dirty_free * -1;	
		if ( dirty_reduce > FCL_MAX_RESIZE ) {
			dirty_reduce = FCL_MAX_RESIZE;
	//		remain = 1;
		}

		//printf (" **Destage %d dirty pages %.2fMB \n", dirty_reduce, (double)dirty_reduce/256 );

		fcl_destage_request ( dirty_reduce );
	} 

	if ( fcl_cache_mgr->cm_clean_free < 0 ) {

		clean_reduce = fcl_cache_mgr->cm_clean_free * -1;
		//printf (" **Remove %d clean pages %.2fMB \n", clean_reduce, (double)clean_reduce/256 );

		fcl_invalid_request ( clean_reduce );
	}


	if ( fcl_cache_mgr->cm_dirty_size != fcl_optimal_write_pages || 
		 fcl_cache_mgr->cm_clean_size != fcl_optimal_read_pages )
		remain = 1;

	if ( fcl_cache_mgr->cm_dirty_size == fcl_optimal_write_pages && 
		 fcl_cache_mgr->cm_clean_size == fcl_optimal_read_pages && 
		 fcl_cache_mgr->cm_dirty_free >= 0 && 
		 fcl_cache_mgr->cm_clean_free >= 0 
		 ) {
		//printf ( " * Resize done \n ");
	}

	ASSERT ( fcl_cache_mgr->cm_size  == fcl_cache_mgr->cm_dirty_size + fcl_cache_mgr->cm_clean_size );

	//printf ( " fqueue = %d, bqueue = %d \n", 
	//			ioqueue_get_number_in_queue ( fcl_fore_q ),
				//ioqueue_get_number_in_queue ( fcl_back_q ) );
	//printf ("\n");


	return remain;
}


void fcl_conduct_resize () {

	if ( fcl_stat->fstat_io_total_pages / fcl_params->fpa_resize_period < fcl_params->fpa_resize_next )   
	// ||		fcl_resize_trigger != 0 ) 
	{ 
		return;
	}

	fcl_params->fpa_resize_next++;

	/*printf ( " Resize period = %d \n", fcl_stat->fstat_io_total_pages/fcl_params->fpa_resize_period );
	printf ( " Read I/O Ratio = %.2f \n", (double)fcl_stat->fstat_io_read_pages/fcl_stat->fstat_io_total_pages );
	printf ( " Write Free = %.2fMB, Read Free = %.2fMB\n", PAGE_TO_MB(fcl_cache_mgr->cm_dirty_free),
			PAGE_TO_MB(fcl_cache_mgr->cm_clean_free)); */

	fcl_find_optimal_size ( fcl_write_hit_tracker, fcl_read_hit_tracker,
			fcl_hit_tracker_nsegment, flash_total_pages,
			&fcl_optimal_read_pages, &fcl_optimal_write_pages);
/*
	printf ( " -> curr read pages = %d (%d), curr write pages = %d (%d)\n", 
			fcl_cache_mgr->cm_clean_count,
			fcl_cache_mgr->cm_clean_size,
			fcl_cache_mgr->cm_dirty_count,
			fcl_cache_mgr->cm_dirty_size);
	printf ( " -> opti read pages = %d, opti write pages = %d \n", 
			fcl_optimal_read_pages,
			fcl_optimal_write_pages );
			*/

	if ( fcl_cache_mgr->cm_clean_size != fcl_optimal_read_pages || 
			fcl_cache_mgr->cm_dirty_size != fcl_optimal_write_pages )  
	{

		fcl_resize_trigger = 1;
	}


	//if ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_OPTIMAL ) {
	fcl_decay_hit_tracker ( fcl_write_hit_tracker, fcl_hit_tracker_nsegment );
	fcl_decay_hit_tracker ( fcl_read_hit_tracker, fcl_hit_tracker_nsegment );
		//}
	//}
}
int debug_arrive = 0;

void fcl_request_arrive (ioreq_event *parent){

	if ( ++debug_arrive % 50000 == 0 ) {
		printf ( " FCL Req Arrive time = %.2f, blkno = %d, bcount = %d, flags = %d, devno = %d, fqueue = %d, bqueue = %d \n", 
				simtime, parent->blkno, parent->bcount, parent->flags, parent->devno, ioqueue_get_number_in_queue ( fcl_fore_q ),
				ioqueue_get_number_in_queue ( fcl_back_q ) );
		printf ( " FCL Dirty Size = %.2fMB, Clean Size = %.2fMB \n", (double)fcl_cache_mgr->cm_dirty_count/256, (double)fcl_cache_mgr->cm_clean_count/256);
		printf ( " FCL Written = %.2fMB, Read = %.2fMB \n", (double)fcl_stat->fstat_io_write_pages/256,
															(double)fcl_stat->fstat_io_read_pages/256);
//		lru_print ( fcl_active_block_mgr ) ;
	}
	//fprintf ( stdout, " FCL Req Arrive time = %f, blkno = %d, bcount = %d \n", 
	//									simtime, parent->blkno, parent->bcount);
	
	if ( !fcl_params->fpa_bypass_cache ) { 

		if ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_RW ||
			 fcl_params->fpa_partitioning_scheme == FCL_CACHE_OPTIMAL )
		{
			fcl_conduct_resize ();
		}

		_fcl_request_arrive ( parent, FCL_OPERATION_NORMAL );

	} else  {

		parent->devno = HDD;
		parent->blkno = parent->blkno % (flash_usable_sectors);

		parent->type = IO_REQUEST_ARRIVE;
		addtointq ( (event *) parent );

	}

}

void fcl_seal_complete_request ( ioreq_event *parent ) {
	struct lru_node *ln;

	ioreq_event *child;
	struct list_head *head = &parent->fcl_active_list;
	struct list_head *ptr;


	list_for_each ( ptr, head ) {
		child = (ioreq_event *) list_entry ( ptr, ioreq_event, fcl_active_list );

		ln = CACHE_PRESEARCH(fcl_cache_mgr, child->blkno);

		if ( ln == NULL) {
			// sequential i/o case 
			//printf ( " blkno = %d \n", child->blkno );
		}
		//ASSERT ( ln != NULL );
	
		if ( ln ) {
			ln->cn_flag = FCL_CACHE_FLAG_SEALED;
		}

	}

}

void fcl_insert_pending_manager ( ioreq_event * parent) {

	struct lru_node *ln = NULL;

	ln = CACHE_PRESEARCH ( fcl_pending_mgr, (int)parent );

	if ( ln == NULL ) {
		ln = CACHE_ALLOC ( fcl_pending_mgr, NULL, (int)parent );

		ln->cn_temp1 = (void *)parent;

		CACHE_INSERT ( fcl_pending_mgr, ln );

		ASSERT ( CACHE_PRESEARCH ( fcl_pending_mgr, (int)parent ) ) ;
	}
 
	//printf (" *fcl_pending_mgr: pending I/Os = %d \n", fcl_pending_mgr->cm_count ); 

}

void fcl_remove_inactive_list ( ioreq_event *parent, ioreq_event *child) {

	list_del ( &child->fcl_inactive_list );

}

void fcl_move_pending_list ( listnode *inactive_list ){

	listnode *inactive_node = inactive_list->next;
	ioreq_event *child;
	ioreq_event *parent;

	int inactive_count = ll_get_size ( inactive_list );
	int i;
	
	// move pending child to its parent pendling list 

	for ( i = 0; i < inactive_count; i++ ) {

		child = inactive_node->data;	
		parent = child->fcl_parent;

		//printf ( " Blocking request blkno = %d \n", child->blkno );

		//print_parent_child_state ( parent );

		fcl_remove_inactive_list ( parent, child );
		list_add_tail ( &child->fcl_pending_list, &parent->fcl_pending_list );
		
		fcl_insert_pending_manager ( parent );	

		//print_parent_child_state ( parent );

		inactive_node = inactive_node->next;
	}

	//printf (" inactive size = %d \n", ll_get_size ( inactive_list ) );
}

void fcl_remove_active_block_in_active_list (int blkno ) {
	struct lru_node *ln;

	//printf (" remove active blk = %d in active block manager \n", blkno );
	ln = CACHE_PRESEARCH(fcl_active_block_mgr, blkno);

	ASSERT ( ln != NULL );

	if ( ln ) {
		
		if ( ll_get_size ( (listnode *)ln->cn_temp2 )){

			//printf (" It has blocking child reqeusts of some parents, parent blkno=%d \n",
			//		parent->blkno);

			fcl_move_pending_list ( ln->cn_temp2 );
			//ASSERT ( ll_get_size ((listnode *)ln->cn_temp2 ) == 0);	
		}

		ll_release ( ln->cn_temp2 );
		ln->cn_temp2 = NULL;
		CACHE_REMOVE ( fcl_active_block_mgr, ln ); 

		free (ln);
	}
}

void fcl_remove_active_list ( ioreq_event *parent ) {

	struct list_head *head = &parent->fcl_active_list;
	struct list_head *ptr;
	ioreq_event *child;


	// move active child in active list to complete list   
	list_for_each ( ptr, head ) {
		child = (ioreq_event *) list_entry ( ptr, ioreq_event, fcl_active_list );

		fcl_remove_active_block_in_active_list ( child->blkno );

		list_add_tail ( &child->fcl_complete_list, &parent->fcl_complete_list );
	}

	while ( !list_empty ( head ) ) {
		child = (ioreq_event *) list_first_entry ( head, ioreq_event, fcl_active_list );
		list_del ( &child->fcl_active_list );
	}

	ASSERT ( list_empty ( &parent->fcl_active_list ) );


}


void fcl_remove_complete_list ( ioreq_event *parent ) {

	ioreq_event *child;
	struct list_head *head = &parent->fcl_complete_list;
	struct list_head *ptr;


	while ( !list_empty ( head ) ) {
		child = (ioreq_event *) list_first_entry ( head, ioreq_event, fcl_complete_list );
		list_del ( &child->fcl_complete_list );
		addtoextraq ( (event *) child );
	}


	ASSERT ( list_empty ( &parent->fcl_complete_list ) );

}

// pending childs go to ACTIVE or InACTIVE lists 
void fcl_issue_pending_child ( ioreq_event *parent ) {

	struct list_head *head = &parent->fcl_pending_list;
	struct list_head *ptr;
	ioreq_event *child;
	

	while ( !list_empty ( head ) ) {
		child = (ioreq_event *) list_first_entry ( head, ioreq_event, fcl_pending_list ) ;
		fcl_classify_child_request ( parent, child, child->blkno );
		list_del ( &child->fcl_pending_list );
	}

	ASSERT ( list_empty ( head ) );
}


void fcl_issue_pending_parent (){
	struct list_head *head = &fcl_pending_mgr->cm_head;
	struct list_head *ptr, *next;
	struct lru_node *ln;
	ioreq_event *parent;
	

	list_for_each_safe( ptr, next, head ) {
		ln = (struct lru_node *) list_entry ( ptr, struct lru_node, cn_list );	

		parent = (ioreq_event *) ln->cn_blkno;

		ASSERT ( ln->cn_temp1 == parent );

		if ( list_empty ( &parent->fcl_active_list ) && 
			 list_empty ( &parent->fcl_inactive_list ) ) {
			
			fcl_make_child_request ( parent );

			if ( !list_empty ( &parent->fcl_active_list ) ) {
				fcl_issue_next_child ( parent );
			}

			//list_del ( ptr );
			CACHE_REMOVE ( fcl_pending_mgr, ln );
			free ( ln );
		}
	}

}	


static int fcl_compare_blkno(const void *a,const void *b){
	if(((ioreq_event *)a)->blkno < ((ioreq_event *)b)->blkno)
		return 1;
	else if(((ioreq_event *)a)->blkno > ((ioreq_event *)b)->blkno)
		return -1;
	return 0;
}

ioreq_event *fcl_create_parent (int blkno,int bcount, double time, int flags, int devno) {
	ioreq_event * parent;

	//parent  = (ioreq_event *) ioreq_copy ( (ioreq_event *) io_extq );  	
	parent  = (ioreq_event *) getfromextraq(); 

	parent->blkno = blkno;
	parent->bcount = bcount;
	parent->time = time;
	parent->flags = flags;
	parent->devno = devno;

	parent->buf = 0;
	parent->busno = 0;
	parent->cause = 0;

	return parent;

}

void fcl_merge_list ( listnode *dirty_list ) {
	listnode *dirty_node;
	int i;
	int req_count = ll_get_size ( dirty_list );

	dirty_node = dirty_list->next;

	for ( i = 0; i < req_count; i++ ) {
		listnode *dirty_next;
		ioreq_event *req1, *req2;
	
		dirty_next = dirty_node->next;
		req1 = dirty_node->data;
		req2 = dirty_next->data;

		if ( dirty_next != dirty_list &&
				fcl_req_is_consecutive ( req1, req2)) {
			
			req1->bcount += req2->bcount;				
			addtoextraq((event *) req2);
			ll_release_node ( dirty_list, dirty_next );

		} else {
			dirty_node = dirty_node->next;

		}

	}

}

void fcl_stage_request () {
	ioreq_event *parent;
	listnode *stage_list;
	listnode *stage_node;

	int i;
	int req_count = 0;

	int list_count = flash_usable_pages;

	ll_create ( &stage_list );

	for ( i = 0; i < list_count; i++ ) {
		int blkno = i * FCL_PAGE_SIZE;

		if ( CACHE_PRESEARCH ( fcl_cache_mgr, blkno ) == NULL ) {
			parent = fcl_create_parent ( blkno, FCL_PAGE_SIZE, simtime, READ, 0 );
			parent->tempint1 = FCL_OPERATION_STAGING;
			
			ll_insert_at_sort ( stage_list, (void *) parent, fcl_compare_blkno );
			req_count++;
		}

		if ( ll_get_size ( stage_list ) >= FCL_MAX_STAGE ) 
			break;
	}


	fcl_merge_list ( stage_list );

	stage_node = stage_list->next;

	for ( i = 0; i < ll_get_size ( stage_list) ; i++ ) {

		parent = stage_node->data;
		_fcl_request_arrive ( parent, FCL_OPERATION_STAGING );

		stage_node = stage_node->next;

	}

	ll_release ( stage_list );
}


int fcl_invalid_request ( int invalid_num) {
	ioreq_event *parent;

	struct list_head *head = &fcl_cache_mgr->cm_clean_head;
	struct list_head *ptr;

	listnode *clean_list; 
	listnode *clean_node; 

	struct lru_node *ln;
	int i;

	int clean_count = fcl_cache_mgr->cm_clean_count;
	int invalid_count = 0;

	if ( clean_count < invalid_num ) 
		return invalid_count;

	ll_create ( &clean_list );

	list_for_each_prev( ptr, head ) {
		ln = (struct lru_node *) list_entry ( ptr, struct lru_node, cn_clean_list);

		ASSERT ( ln->cn_dirty == 0  );

		if ( ln->cn_dirty == 0 ) {

		//	printf (" [%d] invaid block = %d \n", clean_count, ln->cn_blkno );

			ll_insert_at_tail ( clean_list, (void *)ln->cn_blkno); 
			clean_count++;
		}

		if ( ll_get_size ( clean_list ) >= invalid_num ) 
			break;

	}

	clean_node = clean_list->next;

	for ( i = 0; i < ll_get_size ( clean_list ); i++ ) {
		int blkno = (int)clean_node->data;	

		ln = CACHE_PRESEARCH ( fcl_cache_mgr, blkno );
		CACHE_REMOVE ( fcl_cache_mgr, ln );

		reverse_map_release_blk ( SSD, ln->cn_ssd_blk );
		free ( ln );

		clean_node = clean_node->next;
	}

	ll_release ( clean_list );

	
//	ASSERT (0);
	return clean_count;
}

int fcl_destage_request ( int destage_num) {
	ioreq_event *parent;

	struct list_head *head = &fcl_cache_mgr->cm_dirty_head;
	struct list_head *ptr;

	listnode *dirty_list; 
	listnode *dirty_node; 

	struct lru_node *ln;
	int i;

	int dirty_count = fcl_cache_mgr->cm_dirty_count;
	int destage_count = 0;

	if ( dirty_count < destage_num ) 
		return destage_count;

	ll_create ( &dirty_list );

	list_for_each_prev( ptr, head ) {
		ln = (struct lru_node *) list_entry ( ptr, struct lru_node, cn_dirty_list);

		ASSERT ( ln->cn_dirty == 1  );

		if ( ln->cn_dirty ) {
			parent = fcl_create_parent ( ln->cn_blkno, FCL_PAGE_SIZE, simtime, 0, 0 );
			parent->tempint1 = FCL_OPERATION_DESTAGING;	

			//printf ( " blkno = %d \n", parent->blkno);

			ll_insert_at_sort ( dirty_list, (void *) parent, fcl_compare_blkno ); 
			destage_count++;
		}

		if ( ll_get_size ( dirty_list ) >= destage_num ) 
			break;

	}


	fcl_merge_list ( dirty_list );

	dirty_node = dirty_list->next;

	for ( i = 0; i < ll_get_size ( dirty_list ); i++ ) {

		parent = dirty_node->data;	
		//printf ( "1 blkno = %d \n", parent->blkno);
		_fcl_request_arrive ( parent, FCL_OPERATION_DESTAGING );

		dirty_node = dirty_node->next;
	}

	ll_release ( dirty_list );

	return destage_count;
}

void fcl_timer_event ( timer_event *timereq) {

	int ret;

	//printf ( " Timer Inttupt !! %f, %f \n", simtime, timereq->time );

	if ( ioqueue_get_number_in_queue ( fcl_fore_q )  == 0 && 
		 ioqueue_get_number_in_queue ( fcl_back_q ) == 0 && 
		 fcl_timer_func ) 
	{
#if 1 
		printf ( " Background Stage \n" );
		fcl_stage_request ();
#else
		ret = fcl_destage_request ( FCL_MAX_DESTAGE );
		if ( ret ) 
			printf ( " Background Destage ... = %d req\n", ret );
#endif 

	} else {
		//printf ( " Destroy Timer Event \n");
	}

	fcl_timer_func = NULL;
	addtoextraq ( (event *) timereq ); 
}

void fcl_make_timer () {
	timer_event *timereq = (timer_event *)getfromextraq();

	fcl_timer_func = fcl_timer_event;

	timereq->func = &fcl_timer_func ;
	timereq->type = TIMER_EXPIRED;
	//timereq->time = simtime + (double) fcl_idle_time;
	timereq->time = simtime + fcl_params->fpa_idle_detect_time;

	timereq->ptr = NULL;

	addtointq ( (event *) timereq );

	//printf ( " Issue Timer = %f, %f, %f \n", simtime, timereq->time,
	//		simtime + 5000);

}

int fcl_all_queue_outstanding_empty () {

	if ( ioqueue_get_reqoutstanding ( fcl_fore_q )  == 0 && 
		 ioqueue_get_reqoutstanding ( fcl_back_q ) == 0 )
		return 1;

	return 0;
}

int fcl_all_queue_empty () {

	if ( ioqueue_get_number_in_queue ( fcl_fore_q )  == 0 && 
		 ioqueue_get_number_in_queue ( fcl_back_q ) == 0 )
		return 1;

	return 0;
}

void fcl_discard_deleted_pages () {
	reverse_map_discard_freeblk ( SSD );
}

int fcl_parent_request_complete ( ioreq_event *parent ) {
	int count = 0;

	count +=  list_empty ( &parent->fcl_active_list ) ;
	count += list_empty ( &parent->fcl_inactive_list ) ;
	count += list_empty ( &parent->fcl_pending_list ) ;

	if ( count == 3 ) 
		return 1;

	return 0;
}

void _fcl_request_complete ( ioreq_event *child ) {
	ioreq_event *parent, *req2;
	int total_req = 0;	
	int i;


	parent = (ioreq_event *)child->fcl_parent;

	parent->fcl_event_count[parent->fcl_event_ptr-1] -- ;

	// next events are remaining. 
	if ( parent->fcl_event_count[parent->fcl_event_ptr-1] == 0 &&
			parent->fcl_event_ptr < parent->fcl_event_num ) 
	{
		//fprintf ( stdout, " **Issue next Requst .. \n" );
		//printf (" Issue Next Request .. dev = %d, flags = %d\n", child->devno, child->flags);
		fcl_issue_next_child ( parent );
	}

	addtoextraq ((event *) child);

	for ( i = 0; i < parent->fcl_event_num; i++) {
		total_req += parent->fcl_event_count[i];
	}

	// all child requests are complete 
	if ( total_req  == 0 ) { 

		if ( parent->tempint1 == FCL_OPERATION_NORMAL ||
			parent->tempint1 == FCL_OPERATION_STAGING ) 
		{
			fcl_seal_complete_request ( parent );
		}

		fcl_remove_active_list ( parent );

		// active request exist
		ASSERT ( list_empty ( &parent->fcl_active_list ) );

	}

	if ( fcl_parent_request_complete ( parent ) ){

		ASSERT ( list_empty ( &parent->fcl_active_list ) && 
				list_empty ( &parent->fcl_inactive_list )  );

		fcl_parent_release ( parent );

		if ( parent->tempint1 == FCL_OPERATION_NORMAL ) {
			fcl_stat->fstat_complete_count++;

			ASSERT ( simtime - parent->time >= 0 );
			req2 = ioqueue_physical_access_done (fcl_fore_q, parent);
			fcl_event_next_foreground_request ();

		} else {
		//	printf ( " background req done opid = %d \n", parent->opid );

			ASSERT ( simtime - parent->time >= 0 );
			req2 = ioqueue_physical_access_done (fcl_back_q, parent);
			fcl_event_next_background_request ();
		}

		ASSERT (req2 != NULL);
		addtoextraq ((event *) parent);

		//fprintf ( stdout, " ** %.2f %.2f (%.2f)  Complete parent req ... %d %d %d \n", simtime, parent->time, simtime - parent->time,  parent->blkno, parent->bcount, parent->flags);

	}
	

	// issue pending I/Os 
	//if ( ll_get_size ( fcl_pending_mgr )  &&
	if (  fcl_pending_mgr->cm_count  &&
		 ioqueue_get_number_in_queue ( fcl_back_q ) == 0 ) 
	{
		//printf (" Try to issue pending I/Os \n" );
		fcl_issue_pending_parent ();
	}

	//if ( fcl_resize_trigger && fcl_all_queue_outstanding_empty() ) { 
	if ( fcl_resize_trigger && fcl_all_queue_empty() ) { 
		int remain = fcl_resize_rwcache ();

		fcl_resize_trigger = remain ? 1 : 0;
	}

	// issue waited I/Os by resize process 
	///*
	if ( ioqueue_get_number_in_queue ( fcl_back_q ) == 0 && !fcl_resize_trigger ) {

		fcl_event_next_foreground_request ();

	}
	//*/

	/* background group destaging */
	//*
	if ( fcl_params->fpa_background_group_destage &&  
		 fcl_all_queue_empty() &&
		 fcl_cache_mgr->cm_dirty_free == 0 &&
		 parent->tempint1 == FCL_OPERATION_NORMAL 
	)
	{
		fcl_destage_request ( FCL_MAX_DESTAGE );

	}
	//*/

	/* Trim invalidated pages in SSD  */
	if ( fcl_all_queue_empty () ) {
		fcl_discard_deleted_pages ();
	}

#if FCL_BACKGROUND_TIMER == 1 
	/* background destaging  */ 
	if ( fcl_all_queue_empty () &&
		 fcl_timer_func == NULL && 
		 !feof ( disksim->iotracefile) )
	{
		//printf ( " FCL Queue Length = %d \n", ioqueue_get_number_in_queue ( fcl_fore_q ) );
		if ( fcl_params->fpa_idle_detect_time > 0.0 )
			fcl_make_timer ();
	}
#endif 
}

void fcl_event_next_foreground_request () {

	if ( ioqueue_get_reqoutstanding ( fcl_fore_q ) < FCL_FORE_Q_DEPTH &&
			ioqueue_get_number_pending ( fcl_fore_q ) ) {
		//printf (" next request \n");
		fcl_get_next_request ( FCL_OPERATION_NORMAL );

		ASSERT ( ioqueue_get_reqoutstanding ( fcl_fore_q ) <= FCL_FORE_Q_DEPTH ) ;
	}
}

void fcl_event_next_background_request () {

	if ( ioqueue_get_reqoutstanding ( fcl_back_q ) < FCL_BACK_Q_DEPTH &&
			ioqueue_get_number_pending ( fcl_back_q ) ) {

		fcl_get_next_request ( FCL_OPERATION_DESTAGING );
		//fcl_get_next_request ( FCL_OPERATION_STAGING );

		ASSERT ( ioqueue_get_reqoutstanding ( fcl_back_q ) <= FCL_BACK_Q_DEPTH ) ;
	}
}

void fcl_event_arrive ( ioreq_event *curr ) {

	switch ( curr->type ) {
		case FCL_REQUEST_ARRIVE:
			curr->time = simtime + fcl_params->fpa_overhead;
			curr->type = FCL_OVERHEAD_COMPLETE;
			addtointq ( (event *)curr );
			break;
		case FCL_OVERHEAD_COMPLETE:
			fcl_request_arrive ( curr );
			break;
		case FCL_ACCESS_COMPLETE:
			fcl_request_complete ( curr );
			break;
	}

}



void fcl_request_complete (ioreq_event *child){
	//if ( child->devno == HDD && child->flags & READ )
	//	ASSERT ( 0 );
	//printf (" %.2f, %.2f  Request Complete .. \n", simtime, simtime - child->time);
	if ( !fcl_params->fpa_bypass_cache ) {
		_fcl_request_complete ( child );
	} else {
		addtoextraq ( (event *) child );
	}
	
}


int disksim_fcl_loadparams ( struct lp_block *b, int *num) {
	
	fcl_params = malloc ( sizeof(struct fcl_parameters) );
	memset ( fcl_params, 0x00, sizeof(struct fcl_parameters) );
	lp_loadparams ( fcl_params, b, &disksim_fcl_mod);

	if ( FCL_FORE_Q_DEPTH == 0 ) 
		FCL_FORE_Q_DEPTH = 1 ;

	FCL_FORE_Q_DEPTH_TEMP = FCL_FORE_Q_DEPTH;

	if ( FCL_BACK_Q_DEPTH == 0 ) 
		FCL_BACK_Q_DEPTH = 10000 ;

	if ( fcl_params->fpa_hdd_crpos == 0.0 ) 
		fcl_params->fpa_hdd_crpos = 4500;

	if ( fcl_params->fpa_hdd_cwpos == 0.0 ) 
		fcl_params->fpa_hdd_cwpos = 4900;

	if ( fcl_params->fpa_hdd_bandwidth == 0.0 )
		fcl_params->fpa_hdd_bandwidth = 72;

	if ( fcl_params->fpa_resize_period == 0 ) 
		fcl_params->fpa_resize_period = 60000;

	if ( fcl_params->fpa_seq_unit_size == 0 ) 
		fcl_params->fpa_seq_unit_size = 256;

	if ( fcl_params->fpa_hit_tracker_nsegment == 0 ) 
		fcl_params->fpa_hit_tracker_nsegment = 16;
		//fcl_params->fpa_hit_tracker_nsegment = 512;

	if ( fcl_params->fpa_max_destage_size == 0 ) 
		fcl_params->fpa_max_destage_size = 128;

	if ( fcl_params->fpa_max_resize_size == 0 ) 
		fcl_params->fpa_max_resize_size = 128;
	
	if ( fcl_params->fpa_hit_tracker_decayfactor == 0.0 ) 
		fcl_params->fpa_hit_tracker_decayfactor = 0.5;


	fcl_params->fpa_resize_next = 1;

	return 0;
}

void fcl_print_parameters ( FILE *fp ) {

	fprintf ( fp, "\n" );
	fprintf ( fp, " Print FCL Parameters .. \n" );
	fprintf ( fp, " Page size = %d sectors \n", fcl_params->fpa_page_size );
	fprintf ( fp, " Max pages percent = %.2f \n", fcl_params->fpa_max_pages_percent );
	fprintf ( fp, " Bypass cache = %d \n", fcl_params->fpa_bypass_cache );
	fprintf ( fp, " Idle detect time= %.2f ms \n", fcl_params->fpa_idle_detect_time );
	fprintf ( fp, " On-demand Group Destage = %d \n", fcl_params->fpa_ondemand_group_destage );
	fprintf ( fp, " Background Group Destage = %d \n", fcl_params->fpa_background_group_destage );
	
	fprintf ( fp, " Hit Tracker Decay Factor = %f \n", fcl_params->fpa_hit_tracker_decayfactor );
	fprintf ( fp, " Hit Tracker n-Segment Size = %d \n", fcl_params->fpa_hit_tracker_nsegment );

	fprintf ( fp, " Seq Detection Enable = %d \n", fcl_params->fpa_seq_detection_enable );
	fprintf ( fp, " Seq unit size = %d sectors \n", fcl_params->fpa_seq_unit_size );

	fprintf ( fp, " Max Destage Size = %d \n", FCL_MAX_DESTAGE );
	fprintf ( fp, " Max Resize  Size = %d \n", FCL_MAX_RESIZE );
	fprintf ( fp, " Max Request  Size = %d \n", FCL_MAX_REQ_SIZE );
	
	fprintf ( fp, " Foreground Q Depth = %d \n", FCL_FORE_Q_DEPTH );
	fprintf ( fp, " Background Q Dpeth = %d \n", FCL_BACK_Q_DEPTH );
	
	if ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_FIXED ) {
		fprintf ( fp, " Cache partitionig = Fixed \n");
	} else if ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_RW ) {
		fprintf ( fp, " Cache partitionig = Read Write Distinguish \n");
	} else if ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_OPTIMAL ) {
		fprintf ( fp, " Cache partitionig = Optimal \n");
	} else if ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_BYPASS ) {
		fprintf ( fp, " Cache partitionig = Bypassing Cache \n");
	} else {
		fprintf ( fp, " Set default partitioning scheme (Fixed) !!! \n");
		fcl_params->fpa_partitioning_scheme = FCL_CACHE_FIXED;
	}

	fprintf ( fp, " HDD Read Positioning Time = %.1f us \n", fcl_params->fpa_hdd_crpos );
	fprintf ( fp, " HDD Write Positioning Time = %.1f us \n", fcl_params->fpa_hdd_cwpos );
	fprintf ( fp, " HDD Bandwidth = %.1f MB/s \n", fcl_params->fpa_hdd_bandwidth );
	fprintf ( fp, " \n" );

	fprintf ( fp, " SSD Program Time = %.1f us \n", fcl_params->fpa_ssd_cprog );
	fprintf ( fp, " SSD Read Time = %.1f us \n", fcl_params->fpa_ssd_cread );
	fprintf ( fp, " SSD Erase Time = %.1f us \n", fcl_params->fpa_ssd_cerase );
	fprintf ( fp, " SSD Bus Time = %.1f us \n", fcl_params->fpa_ssd_cbus );
	fprintf ( fp, " SSD NP = %d \n", fcl_params->fpa_ssd_np);




	fprintf ( fp, "\n");
}

void fcl_initial_discard_pages () {
	int i;

	printf ( " Discard %d pages (%.2f MB) in SSD \n", flash_usable_pages, (double)flash_usable_pages/256 );

	for ( i = 0; i < flash_usable_pages; i++ ) {
		ssd_trim_command ( SSD, i * FCL_PAGE_SIZE);
	}
	
}	



void fcl_set_ssd_params(ssd_t *curssd){
	SSD_PROG = curssd->params.page_write_latency*1000;
	SSD_READ = curssd->params.page_read_latency*1000;
	SSD_ERASE = curssd->params.block_erase_latency*1000;
	SSD_NP = curssd->params.pages_per_block;
	SSD_BUS = 8 * (SSD_BYTES_PER_SECTOR) * curssd->params.chip_xfer_latency * 1000;
}

void fcl_init () {
	int lru_size = 50000;
	ssd_t *currssd = getssd ( SSD );

	fcl_set_ssd_params ( currssd );
	//fcl_print_parameters ( stdout ) ;
	fcl_print_parameters ( outputfile) ;

	print_test_cost ();

	flash_total_pages =  ssd_get_total_pages ( currssd );

	flash_usable_pages = device_get_number_of_blocks (SSD)/FCL_PAGE_SIZE;
	flash_usable_pages = flash_usable_pages * fcl_params->fpa_max_pages_percent / 100;
	flash_usable_pages -= 10;
	flash_usable_sectors = flash_usable_pages * FCL_PAGE_SIZE; 

	fcl_initial_discard_pages ();

	printf (" ssd total pages = %d, usable pages = %d \n", flash_total_pages, flash_usable_pages ); 

	lru_size  = flash_usable_pages;

	//hdd_total_pages = device_get_number_of_blocks (HDD)/FCL_PAGE_SIZE;
	hdd_total_pages = device_get_number_of_blocks (HDD)/FCL_PAGE_SIZE;
	hdd_total_sectors = hdd_total_pages * FCL_PAGE_SIZE; 


	lru_init ( &fcl_cache_mgr, "LRU", lru_size, lru_size, 1, 0);
	if ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_FIXED)
		lru_set_dirty_size ( fcl_cache_mgr, lru_size - 1024, 1024);
	else
		lru_set_dirty_size ( fcl_cache_mgr, lru_size/2, lru_size-lru_size/2);

	lru_init ( &fcl_active_block_mgr, "AtiveBlockManager", lru_size, lru_size, 1, 0);

	lru_init ( &fcl_pending_mgr, "PengdingBlockManager", lru_size, lru_size, 1, 0);
		
	//ll_create ( &fcl_pending_mgr );
	reverse_map_create ( SSD, lru_size + 1 );

	// alloc queue memory 
	fcl_fore_q = ioqueue_createdefaultqueue();
	ioqueue_initialize (fcl_fore_q, 0);

	fcl_back_q = ioqueue_createdefaultqueue();
	ioqueue_initialize (fcl_back_q, 0);

	fcl_read_hit_tracker = (struct cache_manager **)mlru_init("W_HIT_TRACKER", fcl_hit_tracker_nsegment, flash_total_pages );
	fcl_write_hit_tracker = (struct cache_manager **)mlru_init("R_HIT_TRACKER", fcl_hit_tracker_nsegment, flash_total_pages );

	fcl_stat = (struct fcl_statistics *) malloc ( sizeof ( struct fcl_statistics) );
	memset ( fcl_stat, 0x00, sizeof ( struct fcl_statistics) );

	fprintf ( stdout, " Flash Cache Layer is initializing ... \n");
	printf (" FCL: Flash Cache Usable Size = %.2fGB \n", (double)flash_usable_pages / 256 / 1024);
	printf (" FCL: Hard Disk Usable Size = %.2fGB \n", (double)hdd_total_pages / 256 / 1024);
	printf (" FCL: Effective Cache Size = %.2fMB \n", (double) lru_size / 256 );


	sd_init(fcl_params->fpa_seq_detection_enable,  fcl_params->fpa_seq_unit_size ); // 512 * 256 sectors 
}

void print_hit_ratio_curve () {
	int i;
	int seg_size = fcl_hit_tracker_nsegment * 2;
	int step = flash_total_pages / seg_size ;

	for ( i = 1;i < seg_size+1 ; i ++ ) {
		printf( " %d, hit ratio = %f \n", i, fcl_predict_hit_ratio ( fcl_read_hit_tracker, fcl_hit_tracker_nsegment, step * i, flash_total_pages, FCL_READ));
	}

	for ( i = 1;i < seg_size+1 ; i ++ ) {
		printf( " %d, hit ratio = %f \n", i, fcl_predict_hit_ratio ( fcl_write_hit_tracker, fcl_hit_tracker_nsegment, step * i, flash_total_pages, FCL_WRITE));
	}
}

void fcl_exit () {

	fprintf ( stdout, " Flash Cache Layer is finalizing ... \n"); 

	//print_hit_ratio_curve ();

#undef fprintf 
	
	fprintf ( stdout , " fqueue size = %d, bqueue size = %d \n", 
			ioqueue_get_number_in_queue ( fcl_fore_q ), 
			ioqueue_get_number_in_queue ( fcl_back_q ) );

	fprintf ( outputfile , " FCL: Arrive Request Count = %d \n", fcl_stat->fstat_arrive_count );
	fprintf ( outputfile , " FCL: Complete Request Count = %d \n", fcl_stat->fstat_complete_count );

	//ASSERT ( fcl_stat->fstat_arrive_count == fcl_stat->fstat_complete_count );
	if ( fcl_stat->fstat_arrive_count == fcl_stat->fstat_complete_count ) {
		fprintf ( outputfile , " FCL: Arrive and complete request mis-matched = %d \n", fcl_stat->fstat_complete_count );


	}

	fprintf ( outputfile , " FCL: Dirty Count = %d (%.2fMB)\n", fcl_cache_mgr->cm_dirty_count, (double)fcl_cache_mgr->cm_dirty_count/256 );
	fprintf ( outputfile , " FCL: Clean Count = %d (%.2fMB)\n", fcl_cache_mgr->cm_clean_count, (double)fcl_cache_mgr->cm_clean_count/256 );

	fprintf ( stdout , " FCL: Arrive Request Count = %d \n", fcl_stat->fstat_arrive_count );
	fprintf ( stdout , " FCL: Complete Request Count = %d \n", fcl_stat->fstat_complete_count );



	reverse_map_free( SSD );

	CACHE_PRINT(fcl_cache_mgr, stdout);
	CACHE_PRINT(fcl_cache_mgr, outputfile);

	CACHE_CLOSE(fcl_cache_mgr);
	CACHE_CLOSE(fcl_active_block_mgr);
	CACHE_CLOSE(fcl_pending_mgr);
	//ll_release ( fcl_pending_mgr );

	fcl_fore_q->printqueuestats = TRUE;
	ioqueue_printstats( &fcl_fore_q, 1, " FCL Foreground: ");
	free (fcl_fore_q);

	fcl_back_q->printqueuestats = TRUE;
	ioqueue_printstats( &fcl_back_q, 1, " FCL Background: ");
	free (fcl_back_q);

	// free queue memory


	mlru_exit( fcl_read_hit_tracker, fcl_hit_tracker_nsegment);
	mlru_exit( fcl_write_hit_tracker, fcl_hit_tracker_nsegment);

	
	sd_exit();

	free ( fcl_params );
	free ( fcl_stat );

}

#if 0 
double fcl_predict_hit_count(struct cache_manager **lru_manager,int lru_num, int size,int max_pages,int is_read){
	int j;
	double total1 = 0, total2 = 0;
	double rhit1 = 0, rhit2 = 0;
	int index =0;
	int window;
	double h_window;
	double r_window;
	double n;
	double hit;

	window = max_pages/lru_num;
	index = size/window;
	if(size%window)
		index++;
		
	for(j = 0;j < index;j++){
		total1 = total2;
		rhit1 = rhit2;

		if(is_read == FCL_READ)
			total2 += lru_manager[j]->cm_hit;
		else if(is_read == FCL_WRITE)
			total2 += (lru_manager[j]->cm_hit);
		else if(is_read == FCL_READWRITE){
			total2 += (double)lru_manager[j]->cm_hit;
			rhit2 += (double)lru_manager[j]->cm_read_hit;
			if(lru_manager[j]->cm_hit < lru_manager[j]->cm_read_hit)
				j = j;
		}
	}
		
	
	n = (double)(size%window)/((double)window/10);
	h_window =(double)(total2 - total1)/10;
	//if(h_window == 0.0 && is_read == READWRITE)
	//	h_window = h_window;

	//if(h_window >= 1.0 && is_read == READWRITE)
	//	h_window = h_window;

	if(is_read == FCL_READWRITE){
		
		if(total1 != 0.0)
			rhit1/=total1;
		else
			rhit1 = 0.0;


		if(total2 != 0.0)
			rhit2/=total2;
		else
			rhit2 = 0.0;

		h_window = (double)(rhit2-rhit1)/10;
		total1 = rhit1;

	}

	if(is_read == FCL_READ)
		hit = (double)(total1 + h_window * n);
	else if(is_read == FCL_WRITE)
		hit = (double)(total1 + h_window * n);
	else if(is_read == FCL_READWRITE){
		hit =  (double)(total1 + h_window * n);

		if(hit < 0.0 || hit > 1.0){
			hit = hit;
		}
	}


	return hit;
}
#endif 

#if 0 
void print_parent_child_state ( ioreq_event *parent ) {

	printf ( " Parent opid = %d, blkno = %d,C = %d, A = %d, I = %d, P = %d \n", 
				parent->opid,
				parent->blkno, 
				ll_get_size ( parent->fcl_complete_list ), 
				ll_get_size ( parent->fcl_active_list ), 
				ll_get_size ( parent->fcl_inactive_list ), 
				ll_get_size ( parent->fcl_pending_list ) 
					);

}
#endif 

#if 0 
int fcl_resize_cache () {

	int dirty_diff = fcl_cache_mgr->cm_dirty_size - fcl_optimal_write_pages;
	int clean_diff = fcl_cache_mgr->cm_clean_size - fcl_optimal_read_pages;
	int target_dirty = 0;
	int target_clean = 0;
	int remain = 0;


	printf ( " Resizing cache ...\n");

	printf ( " dirty diff = %d, clean diff %d \n", dirty_diff, clean_diff );
	//ASSERT ( -1*dirty_diff == clean_diff );

	printf ( " clean free = %d, dirty free = %d \n", fcl_cache_mgr->cm_clean_free, fcl_cache_mgr->cm_dirty_free ); 

	printf ( " opclean size = %d, opdirty size = %d \n", fcl_optimal_read_pages, fcl_optimal_write_pages );
	printf ( " clean size = %d, dirty size = %d \n", fcl_cache_mgr->cm_clean_size, fcl_cache_mgr->cm_dirty_size ); 

	// increase 
	//if ( dirty_diff > FCL_MAX_DESTAGE ) 
	//	target_dirty = fcl_cache_mgr->cm_dirty_size + FCL_MAX_DESTAGE;
	

	lru_set_dirty_size ( fcl_cache_mgr, fcl_optimal_write_pages, fcl_optimal_read_pages );

	if ( fcl_cache_mgr->cm_dirty_free >= 0 && fcl_cache_mgr->cm_clean_free >= 0 ) {
		printf ( " \n ");
		return remain;
	}


	//printf ( " dirty diff = %d, clean diff =  %d \n", dirty_diff, clean_diff);
	printf ( " clean free = %d, dirty free = %d \n", fcl_cache_mgr->cm_clean_free, fcl_cache_mgr->cm_dirty_free ); 
	printf ( " clean count = %d, dirty count = %d \n", fcl_cache_mgr->cm_clean_count, fcl_cache_mgr->cm_dirty_count ); 
	printf ( " Total count = %d, Size = %d \n", fcl_cache_mgr->cm_clean_count + fcl_cache_mgr->cm_dirty_count, fcl_cache_mgr->cm_size ); 

	if ( fcl_cache_mgr->cm_dirty_free <  0 ) {
		dirty_diff = fcl_cache_mgr->cm_dirty_free * -1;	
		if ( dirty_diff > FCL_MAX_DESTAGE ) {
			dirty_diff = FCL_MAX_DESTAGE * 8 ;
			remain = 1;
		}

		printf (" **Destage %d dirty pages %.2fMB \n", dirty_diff, (double)dirty_diff/256 );

		fcl_destage_request ( dirty_diff );
	} 

	if ( fcl_cache_mgr->cm_clean_free < 0 ) {

		clean_diff = fcl_cache_mgr->cm_clean_free * -1;
		printf (" **Remove %d clean pages %.2fMB \n", clean_diff, (double)clean_diff/256 );

		fcl_invalid_request ( clean_diff );
	}




	ASSERT ( fcl_cache_mgr->cm_size  == fcl_cache_mgr->cm_dirty_size + fcl_cache_mgr->cm_clean_size );

	printf ( " fqueue = %d, bqueue = %d \n", 
				ioqueue_get_number_in_queue ( fcl_fore_q ),
				ioqueue_get_number_in_queue ( fcl_back_q ) );
	printf ("\n");


	return remain;
}
#endif 
