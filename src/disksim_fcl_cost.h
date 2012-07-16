
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: The header file 
* File Name		: disksim_fcl_cost.h
*/


#ifndef _DISKSIM_FCL_COST_H
#define _DISKSIM_FCL_COST_H

#define  HDD_CRPOS		(fcl_params->fpa_hdd_crpos) //us
#define  HDD_CWPOS		(fcl_params->fpa_hdd_cwpos) //us
#define  HDD_BANDWIDTH	(fcl_params->fpa_hdd_bandwidth) //mb/s

#define	SSD_PROG		(fcl_params->fpa_ssd_cprog) //us
#define	SSD_READ		(fcl_params->fpa_ssd_cread)	//us
#define SSD_ERASE		(fcl_params->fpa_ssd_cerase)//us
#define SSD_BUS			(fcl_params->fpa_ssd_cbus)  //us

#define SSD_NP			(fcl_params->fpa_ssd_np)    

void fcl_find_optimal_size ( struct cache_manager **write_hit_tracker, 
						 struct cache_manager **read_hit_tracker,
						 int tracker_num,
						 int total_pages,
						 int *read_optimal_pages,
						 int *write_optimal_pages);

void fcl_decay_hit_tracker(struct cache_manager **lru_manager,int lru_num);
void print_test_cost () ;
#endif // DISKSIM_FCL_COST_H
