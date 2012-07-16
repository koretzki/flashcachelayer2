
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: The header file 
* File Name		: disksim_fcl_seq_detect.h
*/


struct sequential_detector { 
	int sd_startblk;
	int sd_length;
	int sd_seq_size;
	int	sd_enable;
};


void sd_init( );
void sd_exit( );
int  sd_seq_detection ( int blkno, int length );
int sd_is_seq_io ( int blkno );
