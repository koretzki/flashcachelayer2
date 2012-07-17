    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: The header file
* File Name		: disksim_fcl_map.h
*/


#ifndef _DISKSIM_FCL_MAP_H
#define _DISKSIM_FCL_MAP_H

#define FCL_MAX_MAPTABLE 2

struct fcl_mapping_table {
	int	fm_devno;
	int *fm_reverse_map;
	int fm_reverse_free;
	int fm_reverse_used;
	int fm_reverse_alloc;
	int fm_reverse_max_pages;
	int fm_reverse_wait_pages;
	listnode *fm_reverse_freeq;
};

int reverse_get_blk(int devno,int ssdblk);
void reverse_map_create(int devno, int max);
int reverse_map_alloc_blk(int devno, int hdd_blk);
int reverse_map_release_blk(int devno, int ssd_blk);
void reverse_map_free(int devno);
void reverse_map_discard_freeblk (int devno) ;

#endif // ifndef _DISKSIM_FCL_MAP_H 
