    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: 
*
*/

#include "disksim_global.h"
#include "disksim_fcl_cache.h"


#ifndef _DISKSIM_FCL_H
#define _DISKSIM_FCL_H

#define FCL_PAGE_SIZE (fcl_params->fpa_page_size)

#define SSD 1 
#define HDD 0

#define FCL_OPERATION_NORMAL 1
#define FCL_OPERATION_DESTAGING 2 
#define FCL_OPERATION_STAGING 3

#define FCL_MAX_DESTAGE 128
#define FCL_MAX_STAGE 1

struct fcl_parameters {
	int fpa_page_size;
	double fpa_max_pages_percent;
	int fpa_bypass_cache;
	double fpa_idle_detect_time;
	int fpa_cache_policy;
	int fpa_background_activity;
	double	fpa_overhead;
};

void fcl_request_arrive (ioreq_event *);
void fcl_request_complete (ioreq_event *);
void fcl_init () ;
void fcl_exit () ;

void fcl_remove_complete_list ( ioreq_event *);
void fcl_make_stage_req (ioreq_event *parent, int blkno);
void fcl_make_destage_req (ioreq_event *parent, int blkno);
void _fcl_make_destage_req ( ioreq_event *parent, struct lru_node *ln, int list_index ) ;
void _fcl_make_stage_req ( ioreq_event *parent, struct lru_node *ln, int list_index ) ;
void fcl_issue_pending_child ( ioreq_event *parent ) ;

#endif // ifndef _DISKSIM_FCL_H 
