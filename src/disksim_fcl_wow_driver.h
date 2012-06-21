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

#ifndef _WOW_DRIVER_H
#define _WOW_DRIVER_H
#include "disksim_interface.h"

/*
 * Definitions for simple system simulator that uses DiskSim as a slave.
 *
 * Contributed by Eran Gabber of Lucent Technologies - Bell Laboratories
 *
 */

typedef	double SysTime;		/* system time in seconds.usec */


/* routines for translating between the system-level simulation's simulated */
/* time format (whatever it is) and disksim's simulated time format (a      */
/* double, representing the number of milliseconds from the simulation's    */
/* initialization).                                                         */

/* In this example, system time is in seconds since initialization */
// #define SYSSIMTIME_TO_MS(syssimtime)    (syssimtime*1e3)
// #define MS_TO_SYSSIMTIME(curtime)       (curtime/1e3)


#define	BLOCK	4096
#define	SECTOR	512
#define	BLOCK2SECTOR	(BLOCK/SECTOR)

#ifdef __linux__
#define MAX_REQ_BYTES (256*1024)
#else
#define MAX_REQ_BYTES (128*1024)
#endif 

#define MAX_STAGE_COUNT 1024
#define MAX_DESTAGE_COUNT MAX_STAGE_COUNT

//#define SEQ_COUNT (256) // submit version
//#define SEQ_COUNT (64) // submit version


#define CLEAN 0
#define DIRTY 1

#define SSD 1
#define DISK 0


#define NODE_NEXT(n) {n = n->next;}
#define NODE_PREV(n) {n = n->prev;}

#define WOW_HASH_NUM 1024



typedef struct{
	int disk_blkno;
	int flash_blkno;
	int dirty;
	int deleted;
}page_t;

typedef struct{
	int resident;
	int recency;
	int groupno;	
	int pagecount;
	listnode *page_list;
	listnode *page_hash[WOW_HASH_NUM];
}group_t;


typedef	struct	{
	int n;
	double sum;
	double sqr;
} Stat;

extern ssd_t *currssd;

extern int cache_policy;
extern SysTime RealTime;

//void wow_response_tracking(double response_ms);
//void wow_response_tracking(double response_ms, double stage, double destage, double ssd_read,double ssd_write);
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
						   double avg_response);

/* exported by syssim_driver.c */
void syssim_schedule_callback(disksim_interface_callback_t, SysTime t, void *);
void syssim_report_completion(SysTime t, struct disksim_request *r, void *);
void syssim_deschedule_callback(double, void *);





void wow_flush_req_Qlist(int devno, listnode *Qlist, int rw);
void wow_insert_page_to_reqlist(listnode *req_list, page_t *pg, int (*comp_func)(const void *a,const void *b));


#endif 