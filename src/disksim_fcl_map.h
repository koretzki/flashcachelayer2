    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: 
*
*/


#ifndef _DISKSIM_FCL_MAP_H
#define _DISKSIM_FCL_MAP_H


int reverse_get_blk(int ssdblk);
void reverse_map_create(int max);
int reverse_map_alloc_blk(int hdd_blk);
int reverse_map_release_blk(int ssd_blk);
void reverse_map_free();

#endif // ifndef _DISKSIM_FCL_MAP_H 
