/*
 * DiskSim Storage Subsystem Simulation Environment (Version 4.0)
 * Revision Authors: John Bucy, Greg Ganger
 * Contributors: John Griffin, Jiri Schindler, Steve Schlosser
 *
 * Copyright (c) of Carnegie Mellon University, 2001-2008.
 *
 * This software is being provided by the copyright holders under the
 * following license. By obtaining, using and/or copying this software,
 * you agree that you have read, understood, and will comply with the
 * following terms and conditions:
 *
 * Permission to reproduce, use, and prepare derivative works of this
 * software is granted provided the copyright and "No Warranty" statements
 * are included with all reproductions and derivative works and associated
 * documentation. This software may also be redistributed without charge
 * provided that the copyright and "No Warranty" statements are included
 * in all redistributions.
 *
 * NO WARRANTY. THIS SOFTWARE IS FURNISHED ON AN "AS IS" BASIS.
 * CARNEGIE MELLON UNIVERSITY MAKES NO WARRANTIES OF ANY KIND, EITHER
 * EXPRESSED OR IMPLIED AS TO THE MATTER INCLUDING, BUT NOT LIMITED
 * TO: WARRANTY OF FITNESS FOR PURPOSE OR MERCHANTABILITY, EXCLUSIVITY
 * OF RESULTS OR RESULTS OBTAINED FROM USE OF THIS SOFTWARE. CARNEGIE
 * MELLON UNIVERSITY DOES NOT MAKE ANY WARRANTY OF ANY KIND WITH RESPECT
 * TO FREEDOM FROM PATENT, TRADEMARK, OR COPYRIGHT INFRINGEMENT.
 * COPYRIGHT HOLDERS WILL BEAR NO LIABILITY FOR ANY USE OF THIS SOFTWARE
 * OR DOCUMENTATION.
 *
 */

/*
 * A sample skeleton for a system simulator that calls DiskSim as
 * a slave.
 *
 * Contributed by Eran Gabber of Lucent Technologies - Bell Laboratories
 *
 * Usage:
 *	syssim <parameters file> <output file> <max. block number>
 * Example:
 *	syssim parv.seagate out 2676846
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include<ctype.h>

#include "ssd.h"
#include "ssd_clean.h"
#include"red_black_tree.h"
#include "disksim_interface.h"
#include "disksim_rand48.h"
#include "disksim_global.h"
#include "disksim_iotrace.h"
#include "disksim_hptrace.h"
#include "disksim_interface_private.h"
#include "ssd_utils.h"
#include "wow_driver.h"
#include "cache.h"

static struct disksim_interface *disksim_inf;
SysTime now = 0;		/* current time */
SysTime RealTime = 0.0;
static SysTime next_event = -1;	/* next event */
static int completed = 0;	/* last request was completed */
static Stat st;


int g_DestagePtr = 0;
//listnode *g_Queue;
//listnode *g_FreeQueue;
//listnode *g_Hash[WOW_HASH_NUM];
//listnode *g_FreeHash[WOW_HASH_NUM];
//listnode *p_FreeQueue;
listnode *req_SSD_WQlist;
listnode *req_SSD_RQlist;
listnode *req_HDD_WQlist;
listnode *req_HDD_RQlist;


float overprovision_rate = 0.80;
int Pages_Per_Group = 1024;
int Wow_HDD_Max_Pages;
int Wow_SSD_Max_Pages;
int Wow_HDD_Num_Groups;
int Wow_Free_Flash_Count;
int Wow_Occupancy_flash_count;
int Wow_High_Water = 0;
int Wow_Low_Water = 0;

int Wow_Max_High_Water = 0;

int Wow_Use_NARP = 0;

long long wow_hit = 0;
long long wow_ref = 0;
long long wow_destage_count = 0;
long long wow_stage_count = 0;
long long wow_read_hit = 1;
long long wow_read_ref = 1;
long long wow_write_hit = 0;
long long wow_write_ref = 0;

//4GB
//#define WOW_WS_SHORT_PERIOD (4*320000) 

//2GB
//#define WOW_WS_SHORT_PERIOD (512*1024) 
int Wow_WSS_Refresh_Period = 0;


/* global working set size */
unsigned char *ws_bitmap_global = NULL;
unsigned char *ws_bitmap_global_rd = NULL;
unsigned char *ws_bitmap_global_wr = NULL;
int ws_count_global = 0;
int ws_count_global_rd = 0;
int ws_count_global_wr = 0;

unsigned char *ws_bitmap_local = NULL;
unsigned char *ws_bitmap_local_rd = NULL;
unsigned char *ws_bitmap_local_wr = NULL;
int ws_count_local = 0;
int ws_count_local_rd = 0;
int ws_count_local_wr =0;


/*	global hit curve */
//int *ws_freq_curve_global;
//int *ws_freq_curve_global_rd;
//int *ws_freq_curve_global_wr;


/*	local hit curve */
//int *ws_freq_curve_local;
//int *ws_freq_curve_local_rd;
//int *ws_freq_curve_local_wr;

ssd_t *currssd;


FILE	*response_fp;
int response_count = 0;
char response_str[256];


void wow_freq_curve_init(int *p,int size){
	memset(p, 0x00, sizeof(int) * size);
}
void wow_freq_curve_alloc(int **p, int size){

	*p = (int *)malloc(sizeof(int) * size);
	if(*p == NULL){
		fprintf(stderr, "Error: malloc to ws hit curve\n");
		exit(1);
	}	
	

	wow_freq_curve_init(*p, size);
	printf(" Frequency Curve Alloc = %.f Kbytes\n", (double)sizeof(int)*size/1024);


}




void wow_ws_alloc(unsigned char **bitmap, int size){
	int bytes_to_alloc;

	bytes_to_alloc = size / (sizeof(unsigned char) * 8);
	if (!(*bitmap = (unsigned char *)malloc(bytes_to_alloc))) {
		fprintf(stderr, "Error: malloc to free_blocks in ssd_element_metadata_init failed\n");
		fprintf(stderr, "Allocation size = %d\n", bytes_to_alloc);
		exit(1);
	}
	bzero(*bitmap, bytes_to_alloc);

	printf(" Total WSS = %fGB\n", (double)size*4096/1024/1024/1024);
	printf(" WSS Alloc bitmap = %.f Kbytes\n", (double)bytes_to_alloc/1024);
}

void wow_ws_init(unsigned char *bitmap,int *count, int max){
	int bytes_to_alloc;

	bytes_to_alloc = max / (sizeof(unsigned char) * 8);
//	if (!(*bitmap = (unsigned char *)malloc(bytes_to_alloc))) {
//		fprintf(stderr, "Error: malloc to free_blocks in ssd_element_metadata_init failed\n");
//		fprintf(stderr, "Allocation size = %d\n", bytes_to_alloc);
//		exit(1);
//	}
	bzero(bitmap, bytes_to_alloc);

#if 0 
	int i;

	for(i = 0;i < max;i++){
		ssd_clear_bit(bitmap, i);
	}		
#endif 
	*count = 0;
}


group_t *wow_alloc_groupt(){
	group_t *g;
	int i;

	g = (group_t *)malloc(sizeof(group_t));
	if(g == NULL){
		fprintf(stderr, " malloc error \n");
		return NULL;
	}

	memset(g, 0x00, sizeof(group_t));

	return g;
}

//void wow_insert_group_to_freeq(group_t *g){
//	ll_insert_at_tail(g_FreeQueue, g);
//	ll_insert_at_tail(g_FreeHash[g->groupno%WOW_HASH_NUM], g);
//}





void wow_freq_curve_check(int *p, int blk){
	if(blk >= Wow_HDD_Max_Pages)
		blk = blk;
	p[blk%Wow_HDD_Max_Pages]++;
}




void wow_wss_check(unsigned char *bitmap,int *count, int blk){
	if(!ssd_bit_on(bitmap, blk)){
		ssd_set_bit(bitmap, blk);
		(*count)++;
	}
}

void wow_wss_free(unsigned char *bitmap){
	if(bitmap)
		free(bitmap);
}


void wow_set_watermark(int high,int low){
	
	if(high <= Wow_Max_High_Water){
		Wow_High_Water = high;
		Wow_Low_Water = low;
	}
	
	
		

}
void wow_wss_init(int max_size){
	wow_ws_alloc(&ws_bitmap_global, max_size);
	wow_ws_init(ws_bitmap_global, &ws_count_global, max_size);

	// global read
	wow_ws_alloc(&ws_bitmap_global_rd, max_size);
	wow_ws_init(ws_bitmap_global_rd, &ws_count_global_rd, max_size);

	// global write
	wow_ws_alloc(&ws_bitmap_global_wr, max_size);
	wow_ws_init(ws_bitmap_global_wr, &ws_count_global_wr, max_size);

	// local
	wow_ws_alloc(&ws_bitmap_local, max_size);
	wow_ws_init(ws_bitmap_local, &ws_count_local, max_size);

	// local read
	wow_ws_alloc(&ws_bitmap_local_rd, max_size);
	wow_ws_init(ws_bitmap_local_rd, &ws_count_local_rd, max_size);

	// local write
	wow_ws_alloc(&ws_bitmap_local_wr, max_size);
	wow_ws_init(ws_bitmap_local_wr, &ws_count_local_wr, max_size);
}

void wow_init(float initial_util){
	group_t *g;
	int i;
	int usable_pages = 0;
	currssd = getssd (SSD);
	
	//initialize group data
	//ll_create(&g_Queue);
	//ll_create(&g_FreeQueue);
	//for(i = 0;i < WOW_HASH_NUM;i++){
	//	ll_create(&g_Hash[i]);
	//	ll_create(&g_FreeHash[i]);
	//}

	//initialize page 
	//ll_create(&p_FreeQueue);


	//Wow_SSD_Max_Pages = ssd_get_number_of_blocks(0)/(BLOCK2SECTOR);
	//usable_pages = SSD_DATA_PAGES_PER_BLOCK(currssd) * currssd->params.blocks_per_element;
	if(initial_util == 0.0){
		Wow_SSD_Max_Pages =  ssd_elem_export_size2(currssd);
	}else{
		
		int max_pages =  ssd_elem_export_size2(currssd);
		printf(" Spec SSD size = %.3fMB\n", (double)max_pages*4/1024);
		Wow_SSD_Max_Pages = ssd_get_number_of_blocks(0)/(BLOCK2SECTOR);
		Wow_SSD_Max_Pages *= initial_util;

		if(max_pages < Wow_SSD_Max_Pages){
			Wow_SSD_Max_Pages = max_pages;
			printf(" SSD pages (%d) exceeds max pages (%d)\n",Wow_SSD_Max_Pages, max_pages);
			exit(0);
		}
	}
	Wow_Max_High_Water = Wow_SSD_Max_Pages;
	Wow_SSD_Max_Pages = ssd_get_number_of_blocks(0)/(BLOCK2SECTOR);;

	fprintf(stdout, " Max Cache SSD size = %.2fGB, %.2fMB\n", (double)Wow_Max_High_Water*4/1024/1024, (double)Wow_Max_High_Water*4/1024);
	fprintf(stdout, " Physical SSD size = %.2fGB, %.2fMB\n", (double)Wow_SSD_Max_Pages*4/1024/1024, (double)Wow_SSD_Max_Pages*4/1024);
	//fprintf(stdout, " Max Cache Size in SSD = %.2fGB\n", (double)ssd_elem_export_size2(currssd)*4/1024/1024);
	
	Wow_WSS_Refresh_Period = Wow_SSD_Max_Pages;

#ifdef DISK
	Wow_HDD_Max_Pages = device_get_number_of_blocks(DISK)/BLOCK2SECTOR;
#else
	Wow_HDD_Max_Pages = Wow_Num_Page;
#endif 
	
	Wow_HDD_Max_Pages -= (Wow_HDD_Max_Pages % Pages_Per_Group);
	fprintf(stdout," Total HDD Size = %.2fGB\n", (double)Wow_HDD_Max_Pages*4/1024/1024);


	//Wow_HDD_Num_Groups = Wow_HDD_Max_Pages/Pages_Per_Group;


	//Wow_Free_Flash_Count = Wow_SSD_Max_Pages-1;
	//Wow_Occupancy_flash_count = 0;

	//Wow_Max_High_Water = ssd_elem_export_size2(currssd);
	//wow_set_watermark(ssd_elem_export_size2(currssd), ssd_elem_export_size2(currssd) - Pages_Per_Group);

	//fprintf(stdout, " Max High Water = %.2fGB\n", (double)Wow_Max_High_Water*4/1024/1024);	

	//fill free group queue
	//for(i = 0;i < Wow_HDD_Num_Groups;i++){
	//	g = wow_alloc_groupt();
	//	g->groupno = i;
	//	wow_insert_group_to_freeq(g);
	//}

	//fill free page queue	
	//for(i = 1;i < Wow_SSD_Max_Pages;i++){
	//	ll_insert_at_tail(p_FreeQueue,(void *) i);
	//}
//
	g_DestagePtr = 0;
#if  0
	// global 
	
	

#if 0 
	/* global hit curve */
	wow_freq_curve_alloc(&ws_freq_curve_global, Wow_HDD_Max_Pages);
	wow_freq_curve_alloc(&ws_freq_curve_global_rd, Wow_HDD_Max_Pages);
	wow_freq_curve_alloc(&ws_freq_curve_global_wr, Wow_HDD_Max_Pages);

	/* local hit curve */
	wow_freq_curve_alloc(&ws_freq_curve_local, Wow_HDD_Max_Pages);
	wow_freq_curve_alloc(&ws_freq_curve_local_rd, Wow_HDD_Max_Pages);
	wow_freq_curve_alloc(&ws_freq_curve_local_wr, Wow_HDD_Max_Pages);
#endif 
#endif 
	fprintf(outputfile, "\n\n");

	fflush(stdout);
}


void wow_free(){
	int i;

	
	//ll_release(g_Queue);
	//ll_release(g_FreeQueue);
	//for(i = 0;i < WOW_HASH_NUM;i++){
	//	ll_release(g_Hash[i]);
	//	ll_release(g_FreeHash[i]);
	//}

	if(!ws_count_global)
		return;

#if 1 
	wow_wss_free(ws_bitmap_global);
	wow_wss_free(ws_bitmap_global_rd);
	wow_wss_free(ws_bitmap_global_wr);
	wow_wss_free(ws_bitmap_local);
	wow_wss_free(ws_bitmap_local_rd);
	wow_wss_free(ws_bitmap_local_wr);


	/*free(ws_freq_curve_global);
	free(ws_freq_curve_global_rd);
	free(ws_freq_curve_global_wr);
	
	free(ws_freq_curve_local);
	free(ws_freq_curve_local_rd);
	free(ws_freq_curve_local_wr);*/
#endif 

}


static int ll_compare_ssd(const void *a,const void *b){
	if(((page_t *)a)->flash_blkno > ((page_t *)b)->flash_blkno)
		return 1;
	else if(((page_t *)a)->flash_blkno < ((page_t *)b)->flash_blkno) 
		return -1;
	return 0;
}

static int ll_compare_hdd(const void *a,const void *b){
	if(((page_t *)a)->disk_blkno > ((page_t *)b)->disk_blkno)
		return 1;
	else if(((page_t *)a)->disk_blkno < ((page_t *)b)->disk_blkno) 
		return -1;
	return 0;
}


static int ll_compare_groupno(const void *a,const void *b){
	if((int)((group_t *)a)->groupno > (int)((group_t *)b)->groupno)
		return 1;
	else if((int)((group_t *)a)->groupno < (int)((group_t *)b)->groupno)
		return -1;
	return 0;
}


int ll_compare_group(const void *a,const void *b){
	if((int)((group_t *)a)->groupno == (int)b)
		return 1;	
	return 0;
}

int ll_compare_page(const void *a,const void *b){
	if((int)((page_t *)a)->disk_blkno == (int)b)
		return 1;	
	return 0;
}


page_t *wow_alloc_page(int disk, int flash){
	page_t *pg;
	pg = (page_t *)malloc(sizeof(page_t));

	memset(pg, 0x00, sizeof(page_t));
	pg->disk_blkno = disk;
	pg->flash_blkno = flash;
	
	return pg;
}




void wow_release_flashpage(int flash){
//	Wow_Free_Flash_Count++;
	Wow_Occupancy_flash_count--;

	//ll_insert_at_head(p_FreeQueue, (void *)flash);
	//if(flash == 18305){
	//	flash = flash;	
	//}
	ssd_trim_command(SSD, flash * BLOCK2SECTOR);
}



void wow_destage_all_pages(group_t *g){
	listnode *temp = g->page_list->prev;
	page_t *pg;
	int count = g->pagecount;
	listnode *Qlist;
	int i;

	wow_destage_count += count;

	ll_create(&req_HDD_WQlist);
	Qlist = req_HDD_WQlist;

	while(count){
		pg = temp->data;
		
		if(pg->dirty)
			wow_insert_page_to_reqlist(Qlist, pg, ll_compare_hdd);

		NODE_PREV(temp);
		count--;
	}
	if(g->pagecount){
		wow_flush_req_Qlist(SSD, Qlist, READ);
		wow_flush_req_Qlist(DISK, Qlist, WRITE);
	}
	
	temp = g->page_list->prev;
	while(g->pagecount){

		pg = temp->data;
		wow_release_flashpage(pg->flash_blkno);

		pg->deleted = 1;
		free(pg);		
		g->pagecount--;
		
		NODE_PREV(temp);
	}

	ll_release(g->page_list);

	for(i = 0;i < WOW_HASH_NUM;i++){
		ll_release(g->page_hash[i]);
	}
	ll_release(Qlist);
	

}
void wow_release_node_at_q(listnode *node, group_t *g){
	listnode *temp;
//	ll_release_node(g_Queue, node);
//	temp = ll_find_node(g_Hash[g->groupno%WOW_HASH_NUM], (void *) g->groupno, ll_compare_group);
//	ll_release_node(g_Hash[g->groupno%WOW_HASH_NUM], temp);

}



void wow_destage_alogorithm(){
	listnode *node;
	group_t *g;
	
	//while(Wow_Occupancy_flash_count >= Wow_Low_Water){
	//	
	//	node = g_Queue->prev;
	//	while(1){
	//		
	//		g = node->data;
	//		if(g->groupno > g_DestagePtr)
	//			break;
	//		
	//		NODE_PREV(node);
	//		if(node == g_Queue){				
	//			NODE_PREV(node);
	//			g = node->data;
	//			g_DestagePtr = 0;
	//			break;
	//		}
	//	}

	//	while(g->recency){
	//		g->recency = 0;			
	//		NODE_PREV(node);
	//		g = node->data;			
	//		if(node == g_Queue){				
	//			NODE_PREV(node);
	//			g = node->data;
	//		}
	//	}

	//	g = node->data;

	//	g_DestagePtr = g->groupno;
	//	//printf(" Destage group %d, count = %d\n", g->groupno, g->pagecount);
	//	wow_destage_all_pages(g);
	//	wow_release_node_at_q(node, g);		
	//	wow_insert_group_to_freeq(g);

	//}
}





int wow_get_extra_flashpage(int *flash){
	listnode *temp;
	group_t *g;	
	int total = 0;

	//if(Wow_Free_Flash_Count == 0)
	if(Wow_Occupancy_flash_count >= Wow_High_Water)
		return 0;

	//*flash = (int)ll_get_head(p_FreeQueue);
	//if(flash == 0){
	//	flash = 0;
	//}
	//ll_release_node(p_FreeQueue, p_FreeQueue->next);
	//Wow_Free_Flash_Count--;
	//Wow_Occupancy_flash_count++;
	return *flash;
}


int wow_alloc_flashpage(int *flash){
	int flag = 0;

	while(*flash == 0){
		*flash = wow_get_extra_flashpage(flash);
		if(*flash == 0){
			if(ll_get_size(req_SSD_RQlist) > 0)
				req_HDD_RQlist = req_HDD_RQlist;
			wow_stage_count += ll_get_size(req_HDD_RQlist);
			wow_flush_req_Qlist(DISK, req_HDD_RQlist, READ);
			wow_flush_req_Qlist(SSD, req_SSD_WQlist, WRITE);
			wow_flush_req_Qlist(SSD, req_SSD_RQlist, READ);
			
			ll_release(req_SSD_WQlist);			

			wow_destage_alogorithm();

			ll_create(&req_SSD_WQlist);
			ll_create(&req_HDD_RQlist);
			ll_create(&req_SSD_RQlist);
			flag = 1;
		}
	}

	if(*flash == 18305){
		flag = flag;
		//printf(" alloc 18305 \n");
	}
	return flag;
}


void wow_insert_group_to_q(group_t *g){
	//ll_insert_at_sort(g_Queue, g);	
//	ll_insert_at_sort(g_Queue, g, ll_compare_groupno);
//	ll_insert_at_tail(g_Hash[g->groupno%WOW_HASH_NUM], g);
}
//
//group_t *wow_get_extra_group(int gno){
//	listnode *hash;
//	listnode *node;
//	group_t *g;
//	int i;
////	hash = g_FreeHash[gno%WOW_HASH_NUM];
//	node = ll_find_node(hash, (void *)gno, ll_compare_group);
//	g = node->data;
//	ll_release_node(hash, node);
//
//	node = ll_find_node(g_FreeQueue, (void *)gno, ll_compare_group);
//	ll_release_node(g_FreeQueue, node);
//
//	ll_create(&g->page_list);
//	g->recency = 0;
//	g->resident = 1;
//
//	for(i = 0;i < WOW_HASH_NUM;i++){
//		ll_create(&g->page_hash[i]);
//	}
//	return g;
//}




page_t *wow_insert_page_to_group(group_t *g, int blk, int flash,int dirty){
	page_t *pg;
	pg = wow_alloc_page(blk, flash);
	pg->dirty = dirty;

	ll_insert_at_sort(g->page_list, pg, ll_compare_hdd);
	g->pagecount++;
	ll_insert_at_tail(g->page_hash[blk%WOW_HASH_NUM], pg);


	return pg;
}

void wow_insert_page_to_reqlist(listnode *req_list, page_t *pg, int (*comp_func) (const void*,const void*)){	
	ll_insert_at_sort(req_list, pg, comp_func);	
}

static int compare_freq(const void *a,const void *b){

	return *(int *)b-*(int *)a;
}
extern unsigned int rc_total, rc_read, rc_write;

#if 0
void wow_print_freq_table(FILE *fp){
	int i;
	int total = 0;
	int mega = 256;
	qsort(ws_freq_curve_global, Wow_HDD_Max_Pages, sizeof(int), compare_freq);
	qsort(ws_freq_curve_global_rd, Wow_HDD_Max_Pages, sizeof(int), compare_freq);
	qsort(ws_freq_curve_global_wr, Wow_HDD_Max_Pages, sizeof(int), compare_freq);

	fprintf(fp, " Syssim: Total Frequency Table \n");
	for(i = 0;i <= Wow_SSD_Max_Pages;i++){
		total += ws_freq_curve_global[i];

		if(i % (mega*1024/4) == 0){
			fprintf(fp, "	%d	%.3f\n", (i+1) * 100/ Wow_SSD_Max_Pages, (double)total*100/rc_total);
		}
	}

	total = 0;
	fprintf(fp, " Syssim: Read Frequency Table \n");
	for(i = 0;i <= Wow_SSD_Max_Pages;i++){
		total += ws_freq_curve_global_rd[i];

		if(i % (mega*1024/4) == 0){
			fprintf(fp, "	%d	%.3f\n", (i+1) * 100/ Wow_SSD_Max_Pages, (double)total*100/rc_read);
		}
	}

	total = 0;
	fprintf(fp, " Syssim: Write Frequency Table \n");
	for(i = 0;i <= Wow_SSD_Max_Pages;i++){
		total += ws_freq_curve_global_wr[i];

		if(i % (mega*1024/4) == 0){
			fprintf(fp, "	%d	%.3f\n", (i+1) * 100/ Wow_SSD_Max_Pages, (double)total*100/rc_write);
		}
	}
}
#endif 

void wow_print_wss(FILE *fp){	
	fprintf(fp, " Syssim: Global WSS All	=	%.3f	MB\n", (double)ws_count_global*4/1024);
	fprintf(fp, " Syssim: Global WSS Read	=	%.3f	MB\n", (double)ws_count_global_rd*4/1024);
	fprintf(fp, " Syssim: Global WSS Write	=	%.3f	MB\n", (double)ws_count_global_wr*4/1024);

	/*fprintf(fp, " Syssim: Global WSS All	=	%.3f	MB\n", (double)ws_count_global*512/1024/1024);
	fprintf(fp, " Syssim: Global WSS Read	=	%.3f	MB\n", (double)ws_count_global_rd*512/1024/1024);
	fprintf(fp, " Syssim: Global WSS Write	=	%.3f	MB\n", (double)ws_count_global_wr*512/1024/1024);*/

	fprintf(fp, "\n");

}


void wow_print_wss2(FILE *fp){	
	static int count = 0;

	if(cache_policy != CACHE_WORKLOAD_ANALYSIS)
		return;

	if(count ==0)
		fprintf(fp, "\n\n#No Global Global(R) Global(W) Local Local(R) Local(W)\n");

	//count++;
	fprintf(fp, "%d	%.2f	%.2f	%.2f	%.2f	%.2f	%.2f\n", 
		++count,
		(double)ws_count_global*4/1024/1024,
		(double)ws_count_global_rd*4/1024/1024,
		(double)ws_count_global_wr*4/1024/1024,
		(double)ws_count_local*4/1024/1024,
		(double)ws_count_local_rd*4/1024/1024,
		(double)ws_count_local_wr*4/1024/1024
		);
	//fprintf(fp, " Syssim: Global WSS All	=	%.3f	MB\n", (double)ws_count_global*4/1024);
	//fprintf(fp, " Syssim: Global WSS Read	=	%.3f	MB\n", (double)ws_count_global_rd*4/1024);
	//fprintf(fp, " Syssim: Global WSS Write	=	%.3f	MB\n", (double)ws_count_global_wr*4/1024);
	//fprintf(fp, "\n");

}


int wss_print_count = 0;
int last_hour = 0.0;


//
void wow_wss_refresh(int blk, int rw){
	int curr_hour = 0;
	wow_wss_check(ws_bitmap_global, &ws_count_global, blk);
	wow_wss_check(ws_bitmap_local, &ws_count_local, blk);
	
//	wow_freq_curve_check(ws_freq_curve_global, blk);
//	wow_freq_curve_check(ws_freq_curve_local, blk);

	if(rw == READ){
		wow_wss_check(ws_bitmap_global_rd, &ws_count_global_rd, blk);
		wow_wss_check(ws_bitmap_local_rd, &ws_count_local_rd, blk);	

//		wow_freq_curve_check(ws_freq_curve_local_rd, blk);
//		wow_freq_curve_check(ws_freq_curve_global_rd, blk);

	}else{
		wow_wss_check(ws_bitmap_global_wr, &ws_count_global_wr, blk);
		wow_wss_check(ws_bitmap_local_wr, &ws_count_local_wr, blk);

//		wow_freq_curve_check(ws_freq_curve_local_wr, blk);
//		wow_freq_curve_check(ws_freq_curve_global_wr, blk);
	}

#if 1 
	if(rc_total % (Wow_WSS_Refresh_Period/4) != 0 || rc_total == 0)
		return;
#else
	curr_hour = (int)((double)RealTime/(1000*60*60));
	if(curr_hour <= last_hour){
		return;
	}
#endif 
	last_hour = curr_hour;


	wow_print_wss(stdout);
	wow_print_wss2(outputfile);

	//wow_print_freq_table(stdout);

	if(Wow_Use_NARP){
		wow_set_watermark(ws_count_local, ws_count_local-Pages_Per_Group);
	}
	
	fflush(stdout);

	//if(rc_total % Wow_WSS_Refresh_Period != 0 || rc_total == 0)
	//	return;

	//if(rc_total % Wow_WSS_Refresh_Period == 0){
		wow_ws_init(ws_bitmap_local, &ws_count_local, Wow_HDD_Max_Pages);	
		wow_ws_init(ws_bitmap_local_rd, &ws_count_local_rd, Wow_HDD_Max_Pages);	
		wow_ws_init(ws_bitmap_local_wr, &ws_count_local_wr, Wow_HDD_Max_Pages);	
	//}
	

	//wow_freq_curve_init(ws_freq_curve_local,Wow_HDD_Max_Pages);
	//wow_freq_curve_init(ws_freq_curve_local_rd,Wow_HDD_Max_Pages);
	//wow_freq_curve_init(ws_freq_curve_local_wr,Wow_HDD_Max_Pages);
}



void wow_replacement_policy(int blk,int rw){
	listnode *node = NULL;
	listnode *hash;
	group_t *g;
	page_t *pg;
	int gno = blk/Pages_Per_Group;
	int flash = 0;

	wow_ref++;

	if(rw == READ)
		wow_read_ref++;
	else
		wow_write_ref++;

	wow_wss_refresh(blk, rw);	

Restart:;
	
//	hash = g_Hash[gno%WOW_HASH_NUM];
//	node = ll_find_node(hash,(void *)gno, ll_compare_group);
	if(node){ // group hit 
		g = node->data;
		
		
		
		node = ll_find_node(g->page_hash[blk%WOW_HASH_NUM], (void *)blk, ll_compare_page);		
		if(node){ // flash page hit in group

			//if(rw == READ){
			//	rw = rw;
			//}
			pg = node->data;
			g->recency = 1;
				
			if(rw == WRITE){
				pg->dirty = DIRTY;
				wow_insert_page_to_reqlist(req_SSD_WQlist, pg, ll_compare_ssd);
			}else{ // READ
				wow_insert_page_to_reqlist(req_SSD_RQlist, pg, ll_compare_ssd);
			}
			if(rw == READ)
				wow_read_hit++;
			else
				wow_write_hit++;

			wow_hit++;
			

		}else{ // flahs page miss in group
			if(rw == READ){
				rw = rw;
			}
			if(wow_alloc_flashpage(&flash)){
				goto Restart;	
			}

			if(rw == WRITE)
				pg = wow_insert_page_to_group(g, blk, flash, DIRTY);
			else
				pg = wow_insert_page_to_group(g, blk, flash, CLEAN);
			
			wow_insert_page_to_reqlist(req_SSD_WQlist, pg, ll_compare_ssd);
			if(rw == READ){
				wow_insert_page_to_reqlist(req_HDD_RQlist, pg, ll_compare_hdd);
				wow_insert_page_to_reqlist(req_SSD_RQlist, pg, ll_compare_ssd);
			}
		}

	}else{	// group miss	

		//g = wow_get_extra_group(gno);
		wow_insert_group_to_q(g);
		goto Restart;		
	}


	return;
}












void
panic(const char *s)
{
  perror(s);
  exit(1);
}


void
add_statistics(Stat *s, double x)
{
  s->n++;
  s->sum += x;
  s->sqr += x*x;
}


void
print_statistics(Stat *s, const char *title)
{
  double avg, std;

  avg = s->sum/s->n;
  std = sqrt((s->sqr - 2*avg*s->sum + s->n*avg*avg) / s->n);
  printf("%s: n=%d average=%f std. deviation=%f\n", title, s->n, avg, std);
}


/*
 * Schedule next callback at time t.
 * Note that there is only *one* outstanding callback at any given time.
 * The callback is for the earliest event.
 */
void
syssim_schedule_callback(disksim_interface_callback_t fn, 
			 SysTime t, 
			 void *ctx)
{
  next_event = t;
}


/*
 * de-scehdule a callback.
 */
void
syssim_deschedule_callback(double t, void *ctx)
{
  next_event = -1;
}


void
syssim_report_completion(SysTime t, struct disksim_request *r, void *ctx)
{
  completed = 1;
  now = t;
  add_statistics(&st, t - r->start);
}

ioreq_event * iotrace_ascii_get_ioreq_event (FILE *tracefile, ioreq_event *new)
{
	char line[201];

	if (fgets(line, 200, tracefile) == NULL) {
		addtoextraq((event *) new);
		return(NULL);
	}
	if (sscanf(line, "%lf %d %d %d %x\n", &new->time, &new->devno, &new->blkno, &new->bcount, &new->flags) != 5) {
		fprintf(stderr, "Wrong number of arguments for I/O trace event type\n");
		fprintf(stderr, "line: %s", line);
		//ddbg_assert(0);
	}
	if (new->flags & ASYNCHRONOUS) {
		new->flags |= (new->flags & READ) ? TIME_LIMITED : 0;
	} else if (new->flags & SYNCHRONOUS) {
		new->flags |= TIME_CRITICAL;
	}

	new->buf = 0;
	new->opid = 0;
	new->busno = 0;
	new->cause = 0;
	return(new);
}

int req_no = 0;
int req_window = 100000;
FILE *trace_fp;
char filename[100];

void wow_issue_request(struct disksim_request *r){
	int i;

	//return ;
#ifdef __linux__
	 //debug code
	//if(r->devno == DISK){
	//	now += 2.0;
	//	return;
	//}
	
	completed = 0;
	//disksim_interface_request_arrive(disksim_inf, now, r);
	disksim_interface_request_arrive(disksim_inf, now, r);
	//r->blkno += 100;
	//disksim_interface_request_arrive(disksim_inf, now, r);

	/* Process events until this I/O is completed */
	while(next_event >= 0) {
		now = next_event;
		next_event = -1;
		disksim_interface_internal_event(disksim_inf, now, 0);
	}

	if (!completed) {
		fprintf(stderr,
			"internal error. Last event not completed\n");
		exit(1);
	}
#else
	
	//if(r->blkno/8 == 3866)
	//		printf("dev = %d issue request  9630 ... %d\n",r->devno, r->flags);
	if(r->devno == SSD){
		
		

		completed = 0;
		//disksim_interface_request_arrive(disksim_inf, now, r);
		disksim_interface_request_arrive(disksim_inf, now, r);
		//r->blkno += 100;
		//disksim_interface_request_arrive(disksim_inf, now, r);

		/* Process events until this I/O is completed */
		while(next_event >= 0) {
			now = next_event;
			next_event = -1;
			disksim_interface_internal_event(disksim_inf, now, 0);
		}

		if (!completed) {
			fprintf(stderr,
				"internal error. Last event not completed\n");
			exit(1);
		}
	}else{
		now += 2.0;
	}
	
	

#endif 

#ifndef __linux__
	/*if(req_no % req_window == 0){
		
		if(req_no/req_window > 0)
			fclose(trace_fp);

		sprintf(filename, "miss_req_%d",req_no/req_window);
		trace_fp = fopen(filename,"w");
	}


	if(r->devno != SSD){
		if(r->flags == READ){
			for(i = 0;i < r->bytecount/BLOCK;i++){
				fprintf(trace_fp,"%d	%d\n", req_no++, r->blkno/BLOCK2SECTOR+i);
			}
		}else{
			
			for(i = 0;i < r->bytecount/BLOCK;i++){
				fprintf(trace_fp,"%d	-	%d\n", req_no++, r->blkno/BLOCK2SECTOR+i);
			}
		}
	}*/

#endif 
}

void wow_init_request(struct disksim_request *r){
	memset(r, 0x00, sizeof(struct disksim_request));

}
void wow_make_request(struct disksim_request *r,SysTime curtime, int dev, int read,unsigned long secno, int num){
	r->start = curtime;
	r->flags = read;
	r->devno = dev;
	r->blkno = secno;
	r->bytecount = num * SECTOR;
}



void wow_flush_req_Qlist(int devno, listnode *Qlist,int rw){
	int i;
	listnode *rq;
	struct disksim_request r;
	SysTime currTime = now;

	if(ll_get_size(Qlist) == 0)
		return;

	wow_init_request(&r);
	rq = Qlist->prev;
	for(i = 0;i < ll_get_size(Qlist);i++){
		unsigned long blk;
		page_t *pg = rq->data;		

		if(devno == SSD)
			blk = pg->flash_blkno;
		else 
			blk = pg->disk_blkno;

		if(!pg->dirty)
			pg = pg;
		if(pg->deleted)
			pg = pg;
		if(blk > Wow_HDD_Max_Pages)
			blk = blk;
		blk *= (unsigned long)BLOCK2SECTOR;

		
		if(!r.blkno){ //initialize
			wow_make_request(&r, currTime, devno, rw, blk, BLOCK2SECTOR);
		}else if(r.blkno + r.bytecount/SECTOR == blk){
			r.bytecount += BLOCK;
			if(r.bytecount >= MAX_REQ_BYTES){
				wow_issue_request(&r);	
				wow_init_request(&r);
			}
		}else if(r.blkno + r.bytecount/SECTOR!= blk){
			wow_issue_request(&r);			
			wow_make_request(&r, currTime, devno, rw, blk, BLOCK2SECTOR);
		}

		if(rw == WRITE)
			pg->dirty = CLEAN;


		NODE_PREV(rq);
	}
	if(r.bytecount)
		wow_issue_request(&r);

}




#define GIGA 4
#define REQ_SIZE (256*1024)
void gen_synth(){
	int i;
	int interation = (long long)GIGA * 1024 * 1024 * 1024/REQ_SIZE;
	//int interation = 1024;
	unsigned int blk;
	unsigned int devno = 0;
	unsigned int dev_max_pages;
	struct disksim_request r;
	SysTime start_time, end_time;
	
#if 1 
	devno = 0;
	dev_max_pages = Wow_HDD_Max_Pages;
#else
	devno = 1;
	dev_max_pages = Wow_SSD_Max_Pages * 90 / 100;

	//aging
	for(i = 0;i < dev_max_pages;i++){
		blk = i * BLOCK2SECTOR;
		blk %= (dev_max_pages-1);
		wow_make_request(&r, now, devno, WRITE, blk, BLOCK2SECTOR);
		wow_issue_request(&r);
	}
#endif 

	start_time = now;
	//seq read 
	for(i = 0;i < interation;i++){
		blk = i * (REQ_SIZE/BLOCK)  * BLOCK2SECTOR;
		blk %= dev_max_pages;
		wow_make_request(&r, now, devno, WRITE, blk, (REQ_SIZE/BLOCK) * BLOCK2SECTOR);
		wow_issue_request(&r);
	}
	end_time = now;	
	fprintf(stdout, "SW Throughput = %fMB\n", (double)GIGA *1024/((end_time-start_time)/1000));


	start_time = now;
	//rand read 
	for(i = 0;i < interation;i++){
		blk = RND(dev_max_pages/(REQ_SIZE/BLOCK));				
		blk *= (REQ_SIZE/BLOCK);
		blk %= dev_max_pages;
		blk *= BLOCK2SECTOR;
		wow_make_request(&r, now, devno, WRITE, blk, (REQ_SIZE/BLOCK) * BLOCK2SECTOR);
		wow_issue_request(&r);
	}
	end_time = now;	
	fprintf(stdout, "RW Throughput = %fMB\n", (double)GIGA *1024/((end_time-start_time)/1000));


	start_time = now;
	//seq read 
	for(i = 0;i < interation;i++){
		blk = i * (REQ_SIZE/BLOCK)  * BLOCK2SECTOR;
		blk %= dev_max_pages;
		wow_make_request(&r, now, devno, READ, blk, (REQ_SIZE/BLOCK) * BLOCK2SECTOR);
		wow_issue_request(&r);
	}
	end_time = now;	
	fprintf(stdout, "SR Throughput = %fMB\n", (double)GIGA *1024/((end_time-start_time)/1000));


	start_time = now;
	//rand read 
	for(i = 0;i < interation;i++){
		blk = RND(dev_max_pages/(REQ_SIZE/BLOCK));					
		blk *= (REQ_SIZE/BLOCK);
		blk %= dev_max_pages;
		blk *= BLOCK2SECTOR;		
		wow_make_request(&r, now, devno, READ, blk, (REQ_SIZE/BLOCK) * BLOCK2SECTOR);
		wow_issue_request(&r);
	}
	end_time = now;	
	fprintf(stdout, "RR Throughput = %fMB\n", (double)GIGA *1024/((end_time-start_time)/1000));

}
//int Wow_SSD_Max_Pages;

void wow_response_tracking(double curr_time,
						   double response_ms,
						   double stage,
						   double destage,
						   double ssd_read,
						   double ssd_write,
						   double gc_time, 
						   int s_total,
						   int s_suggested_read, 
						   int s_suggested_write,
						   int s_read, 
						   int s_write,
						   double read_hit,
						   double write_hit,
						   double avg_response){
	int mod = 512; // 512 for fast2012:
	int giga = Wow_SSD_Max_Pages / 256 / 1024;

	response_count++;
	//if(response_count % mod == 0 || response_ms >= 1000.0){
	if(response_count % mod == 0){
#if 0 
		fprintf(response_fp, "%f\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%f\t%f\t%.3f\n", 
			curr_time,
			response_count, 
			response_ms, 
			stage, 
			destage, 
			ssd_read,
			ssd_write,
			gc_time,
			(double)giga*((s_suggested_read+s_suggested_write)*100/s_total)/100, 
			(double)giga*((s_suggested_read)*100/s_total)/100,
			(double)giga*((s_read+s_write)*100/s_total)/100, 
			(double)giga*((s_read)*100/s_total)/100,
			(double)giga*((s_suggested_write)*100/s_total)/100, 
			(double)giga*((s_suggested_read)*100/s_total)/100,
			(double)giga*((s_write)*100/s_total)/100, 
			(double)giga*((s_read)*100/s_total)/100,
			read_hit,
			write_hit,
			avg_response
			);
#endif 

		//1
		fprintf(response_fp, "%d\t", response_count);
		//2
		fprintf(response_fp, "%f\t", curr_time);
		//3
		fprintf(response_fp, "%.3f\t", response_ms);
		//4
		fprintf(response_fp, "%.3f\t", stage);
		//5
		fprintf(response_fp, "%.3f\t", destage);
		//6
		fprintf(response_fp, "%.3f\t", ssd_read);
		//7
		fprintf(response_fp, "%.3f\t", ssd_write);
		//8
		fprintf(response_fp, "%.3f\t", gc_time);
		//9
		fprintf(response_fp, "%.3f\t", (double)giga*((s_suggested_read+s_suggested_write)*100/s_total)/100);
		//10
		fprintf(response_fp, "%.3f\t", (double)giga*((s_suggested_read)*100/s_total)/100);
		//11
		fprintf(response_fp, "%.3f\t", (double)giga*((s_read+s_write)*100/s_total)/100);
		//12
		fprintf(response_fp, "%.3f\t", (double)giga*((s_read)*100/s_total)/100);
		//13
		fprintf(response_fp, "%.3f\t", (double)giga*((s_suggested_write)*100/s_total)/100);
		//14
		fprintf(response_fp, "%.3f\t", (double)giga*((s_suggested_read)*100/s_total)/100);
		//15
		fprintf(response_fp, "%.3f\t", (double)giga*((s_write)*100/s_total)/100);
		//16
		fprintf(response_fp, "%.3f\t", (double)giga*((s_read)*100/s_total)/100);
		//17
		fprintf(response_fp, "%f\t", read_hit);
		//18
		fprintf(response_fp, "%f\t", write_hit);
		//19
		fprintf(response_fp, "%.3f\n", avg_response);


		fflush(response_fp);
		if(response_count % (mod * 100) == 0)
			fprintf(stdout, " Avg Response = %.3fms\n", avg_response);

		if(response_ms == 0.0)
			response_ms = response_ms;
	}
}

int cache_policy;
//extern int *atlas_array;
extern int RESIZE_PERIOD;
//extern int atlas_size;
//#define MAPPING_FILE "/root/atlas_mapping.dat"
//extern FILE *mapping_fp;
#if 0 
void extract_mapping(){

	int i;
	int mapping_temp;
	int percent;
	FILE *tracefile;
//	extern FILE *mapping_fp;

	printf(" Start testing... \n");

	//Wow_HDD_Max_Pages = 10000;

	mapping_temp = 5000;
	atlas_size = (Wow_HDD_Max_Pages + mapping_temp) * (BLOCK/SECTOR);

	if(atlas_array == NULL){
		int size = 0;

		size = atlas_size * sizeof(int); // 4 byte (integer)

		atlas_array = (int *)malloc(size);
		if(atlas_array == NULL){
			printf("malloc error \n");
			fflush(stdout);
			exit(0);
		}
		memset(atlas_array, 0x00, size);
	}



	printf(" Max Sectors = %d, Max Pages = %d\n", Wow_HDD_Max_Pages * (BLOCK/SECTOR), Wow_HDD_Max_Pages);
	printf(" file size = %d\n",  atlas_size * sizeof(int));

	fflush(stdout);

	percent = Wow_HDD_Max_Pages / 100;

	for(i = 0;i < Wow_HDD_Max_Pages;i++){
		//for(i = Wow_HDD_Max_Pages-1;i >=0 ;i--){

		struct disksim_request r;
		r.devno = 0;
		r.blkno = i * (BLOCK/SECTOR);
		r.bytecount = 4096;
		r.flags = WRITE;		
		wow_issue_request(&r);

		if(i % percent == 0 && i > 0){
			printf("caculating %d%% \n", (i/percent));
			fflush(stdout);
		}
	}



	mapping_fp = fopen(MAPPING_FILE, "w");
	if(mapping_fp == NULL){
		printf(" cannot open file %s\n", MAPPING_FILE);
		fflush(stdout);
		exit(0);
	}


	percent = atlas_size/100;

	for(i = 0;i < atlas_size;i++){		
		//for(i = Wow_HDD_Max_Pages*8-1;i >= 0;i--){		

		fwrite(&atlas_array[i], sizeof(int), 1, mapping_fp);

		if(i % percent == 0 && i > 0){
			printf("write file %d%% \n", (i/percent));
			fflush(stdout);
		}
	}


	fclose(mapping_fp);


	//validation
#if 1 
	mapping_fp = fopen(MAPPING_FILE, "r");
	if(mapping_fp == NULL){
		printf(" cannot open file %s\n", MAPPING_FILE);
		fflush(stdout);
		exit(0);
	}


	percent = atlas_size/100;

	for(i = 0;i < atlas_size;i++){		
		int test_val = 0;


		//fwrite(&atlas_array[i], 4, 1, mapping_fp);
		fread(&test_val, sizeof(int), 1, mapping_fp);

		if(test_val != atlas_array[i]){
			printf(" Verification error : test = %d, atlas_array =  %d\n", test_val, atlas_array[i]);
			fflush(stdout);
		}

		if(i % percent == 0 && i > 0){
			printf("verification %d%% \n", (i/percent));
			fflush(stdout);
		}
	}
	fclose(mapping_fp);

#endif // the end of verification

	free(atlas_array);

	exit(0);

}

void use_atlas_mapping(){

	int i;
	int mapping_temp;
	int percent;
	FILE *tracefile;


	
	
	mapping_fp = fopen(MAPPING_FILE, "r");
	if(mapping_fp == NULL){
		printf(" cannot open file %s\n", MAPPING_FILE);
		fflush(stdout);
		exit(0);
	}

	
	
	
	mapping_temp = 5000;
	atlas_size = (Wow_HDD_Max_Pages + mapping_temp) * (BLOCK/SECTOR);
	printf(" Max Sectors = %d, Max Pages = %d\n", Wow_HDD_Max_Pages * (BLOCK/SECTOR), Wow_HDD_Max_Pages);
	printf(" file size = %d\n",  atlas_size * sizeof(int));
	printf(" Using Mapping File \n");
	fflush(stdout);


	if(atlas_array == NULL){
		int size = 0;

		size = atlas_size * sizeof(int); // 4 byte (integer)

		atlas_array = (int *)malloc(size);
		if(atlas_array == NULL){
			printf("malloc error \n");
			fflush(stdout);
			exit(0);
		}
		memset(atlas_array, 0x00, size);
	}
	
	
	printf(" Read Mapping File \n");
	fflush(stdout);
	percent = atlas_size/100;
	for(i = 0;i < atlas_size;i++){		
		int test_val = 0;

		//fwrite(&atlas_array[i], 4, 1, mapping_fp);
		fread(&test_val, sizeof(int), 1, mapping_fp);

		//if(test_val != atlas_array[i]){
		//	printf(" Verification error : test = %d, atlas_array =  %d\n", test_val, atlas_array[i]);
		//	fflush(stdout);
		//}
		atlas_array[i] = test_val;

		if(i % percent == 0 && i > 0){
			printf("verification %d%% \n", (i/percent));
			fflush(stdout);
		}
	}


#if 0 
	printf(" Write Test \n");
	fflush(stdout);
	
	percent = Wow_HDD_Max_Pages / 100;
	for(i = 0;i < Wow_HDD_Max_Pages;i++){
		//for(i = Wow_HDD_Max_Pages-1;i >=0 ;i--){

		struct disksim_request r;
		r.devno = 0;
		r.blkno = i * (BLOCK/SECTOR);
		r.bytecount = 4096;
		r.flags = WRITE;		
		wow_issue_request(&r);

		if(i % percent == 0 && i > 0){
			printf("caculating %d%% \n", (i/percent));
			fflush(stdout);
		}
	}
#endif 



	//exit(0);

}
#endif 

//#define USE_ATLAS_MAPPING 
//extern in ssdlib_ssd_max_pages;


/*
																	 [resize period][seqsize][N windowsize]
wow slc.parv costmoel.outv 100  ../../../experiments/websearch.txt 1 16 32 16
*/


int
main(int argc, char *argv[])
{
	int i = 0;
	int j = 0;
	int nsectors;
	int count;
	int total_erases = 0;
	int total_blocks = 0;
	int track_num = 16;
	double erase_avg = 0.0;
	//double erase_std = 0.0;
	double run_squares = 0.0;
	long long total_erase = 0;
	struct stat buf;
	struct disksim_request r;
	listnode *rq;
	int percent = 0;
	float initial_util = 0.1;
	extern int SEQ_COUNT;
	ioreq_event req;

	int mapping_temp;
//#define USE_ERASE_FP 
#ifdef USE_ERASE_FP
	FILE *erase_fp;
#endif
	char erase_str[256];
	unsigned long long debug_count = 0;
	statgen	system_response;
	
	
	//genzipf();
//	seq_detect_main();

	track_num = 16;

	if (argc < 6 || argc > 9) {
		fprintf(stderr, "usage: %s <param file> <output file> <#sectors>\n",
			argv[0]);
		exit(1);
	}

	disksim_inf = disksim_interface_initialize(argv[1], 
		argv[2],
		syssim_report_completion,
		syssim_schedule_callback,
		syssim_deschedule_callback,
		0,
		0,
		0);
	//disksim = disksim_inf->disksim;

	/* NOTE: it is bad to use this internal disksim call from external... */
	DISKSIM_srand48(1);

	cache_policy = atoi(argv[5]);
	if(cache_policy >= CACHE_POLICY_NUM){
		fprintf(stdout, " Invalid Argument ... \n");
		return 0;
	}
	if (stat(argv[1], &buf) < 0)
		panic(argv[1]);

	//if(argc == 7){
	//	initial_util = atof(argv[6]);
	//}else{
		initial_util = 0.0;
	//}

	if(argc >= 8 && atoi(argv[7]) > 0){
		SEQ_COUNT = atoi(argv[7]);
		fprintf(outputfile, " Seq Detection Size = %dKB\n", SEQ_COUNT * 4);
		fprintf(stdout, " Seq Detection Size = %dKB\n", SEQ_COUNT * 4);
	}

	if(argc >= 9 && atoi(argv[8]) > 0){
		track_num = atoi(argv[8]);
	}
	
	
	wow_init(initial_util);
	rw_set_params(currssd);

	printf(" hdd cost %dus\n", HDD_COST());

	sprintf(response_str,"%s.response", argv[2]);	
	response_count = 0;

	response_fp = fopen(response_str, "w");
	if(response_fp == NULL){
		fprintf(stdout, "cannot open file %s\n", response_str);
		exit(0);
	}

	sprintf(erase_str, "%s.erase", argv[2]);
#ifdef USE_ERASE_FP
	erase_fp = fopen(erase_str, "w");
	if(erase_fp == NULL){
		fprintf(stdout, "cannot open file %s\n", erase_str);
		exit(0);
	}
#endif 
	fprintf(stdout, " Open trace file = %s\n", argv[4]);

	
	
	//RESIZE_PERIOD = 65536;

// extract mapping
#if 0 
	//extract_mapping();
#else
	//use_atlas_mapping();
#endif 

	if(argc >= 8 && atoi(argv[6]) > 0){
		double scale = 0.0;
		int period = atoi(argv[6]);
		//track_num = atoi(argv[6]);
		//if(track_num < 8)
		//	track_num = 8;

		//scale = atof(argv[7]);
		if(scale <= 0.0)
			scale = 1.0;
		
		RESIZE_PERIOD = Wow_SSD_Max_Pages / period;
		//RESIZE_PERIOD = (double)RESIZE_PERIOD * scale;
	}
	
	
	if(cache_policy == CACHE_LRU_RW){
//#ifndef __linux__
//		Wow_HDD_Max_Pages = 268435456/8;
//#endif 
		fprintf(stdout, " FCM ... \n");
		fprintf(outputfile, " FCM ... \n");
		fflush(stdout);
		//wow_wss_init(Wow_HDD_Max_Pages);
		rw_simulation(argv[4], Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages, 0);
	}else if(cache_policy == CACHE_LRU_RWO_ADAPTIVE){
		fprintf(stdout, " AFCM ... \n");
		fprintf(outputfile, " AFCM ... \n");
		fflush(stdout);
#ifndef __linux__
		Wow_HDD_Max_Pages = 268435456/8;
#endif 

		//wow_wss_init(Wow_HDD_Max_Pages);
		
		


		fprintf(stdout , " Syssim: Resize Period = %d\n", RESIZE_PERIOD);
		fprintf(outputfile, " Syssim: Resize Period = %d\n", RESIZE_PERIOD);
		fprintf(stdout, " Syssim: Hit tracking precision = %d\n", track_num);
		fprintf(outputfile, " Syssim: Hit tracking precision = %d\n", track_num);
		fflush(stdout);

		adaptive_simulation(argv[4], track_num, Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages, 0, 1, 0);


	}else if(cache_policy == CACHE_LRU_SELECTIVE){
		
		fprintf(stdout, " Selective AFCM ... \n");
		fprintf(outputfile, " Selective AFCM ... \n");

#ifndef __linux__
		Wow_HDD_Max_Pages = 268435456/8;
#endif 

		//wow_wss_init(Wow_HDD_Max_Pages);


	

		fprintf(stdout , " Syssim: Resize Period = %d\n", RESIZE_PERIOD);
		fprintf(outputfile, " Syssim: Resize Period = %d\n", RESIZE_PERIOD);
		fprintf(stdout, " Syssim: Hit tracking precision = %d\n", track_num);
		fprintf(outputfile, " Syssim: Hit tracking precision = %d\n", track_num);

		adaptive_simulation(argv[4], track_num, Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages, 0, 1, 1);


	}else if(cache_policy == CACHE_BGCLOCK_RW){
		fprintf(stdout, " Block Group Clock Replacement ... \n");
		fprintf(outputfile, " Block Group Clock Replacement ... \n");
		//wow_simulation(argv[4], 16, Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages);
		rw_simulation(argv[4], Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages, 1);
	}else if(cache_policy == CACHE_BGCLOCK_RWO_ADAPTIVE){
		fprintf(stdout, " Block Group Write Clock and LRU Read Replacement ... \n");
		fprintf(outputfile, " Block Group Write Clock and LRU Read Replacement ... \n");
		adaptive_simulation(argv[4], track_num, Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages, 1, 1, 0);
	}else if(cache_policy == CACHE_WORKLOAD_ANALYSIS){		
		fprintf(stdout, " Syssim: Hit tracking precision = %d\n", track_num);
		fprintf(outputfile, " Syssim: Hit tracking precision = %d\n", track_num);

		fprintf(stdout, " I/O directly to the disk for workload analysis  ... \n");
		fprintf(outputfile, " I/O directly to the disk for workload analysis  ... \n");		
		workload_simulation(argv[4], Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages, 0, track_num);
	}else if(cache_policy == CACHE_DISK_ONLY){		
		fprintf(stdout, " DISK Only Simulation ... \n");
		fprintf(outputfile, " DISK Only Simulation ... \n");
		//wow_wss_init(Wow_HDD_Max_Pages);
		disk_simulation(argv[4], Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages, 0);
	}else if(cache_policy == CACHE_SSD_ONLY){		
		fprintf(stdout, " SSD Only Simulation ... \n");
		fprintf(outputfile, " SSD Only Simulation ... \n");
//		wow_wss_init(Wow_HDD_Max_Pages);
		ssd_simulation(argv[4], Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages, currssd);
	}else if(cache_policy == CACHE_LRU_RW_ADAPTIVE){		

#ifndef __linux__
		Wow_HDD_Max_Pages = 35913728;
#endif 
		fprintf(stdout, " FCM(RW) ... \n");
		fprintf(outputfile, " FCM(RW) ... \n");
		fflush(stdout);
		//wow_wss_init(Wow_HDD_Max_Pages);
		adaptive_simulation(argv[4], track_num, Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages, 0, 0, 0);
	}else if(cache_policy == CACHE_ZIPF){
		fprintf(stdout, " Zipf Simulation   ... \n");
		fprintf(outputfile, " Zipf Simulation   ... \n");
		zipf_simulation(argv[4], Wow_Max_High_Water, Wow_HDD_Max_Pages, Wow_SSD_Max_Pages, 0);
	}


	stat_print(&currssd->stat.responsestats, "NANDCACHE: ");

#ifdef USE_ERASE_FP
	fprintf(erase_fp," #Print Erase Count \n");
	fprintf(erase_fp," #logicalBlock EraseCount CummCount \n");
#endif 
	for (i = 0; i < currssd->params.nelements; i ++) {
		ssd_element_stat *stat = &(currssd->elements[i].stat);
		ssd_element_metadata *meta = &(currssd->elements[i].metadata);
		block_metadata *block = meta->block_usage;
		
		total_erase += stat->num_clean;

		for(j = 0;j < currssd->params.blocks_per_element;j++){
			int n = SSD_MAX_ERASURES - block[j].rem_lifetime;
			total_erases += (n);
#ifdef USE_ERASE_FP
			fprintf(erase_fp, "%d	%d	%d\n",i*currssd->params.blocks_per_element+j, 
			n, total_erases);
#endif
			run_squares += (double)(n * n);
		}

	}
	
	total_blocks = currssd->params.nelements * currssd->params.blocks_per_element;
	erase_avg = (double)total_erase/(double)total_blocks;
	run_squares = run_squares / (double) total_blocks - (erase_avg*erase_avg);
	run_squares = (run_squares > 0.0) ? sqrt(run_squares) : 0.0;

	fprintf(outputfile, "Syssim Total Erases Count	=	%lld\n",total_erase);
	fprintf(outputfile, "Syssim Total Erases Avg. 	=	%f\n",erase_avg);
	fprintf(outputfile, "Syssim Total Erases Std. 	=	%f\n",run_squares);
#ifdef USE_ERASE_FP	
	fprintf(erase_fp, "Syssim Total Erases Count	=	%lld\n",total_erase);
	fprintf(erase_fp, "Syssim Total Erases Avg. 	=	%f\n",erase_avg);
	fprintf(erase_fp, "Syssim Total Erases Std. 	=	%f\n",run_squares);
	fclose(erase_fp);
#endif 

	wow_print_wss(outputfile);
//	wow_print_freq_table(outputfile);
	
	
	
	fclose(response_fp);
	
	disksim_interface_shutdown(disksim_inf, now);

	printf(" Simulation was successfully done\n"); 
	
	//wow_free();	
	
	
	//if(mapping_fp){
	//	fclose(mapping_fp);
	//	free(atlas_array);
	//}
	

	exit(0);
}






#if 1 

int
wowmain(int argc, char *argv[])
{
  int i = 0;
  int nsectors;
  int count;
  long long total_erase = 0;
  struct stat buf;
  struct disksim_request r;
  listnode *rq;
  
  ioreq_event req;
  FILE	*tracefile;
  unsigned long long debug_count = 0;

  if (argc != 6) {
    fprintf(stderr, "usage: %s <param file> <output file> <#sectors>\n",
	    argv[0]);
    exit(1);
  }

  disksim_inf = disksim_interface_initialize(argv[1], 
	  argv[2],
	  syssim_report_completion,
	  syssim_schedule_callback,
	  syssim_deschedule_callback,
	  0,
	  0,
	  0);

  /* NOTE: it is bad to use this internal disksim call from external... */
  DISKSIM_srand48(1);

  
#ifdef __linux__
  tracefile = fopen64(argv[4], "r");
#else
  tracefile = fopen(argv[4], "r");
#endif
  
  Wow_Use_NARP = atoi(argv[5]);
  if(Wow_Use_NARP != 0 && Wow_Use_NARP != 1){
	  fprintf(stdout, " Invalid Argument ... \n");
	  return 0;
  }

  if(Wow_Use_NARP){
	  fprintf(stdout, " NARP On !! \n");
  }else{
	  fprintf(stdout, " NARP Off !! \n");
  }

  if(tracefile == NULL){
	  fprintf(stdout," cannot open trace file %s\n", argv[4]);
	  return 0;
  }
  if (stat(argv[1], &buf) < 0)
    panic(argv[1]);




//  wow_init();

#if 0 
  gen_synth();
  return 0;
#endif 



	//exit(1);

  for (;;) {
	int flush = 0;
	int rw = 0;

	ll_create(&req_SSD_WQlist);  
	ll_create(&req_HDD_RQlist);
	ll_create(&req_SSD_RQlist);

	if(iotrace_ascii_get_ioreq_event(tracefile, &req) == NULL){
		break;
	}
	if(req.flags & READ){
		//fprintf(stdout," Read Req : blkno %d, size = %d\n", req.blkno, req.bcount);
	}else{
		//fprintf(stdout," Write Req : blkno %d, size = %d\n", req.blkno, req.bcount);
	}

	rw = req.flags;

	count = req.bcount / BLOCK2SECTOR;
	if(req.bcount % BLOCK2SECTOR){
		count++;
	}
		
	//for(i = 0;i < count;i++){	
		//wow_replacement_policy((req.blkno/BLOCK2SECTOR+i)%Wow_HDD_Max_Pages, rw);
	//}

	wow_stage_count += ll_get_size(req_HDD_RQlist);
	wow_flush_req_Qlist(DISK, req_HDD_RQlist, READ);	
	wow_flush_req_Qlist(SSD, req_SSD_WQlist, WRITE);
	wow_flush_req_Qlist(SSD, req_SSD_RQlist, READ);
	ll_release(req_SSD_WQlist);
	ll_release(req_SSD_RQlist);
	ll_release(req_HDD_RQlist);

	debug_count++;
	if(debug_count == 125)
		debug_count = debug_count;
  }

  for (i = 0; i < currssd->params.nelements; i ++) {
	  ssd_element_stat *stat = &(currssd->elements[i].stat);
	  total_erase += stat->num_clean;
  }
  
  fprintf(outputfile, "\n############### Syssim Result ################\n");
  fprintf(outputfile, "Syssim Execution Time	=	%f\n", now);
  fprintf(outputfile, "Syssim Total Hit Ratio	=	%f	(%lld/%lld)\n",(double)wow_hit/(double)wow_ref, wow_hit, wow_ref);
  fprintf(outputfile, "Syssim Read Hit Ratio	=	%f	(%lld/%lld)\n",(double)wow_read_hit/(double)wow_read_ref,wow_read_hit,wow_read_ref);
  fprintf(outputfile, "Syssim Write Hit Ratio	=	%f	(%lld/%lld)\n",(double)wow_write_hit/(double)wow_write_ref,wow_write_hit,wow_write_ref);
  fprintf(outputfile, "Syssim Total Erases	=	%lld\n",total_erase);
  fprintf(outputfile, "Syssim Destage Count	=	%lld\n", wow_destage_count);
  fprintf(outputfile, "Syssim Stage Count	=	%lld\n", wow_stage_count);
//  fprintf(outputfile, "Syssim Free Flash Pages	=	%d\n",Wow_Free_Flash_Count);
  fprintf(outputfile, "Syssim Working Set Size = %.2fGB\n", (double)ws_count_global*4/1024/1024);
  fprintf(outputfile, "Syssim Ref/WSS = %.3f\n", (double)wow_ref/ws_count_global);
  fprintf(outputfile, "\n");




  disksim_interface_shutdown(disksim_inf, now);

  print_statistics(&st, " response time");
  printf(" Execution time = %f\n", now);
  printf(" Simulation was successfully done\n"); 

	
  wow_free();

  ll_release(req_SSD_WQlist);

  fclose(tracefile);

  exit(0);
}


#endif 
