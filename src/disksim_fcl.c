    
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
struct cache_manager *fcl_active_block_manager;
listnode			 *fcl_pending_manager; 
int 				 fcl_opid = 0;
int					 fcl_outstanding = 10000;
int 				 flash_max_pages = 50000;
int 				 flash_max_sectors = 50000 * FCL_PAGE_SIZE;
int 				 hdd_max_pages = 50000;
int 				 hdd_max_sectors = 50000 * FCL_PAGE_SIZE;

#define fprintf 
#define printf


ioreq_event *fcl_create_child (ioreq_event *parent, int devno, int blkno, int bcount, unsigned int flags){
	ioreq_event *child = NULL;

	child = ioreq_copy ( parent );
//	child  = (ioreq_event *) getfromextraq(); // DO NOT Use !!	

	//child->fcl_cbp = (fcl_cb *)malloc(sizeof(fcl_cb));
	//memset ( child->fcl_cbp, 0x00, sizeof(fcl_cb));

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

	printf ( " Debug print \n" );

	while ( last != NULL ) {
		printf ( " List[%d]: %p %d %d dev = %d, flag = %d  \n", list_index, last, last->blkno, 
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
	
	ll_create ( (listnode **) &parent->fcl_complete_list );
	ll_create ( (listnode **) &parent->fcl_active_list );
	ll_create ( (listnode **) &parent->fcl_inactive_list );
	ll_create ( (listnode **) &parent->fcl_pending_list );


}

void fcl_issue_next_child ( ioreq_event *parent ){
	ioreq_event *req;
	int flags = -1;
	int devno = -1;

	fprintf ( stdout, " issue next = %d \n", parent->fcl_event_ptr );


	ASSERT ( parent->fcl_event_count[parent->fcl_event_ptr] != 0 );

	req = parent->fcl_event_list[parent->fcl_event_ptr];
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

	parent->fcl_event_list[parent->fcl_event_ptr] = NULL;

	parent->fcl_event_ptr++;

}

void fcl_generate_child_request ( ioreq_event *parent, int devno, int blkno, int flags, int list_index )
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
	int filling = 0;
	struct lru_node *ln = NULL;
	listnode *node;

	node = CACHE_SEARCH(fcl_cache_manager, blkno);

	// hit case  
	if(node){
		// remove this node to move the MRU position
		ln = CACHE_REMOVE(fcl_cache_manager, node);

		// TODO: this child request must be blocked  

		if ( ln->cn_flag == FCL_CACHE_FLAG_FILLING ) {
			filling = 1;
			ASSERT ( ln->cn_flag == FCL_CACHE_FLAG_SEALED );	
		}


	}else{ // miss case 
		// evict the LRU position node from the LRU list
		ln = CACHE_REPLACE(fcl_cache_manager, 0);
		
		if ( ln ) {
			ASSERT ( ln->cn_flag == FCL_CACHE_FLAG_SEALED );
		}

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
	if ( parent->flags & READ ) 
		fcl_generate_child_request ( parent, SSD, ln->cn_ssd_blk, READ, FCL_EVENT_MAX-2);
	else
		fcl_generate_child_request ( parent, SSD, ln->cn_ssd_blk, WRITE, FCL_EVENT_MAX-1);

	//if ( !filling )

	CACHE_INSERT(fcl_cache_manager, ln);

}


listnode *fcl_lookup_active_list ( int blkno ) {
	listnode *node;

	node = CACHE_PRESEARCH( fcl_active_block_manager, blkno );
	if ( node ) {
		return node;
	}
	return NULL;
}

void fcl_insert_active_list ( ioreq_event *child ) {
	struct lru_node *ln = NULL;	

	ln = CACHE_ALLOC ( fcl_active_block_manager, NULL, child->blkno );
	ln->cn_flag = 0;
	ln->cn_temp1 = (void *)child;
	ll_create ( (listnode **) &ln->cn_temp2 );

	CACHE_INSERT ( fcl_active_block_manager, ln );
}

void fcl_insert_inactive_list ( ioreq_event *child ) {
	struct lru_node *ln = NULL;	
	listnode *node;

	node = CACHE_PRESEARCH ( fcl_active_block_manager, child->blkno );
	ln = (struct lru_node *)node->data;

	ll_insert_at_tail ( ln->cn_temp2, child );
}

void fcl_classify_child_request ( ioreq_event *parent, ioreq_event *child, int blkno ) {
	listnode *node;

	node = fcl_lookup_active_list ( blkno );

	if ( !node ) { // insert active list 
		printf ( " %f insert active list blkno = %d, %d \n", simtime, blkno, child->blkno );
		fcl_insert_active_list ( child );
		ll_insert_at_tail ( parent->fcl_active_list, child );
	} else { // insert inactive list 
		printf ( " %f insert inactive list blkno = %d, %d \n", simtime, blkno, child->blkno );
		fcl_insert_inactive_list ( child );
		ll_insert_at_tail ( parent->fcl_inactive_list, child );
	}

}


void fcl_make_pending_list ( ioreq_event *parent ) {
	int page_count = parent->bcount / FCL_PAGE_SIZE;
	int devno = parent->devno;
	int flags = parent->flags;
	int blkno;
	int i;

	ioreq_event *child;
	listnode *node;

	for (i = 0; i < page_count; i++){
		blkno = parent->blkno + i * FCL_PAGE_SIZE;
		
		child = fcl_create_child (  parent, devno, blkno, 
								FCL_PAGE_SIZE, flags ); 
	
		child->fcl_parent = parent;

		ll_insert_at_tail ( parent->fcl_pending_list, child );
	} 

}

void fcl_split_parent_request (ioreq_event *parent) {
	
	int i;
	int blkno;
	int active_page_count;// = ll_get_size ( parent->fcl_active_list );
	listnode *node;
	listnode *active_list;
	ioreq_event *child;

	//debug 

	int total_req = 0;

	for ( i = 0; i < parent->fcl_event_num; i++) {
		total_req += parent->fcl_event_count[i];
		ASSERT ( parent->fcl_event_list[i] == NULL );
	}


	ASSERT ( total_req == 0 );

	ASSERT ( parent->bcount % FCL_PAGE_SIZE == 0 );
	ASSERT ( ll_get_size ( parent->fcl_active_list ) != 0 );

	active_page_count = ll_get_size ( parent->fcl_active_list );

	node = ((listnode *)parent->fcl_active_list)->next ;

	for (i = 0; i < active_page_count; i++){
		ASSERT ( node != NULL );
		child = (ioreq_event *)node->data;
		blkno = child->blkno;

		printf (" blkno = %d \n", blkno );
		fcl_distribute_request ( parent, blkno ); 
		node = node->next;
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
				fprintf ( stdout, " [%d] %p %d \n", j, 
						parent->fcl_event_list[j], parent->fcl_event_count[j]);
			}

		}
	}

	for ( i = 0; i < FCL_EVENT_MAX; i++){
		
		if ( parent->fcl_event_count [i] ) {
			parent->fcl_event_num ++ ;
			fprintf ( stdout, " [%d] %p %d \n", i, 
					parent->fcl_event_list[i], parent->fcl_event_count[i]);
		}
	}

}

// pending childs => active childs 
// active childs => SSD, HDD childs 
void fcl_make_child_request (ioreq_event *parent) {

	int i, j;
	
	print_parent_child_state ( parent );
	// pending requests go to Active or Inactive 
	fcl_issue_pending_child ( parent );

	print_parent_child_state ( parent );

	//ASSERT ( ll_get_size ( parent->fcl_active_list ) != 0 );
	if ( ll_get_size ( parent->fcl_active_list ) ) {
		// Active requests -> SSD, HDD
		fcl_split_parent_request ( parent );

		parent->fcl_event_ptr = 0;
		parent->fcl_event_num = 0;

		fcl_remove_empty_node ( parent );

		// consecutive requests will be merged 
		// [blkno: 1],[blkno: 2] => [blkno: 1-2]
		fcl_make_merge_next_request ( parent->fcl_event_list, parent->fcl_event_count );
	} else {

		printf ( " Pending requests go to inactive list .. \n" );
		//ASSERT ( 0 );
	}

	
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
	
	fcl_make_pending_list ( parent );

	// parent request will be splited and distributed into SSD and HDD 
	fcl_make_child_request ( parent );
	
//	ASSERT ( ll_get_size ( parent->fcl_active_list ) != 0 );

	if ( ll_get_size ( parent->fcl_active_list ) ) {
		// issue requests to IODRIVER
		fcl_issue_next_child ( parent ); 
	}

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

	printf ( " Queue # of reqs in queue = %d \n", ioqueue_get_number_in_queue ( fcl_global_queue ));

	ASSERT ( ioqueue_get_number_in_queue ( fcl_global_queue ) < fcl_outstanding);

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
/*
void fcl_seal_complete_request ( ioreq_event *parent ) {
	listnode *node;
	struct lru_node *ln;
	int page_count = parent->bcount / FCL_PAGE_SIZE;
	int i;
	int blkno;
	

	for (i = 0; i < page_count; i++){
		blkno = parent->blkno + i * FCL_PAGE_SIZE;
		node = CACHE_PRESEARCH(fcl_cache_manager, blkno);

		ASSERT ( node != NULL );
	
		//continue;
		if ( node ) {
			ln = ( struct lru_node *) node->data;
			ln->cn_flag = FCL_CACHE_FLAG_SEALED;
		}
	} 

}
*/

void fcl_seal_complete_request ( ioreq_event *parent ) {
	listnode *lru_node;
	struct lru_node *ln;

	listnode *active_node;
	ioreq_event *child;
	int active_page_count = ll_get_size ( parent->fcl_active_list );

	int i;

	active_node = ((listnode *)parent->fcl_active_list)->next;

	for (i = 0; i < active_page_count; i++){

		ASSERT ( active_node != NULL );

		child = (ioreq_event *)active_node->data;

		//printf (" seal blk = %d \n", child->blkno );
		lru_node = CACHE_PRESEARCH(fcl_cache_manager, child->blkno);

		ASSERT ( lru_node != NULL );
	
		if ( lru_node ) {
			ln = ( struct lru_node *) lru_node->data;
			ln->cn_flag = FCL_CACHE_FLAG_SEALED;
		}

		active_node = active_node->next;
	} 
}
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

void fcl_insert_pending_manager ( ioreq_event * parent) {

	listnode *pending_parent = fcl_pending_manager->next;
	int pending_parent_count = ll_get_size ( fcl_pending_manager );

	int exist = 0;
	int i;

	for ( i = 0; i < pending_parent_count; i++ ) {
		if ( parent == pending_parent->data ) {
			exist = 1;
			//ASSERT ( 0 );
			break;
		}
		pending_parent = pending_parent->next;
	}
 
	if ( !exist ) {
		ll_insert_at_tail ( fcl_pending_manager, parent ) ;
	}

	printf (" *fcl_pending_manager: pending I/Os = %d \n", ll_get_size ( fcl_pending_manager ) );

	if ( exist ) { 
		//ASSERT ( 0 );
	}
}

void fcl_remove_inactive_list ( ioreq_event *parent, ioreq_event *child) {
	listnode *inactive_list = ((listnode *)parent->fcl_inactive_list);
	listnode *inactive_node = inactive_list->next;
	listnode *found_node = NULL;
	int inactive_count = ll_get_size ( inactive_list );
	int i;

	for ( i = 0; i < inactive_count; i++ ) {

		if ( inactive_node->data == child ) {
			found_node = inactive_node;
			break;
		}

		inactive_node = inactive_node->next;
	}
	
	ASSERT ( found_node != NULL );

	if ( found_node ) {
		ll_release_node ( parent->fcl_inactive_list, found_node );
	}

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
		ll_insert_at_tail ( parent->fcl_pending_list, child );
		
		fcl_insert_pending_manager ( parent );	

		//print_parent_child_state ( parent );

		inactive_node = inactive_node->next;
	}

	printf (" inactive size = %d \n", ll_get_size ( inactive_list ) );
	ll_release ( inactive_list );
}

void fcl_remove_active_list ( ioreq_event *parent ) {
	listnode *lru_node;
	struct lru_node *ln;

	listnode *active_node;
	ioreq_event *child;
	int active_page_count = ll_get_size ( parent->fcl_active_list );

	listnode *curr_complete_list;
	listnode *complete_node;
	int i;


	ll_create ( &curr_complete_list );

	// move active child in active list to complete list   
	active_node = ((listnode *)parent->fcl_active_list)->next;
	for (i = 0; i < active_page_count; i++){

		ASSERT ( active_node != NULL );

		child = (ioreq_event *)active_node->data;
	
		ll_insert_at_tail ( parent->fcl_complete_list, child );
		ll_insert_at_tail ( curr_complete_list, (void *) child->blkno );

		active_node = active_node->next;
	} 

	while ( ll_get_size ( parent->fcl_active_list ) ) {
		ll_release_tail ( parent->fcl_active_list );
	}


	ASSERT ( ll_get_size ( parent->fcl_active_list ) == 0 );


	// remove complete block in fcl_active_block manager 
	complete_node = curr_complete_list->next;
	for (i = 0; i < active_page_count; i++){
		int blkno = 0;

		ASSERT ( complete_node != NULL );

		blkno = (int)complete_node->data;

		printf (" remove active blk = %d in active block manager \n", blkno );
		lru_node = CACHE_PRESEARCH(fcl_active_block_manager, blkno);

		ASSERT ( lru_node != NULL );
	
		if ( lru_node ) {
			ln = ( struct lru_node *) lru_node->data;
			
			if ( ll_get_size ( (listnode *)ln->cn_temp2 )){

				//printf (" It has blocking child reqeusts of some parents, parent blkno=%d \n",
				//		parent->blkno);

				fcl_move_pending_list ( ln->cn_temp2 );
				ln->cn_temp2 = NULL;
				//ASSERT ( ll_get_size ((listnode *)ln->cn_temp2 ) == 0);	
			}

			CACHE_REMOVE ( fcl_active_block_manager, lru_node ); 

			free (ln);
		}

		complete_node = complete_node->next;
	} 

	ll_release ( curr_complete_list );

	print_parent_child_state ( parent );

}


void fcl_remove_complete_list ( ioreq_event *parent ) {

	listnode *complete_node;
	ioreq_event *child;

	int complete_page_count = ll_get_size ( parent->fcl_complete_list );
	int i;

	complete_node = ((listnode *)parent->fcl_complete_list)->next;
	printf (" Remove complete list \n");
	for (i = 0; i < complete_page_count; i++){

		ASSERT ( complete_node != NULL );

		child = (ioreq_event *)complete_node->data;
		addtoextraq ( (event *)child );
		complete_node = complete_node->next;
	} 

	while ( ll_get_size ( parent->fcl_complete_list ) ) {
		ll_release_tail ( parent->fcl_complete_list );
	}

	ASSERT ( ll_get_size ( parent->fcl_complete_list ) == 0 );

}

// pending childs go to ACTIVE or InACTIVE lists 
void fcl_issue_pending_child ( ioreq_event *parent ) {

	listnode *pending_node = parent->fcl_pending_list;
	ioreq_event *child;

	int pending_count = ll_get_size ( pending_node );
	int i;

	pending_node = pending_node->next;

	for ( i = 0; i < pending_count ; i++ ) {
		child = (ioreq_event *)pending_node->data;	

		ASSERT ( parent == child->fcl_parent );
		fcl_classify_child_request ( parent, child, child->blkno );

		pending_node = pending_node->next;
	}

	for ( i = 0; i < pending_count; i++ ) {
		ll_release_tail ( parent->fcl_pending_list );
	}

	ASSERT ( ll_get_size ( parent->fcl_pending_list ) == 0 );

	print_parent_child_state ( parent );

//	ASSERT (0);
}


void fcl_issue_pending_parent (){
	listnode *pending_next = fcl_pending_manager->next;
	listnode *pending_del = NULL;
	ioreq_event *parent;
	int pending_count = ll_get_size ( fcl_pending_manager );
	int i;
		
	int debug_count = 0;

	for ( i = 0; i < pending_count; i++ ) {
		parent = pending_next->data;
		print_parent_child_state ( parent );
		
		pending_del = pending_next;
		pending_next = pending_next->next;

		if ( ll_get_size ( parent->fcl_active_list ) == 0 && 
			 ll_get_size ( parent->fcl_inactive_list ) == 0 ) {
			
			fcl_make_child_request ( parent );
			//ASSERT ( ll_get_size ( parent->fcl_active_list ) != 0 ) ;
			if ( ll_get_size ( parent->fcl_active_list ) ) {
				fcl_issue_next_child ( parent );
			}

			debug_count ++;

			ll_release_node ( fcl_pending_manager, pending_del );
		}
	}

	if ( debug_count ) {
		printf ( " > %d of %d pending I/Os have been issued \n", debug_count, pending_count );
		//ASSERT ( 0);
	}


}	

void fcl_request_complete (ioreq_event *child){
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
		fcl_issue_next_child ( parent );
	}

	addtoextraq ((event *) child);

	for ( i = 0; i < parent->fcl_event_num; i++) {
		total_req += parent->fcl_event_count[i];
	}

	// all child requests are complete 
	if ( total_req  == 0 ) { 

		//ASSERT ( ll_get_size ( parent->fcl_inactive_list ) == 0 );
		fcl_seal_complete_request ( parent );
		fcl_remove_active_list ( parent );
		
		printf (" * %d all active childs are completed \n", parent->opid);
		print_parent_child_state ( parent );

		// active request exist
		ASSERT ( ll_get_size ( parent->fcl_active_list ) == 0 );

	}

	// issue pending I/Os 
	if ( ll_get_size ( fcl_pending_manager ) ){

		printf (" Try to issue pending I/Os \n" );

		fcl_issue_pending_parent ();

	//	ASSERT ( 0 );
	}

	if ( ll_get_size ( parent->fcl_complete_list ) ==  parent->bcount/FCL_PAGE_SIZE ){

		ASSERT ( ll_get_size ( parent->fcl_active_list ) == 0 && 
				ll_get_size ( parent->fcl_inactive_list ) == 0 );

		fcl_remove_complete_list ( parent );

		ll_release ( parent->fcl_complete_list );
		ll_release ( parent->fcl_active_list );
		ll_release ( parent->fcl_inactive_list );
		ll_release ( parent->fcl_pending_list );

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

	lru_size = flash_max_pages;

	printf (" FCL: Flash Cache Usable Size = %.2fGB \n", (double)flash_max_pages / 256 / 1024);

	hdd_max_pages = ssd_elem_export_size2 ( hdd );
	hdd_max_sectors = hdd_max_pages * FCL_PAGE_SIZE;

	printf (" FCL: Hard Disk Usable Size = %.2fGB \n", (double)hdd_max_pages / 256 / 1024);

	lru_init ( &fcl_cache_manager, "LRU", lru_size, lru_size, 1, 0);
	lru_init ( &fcl_active_block_manager, "AtiveBlockManager", lru_size, lru_size, 1, 0);

	reverse_map_create ( lru_size+1 );

	// alloc queue memory 
	//fcl_global_queue = malloc(sizeof(ioqueue));
	fcl_global_queue = ioqueue_createdefaultqueue();
	ioqueue_initialize (fcl_global_queue, 0);

	constintarrtime = 5.0;

	ll_create ( &fcl_pending_manager );

}

void fcl_exit () {

	fprintf ( stdout, " Flash Cache Layer is finalizing ... \n"); 

	reverse_map_free();

	CACHE_CLOSE(fcl_cache_manager, 1);
	CACHE_CLOSE(fcl_active_block_manager, 0);

	fcl_global_queue->printqueuestats = TRUE;
	ioqueue_printstats( &fcl_global_queue, 1, " FCL : ");

	ll_release ( fcl_pending_manager );
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
