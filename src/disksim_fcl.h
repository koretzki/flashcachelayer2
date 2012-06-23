    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: 
*
*/


#ifndef _DISKSIM_FCL_H
#define _DISKSIM_FCL_H

#define FCL_PAGE_SIZE 8

#define SSD 0
#define HDD 1

#define FCL_OPERATION_NORMAL 1
#define FCL_OPERATION_DESTAGING 2 
#define FCL_OPERATION_STAGING 3

void fcl_request_arrive (ioreq_event *curr) ;
void fcl_request_complete (ioreq_event *curr) ;
void fcl_init () ;
void fcl_exit () ;

#endif // ifndef _DISKSIM_FCL_H 
