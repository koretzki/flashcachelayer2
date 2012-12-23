    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: The header file 
* File Name		: disksim_fcl.h
*/

#include "disksim_global.h"
#include "disksim_fcl_cache.h"


#ifndef _DISKSIM_FCL_H
#define _DISKSIM_FCL_H

#define FCL_PAGE_SIZE (fcl_params->fpa_page_size)
#define FCL_PAGE_SIZE_BYTE (FCL_PAGE_SIZE * 512)

#define HDD 0 // physically 0
#define NUM_HDD 1


#define TLC_CACHE 0 // physically 1
#define SLC_CACHE 1 // physically 2
#define MAX_CACHE 2

#define FCL_NUM_CACHE (fcl_params->fpa_num_cache)

#define FCL_READ DISKSIM_READ
#define FCL_WRITE DISKSIM_WRITE
#define FCL_READWRITE	4

#define FCL_OPERATION_NORMAL 	1
#define FCL_OPERATION_DESTAGING 2 
#define FCL_OPERATION_STAGING	3


#define FCL_MAX_STAGE 	1

#define FCL_CACHE_FIXED		1
#define FCL_CACHE_RW		2
#define FCL_CACHE_OPTIMAL 	3
#define FCL_CACHE_BYPASS 	4

#define FCL_REPLACE_DIRTY 	0	
#define FCL_REPLACE_CLEAN	1
#define FCL_REPLACE_ANY     2	

//#define FCL_MAX_REQ_SIZE  256
#define FCL_MAX_REQ_SIZE  512

// Page Unit (e.g., 4KB)
#define FCL_MAX_DESTAGE			(fcl_params->fpa_max_destage_size) 
#define FCL_MAX_RESIZE			(fcl_params->fpa_max_resize_size)

#define FCL_FORE_Q_DEPTH		(fcl_params->fpa_fore_outstanding)
#define FCL_FORE_Q_DEPTH_TEMP	(fcl_params->fpa_fore_outstanding_temp)
#define FCL_BACK_Q_DEPTH		(fcl_params->fpa_back_outstanding)

#define	flash_total_pages(d)	(fcl_params->fpa_flash_total_pages[d])
#define	flash_usable_pages(d)	(fcl_params->fpa_flash_usable_pages[d])
#define	flash_usable_sectors(d)	(fcl_params->fpa_flash_usable_sectors[d])

#define	hdd_total_pages			(fcl_params->fpa_hdd_total_pages)
#define	hdd_total_sectors		(fcl_params->fpa_hdd_total_sectors)

#define fcl_hit_tracker_nsegment (fcl_params->fpa_hit_tracker_nsegment)

#define PAGE_TO_MB(x) ((double)x/256)
#define PAGE_TO_GB(x) ((double)x/256/1024)

#define FCL_BACKGROUND_TIMER	0 

#define fcl_io_read_pages	(fcl_stat->fstat_io_read_pages)
#define fcl_io_total_pages	(fcl_stat->fstat_io_total_pages)

#define devicenos                   (disksim->deviceinfo->devicenos)

struct fcl_parameters {
	int		fpa_num_cache;
	int		fpa_page_size;
	double	fpa_max_pages_percent;
	int		fpa_bypass_cache;
	double	fpa_idle_detect_time;
	int		fpa_partitioning_scheme;
	int		fpa_background_activity;
	double	fpa_overhead;

	int		fpa_max_resize_size;
	int		fpa_max_destage_size;

	int		fpa_seq_detection_enable;
	int		fpa_seq_unit_size;

	// RW-FCL and OP_FCL
	int		fpa_resize_period;
	int		fpa_resize_next;

	int		fpa_hit_tracker_nsegment;
	double	fpa_hit_tracker_decayfactor;

	int		fpa_ondemand_group_destage;
	int		fpa_background_group_destage;
	int		fpa_fore_outstanding;
	int		fpa_fore_outstanding_temp;
	int		fpa_back_outstanding;

	// HDD Cost 
	double	fpa_hdd_crpos;		//us
	double	fpa_hdd_cwpos;		//us
	double	fpa_hdd_bandwidth;	// mb/s

	// SSD Cost 
	double	fpa_ssd_cprog;	//us
	double	fpa_ssd_cread;	//us 
	double	fpa_ssd_cerase;	//us
	double	fpa_ssd_cbus;	//us
	int		fpa_ssd_np;

	int		fpa_flash_total_pages[MAX_CACHE];
	int		fpa_flash_usable_pages[MAX_CACHE];
	int		fpa_flash_usable_sectors[MAX_CACHE];

	int		fpa_hdd_total_pages;
	int		fpa_hdd_total_sectors;	
};

struct fcl_statistics { 
	int fstat_arrive_count;
	int fstat_complete_count;

	int fstat_io_read_pages;
	int fstat_io_write_pages;
	int fstat_io_total_pages;

	int fstat_dev_total_pages[NUM_HDD+MAX_CACHE];
	int fstat_dev_read_pages[NUM_HDD+MAX_CACHE];
	int fstat_dev_write_pages[NUM_HDD+MAX_CACHE];
};

extern struct fcl_parameters *fcl_params;
extern struct fcl_statistics *fcl_stat;

//extern int fcl_io_read_pages, fcl_io_total_pages;


void fcl_request_arrive (ioreq_event *);
void fcl_request_complete (ioreq_event *);
void fcl_init () ;
void fcl_exit () ;

void fcl_remove_complete_list ( ioreq_event *);
void fcl_make_stage_req (ioreq_event *parent, int blkno);
void fcl_make_destage_req (ioreq_event *parent, int blkno, int replace);
void _fcl_make_destage_req ( ioreq_event *parent, struct lru_node *ln, int list_index ) ;
void _fcl_make_stage_req ( ioreq_event *parent, struct lru_node *ln, int list_index ) ;
void fcl_issue_pending_child ( ioreq_event *parent ) ;

double fcl_predict_hit_ratio(struct cache_manager **lru_manager,int lru_num, int size,int max_pages,int is_read);

int fcl_destage_request ( int destage_num) ;
int fcl_invalid_request ( int devno, int invalid_num) ;

struct lru_node *fcl_lookup_active_list ( int blkno ) ;

void fcl_event_next_foreground_request () ;
void fcl_event_next_background_request () ;

void fcl_update_workload_tracker ( ioreq_event *parent ) ;
struct lru_node *fcl_cache_search( int blkno ) ;
struct lru_node *fcl_cache_presearch( int blkno ) ;

#endif // ifndef _DISKSIM_FCL_H 
