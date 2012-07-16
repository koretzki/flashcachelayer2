
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: The sequential I/O detector program
* File Name		: disksim_fcl_seq_detect.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "disksim_fcl_seq_detect.h"
#include "disksim_global.h"

struct sequential_detector *seq_detector;


int sd_is_seq_io ( int blkno ) {
	int startblk = seq_detector->sd_startblk;
	int lastblk = startblk + seq_detector->sd_length;
	int seq = 0;

	if ( !seq_detector->sd_enable ) 
		return 0;

	if ( seq_detector->sd_length >= seq_detector->sd_seq_size &&
		 startblk <= blkno && lastblk >= blkno )
	{
		//printf (" Sequential I/O = %d \n", blkno );
		seq = 1;
	} else {

		//printf (" Non-Sequential I/O = %d \n", blkno );
	}		

	return seq;

}
int sd_seq_detection ( int blkno, int length ) {
	int seq = 0;

	if ( !seq_detector->sd_enable ) 
		return 0;

	if ( seq_detector->sd_startblk + seq_detector->sd_length != blkno ) {

		seq_detector->sd_startblk = blkno;
		seq_detector->sd_length = 0;

	}

	seq_detector->sd_length += length;

	if ( seq_detector->sd_length >= seq_detector->sd_seq_size ) {
		seq = 1;
		/* 
		   printf ( " %f Seq detection: blkno = %d, length = %d \n", 
				   					simtime,
									seq_detector->sd_startblk,
									seq_detector->sd_length );
		//*/
	}

	return seq;

}

void sd_init ( int enable, int seq_unit_sectors ){
	seq_detector = (struct sequential_detector *) malloc ( sizeof ( struct sequential_detector) );
	memset ( seq_detector, 0x00, sizeof ( struct sequential_detector ) );

	seq_detector->sd_seq_size = seq_unit_sectors;
	seq_detector->sd_enable = enable;
}

void sd_exit () { 
	free ( seq_detector );
}
