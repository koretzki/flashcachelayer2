    
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

//#define HDD 0 // physically 0

// device numbering rule
//		logical	physical
// hdd0	0		0
// hdd1	1		1
// hdd2	2		2
// ssd0	3		0
// ssd2	4		1
// ssd2	5		2

#define MAX_DISK 2
#define MAX_CACHE 2

#define FCL_NUM_CACHE (fcl_params->fpa_num_cache)
#define FCL_NUM_DISK (fcl_params->fpa_num_disk)

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

#define	hdd_total_pages(d)		(fcl_params->fpa_hdd_total_pages[d])
#define	hdd_total_sectors(d)	(fcl_params->fpa_hdd_total_sectors[d])

#define fcl_hit_tracker_nsegment (fcl_params->fpa_hit_tracker_nsegment)

#define PAGE_TO_MB(x) ((double)x/256)
#define PAGE_TO_GB(x) ((double)x/256/1024)

#define FCL_BACKGROUND_TIMER	1 

#define fcl_io_read_pages	(fcl_stat->fstat_io_read_pages)
#define fcl_io_total_pages	(fcl_stat->fstat_io_total_pages)

#define devicenos                   (disksim->deviceinfo->devicenos)

struct fcl_parameters {
	int		fpa_num_cache;
	int		fpa_num_disk;
	int		fpa_page_size;
	double	fpa_max_pages_percent;
	int		fpa_bypass_cache;
	double	fpa_idle_detect_time;
	int		fpa_dirty_migration;
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
	double	fpa_ssd_cprog[MAX_CACHE];	//us
	double	fpa_ssd_cread[MAX_CACHE];	//us 
	double	fpa_ssd_cerase[MAX_CACHE];	//us
	double	fpa_ssd_cbus[MAX_CACHE];	//us
	int		fpa_ssd_np[MAX_CACHE];

	int		fpa_flash_total_pages[MAX_CACHE];
	int		fpa_flash_usable_pages[MAX_CACHE];
	int		fpa_flash_usable_sectors[MAX_CACHE];

	int		fpa_hdd_total_pages[MAX_DISK];
	int		fpa_hdd_total_sectors[MAX_DISK];
};

struct fcl_statistics { 
	int fstat_arrive_count;
	int fstat_complete_count;

	int fstat_cache_ref;
	int fstat_cache_hit;

	int fstat_io_read_pages;
	int fstat_io_write_pages;
	int fstat_io_total_pages;

	int fstat_dev_total_pages[MAX_DISK+MAX_CACHE];
	int fstat_dev_read_pages[MAX_DISK+MAX_CACHE];
	int fstat_dev_write_pages[MAX_DISK+MAX_CACHE];

	int fstat_seq_total_pages;
	int fstat_seq_read_pages;
	int fstat_seq_write_pages;
	double fstat_idle_start;
	double fstat_idle_time;
	int fstat_idle_count;	
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

struct lru_node *fcl_lookup_active_list ( int devno, int blkno ) ;

void fcl_event_next_foreground_request () ;
void fcl_event_next_background_request () ;

void fcl_update_workload_tracker ( ioreq_event *parent ) ;
struct lru_node *fcl_cache_search( int hddno,  int blkno ) ;
struct lru_node *fcl_cache_presearch( int hddno, int blkno ) ;
struct lru_node *fcl_alloc_node( int devno, int blkno ) ;
void fcl_classify_child_request ( ioreq_event *parent, ioreq_event *child, int blkno ) ;
void fcl_insert_active_temp_list ( ioreq_event *parent, ioreq_event *chld, int blkno ) ;
ioreq_event *fcl_create_parent (int devno, int blkno,int bcount, double time, int flags) ;
int fcl_all_queue_empty () ;

#endif // ifndef _DISKSIM_FCL_H 
