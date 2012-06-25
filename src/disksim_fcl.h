    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: 
*
*/

#include "disksim_global.h"

#ifndef _DISKSIM_FCL_H
#define _DISKSIM_FCL_H

#define FCL_PAGE_SIZE 8

#define SSD 0
#define HDD 1

#define FCL_OPERATION_NORMAL 1
#define FCL_OPERATION_DESTAGING 2 
#define FCL_OPERATION_STAGING 3

struct fcl_parameters {
	int fpa_max_pages;
	int fpa_cache_policy;
	int fpa_background_activity;
};

void fcl_request_arrive (ioreq_event *);
void fcl_request_complete (ioreq_event *);
void fcl_init () ;
void fcl_exit () ;

#endif // ifndef _DISKSIM_FCL_H 
