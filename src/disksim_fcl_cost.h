
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: 
*
*/


#ifndef _DISKSIM_FCL_COST_H
#define _DISKSIM_FCL_COST_H

void fcl_find_optimal_size ( struct cache_manager **write_hit_tracker, 
						 struct cache_manager **read_hit_tracker,
						 int tracker_num,
						 int total_pages,
						 int *read_optimal_pages,
						 int *write_optimal_pages);

void fcl_decay_hit_tracker(struct cache_manager **lru_manager,int lru_num);

#endif // DISKSIM_FCL_COST_H
