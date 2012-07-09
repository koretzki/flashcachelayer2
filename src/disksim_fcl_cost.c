
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: 
*
*/

#include "disksim_fcl.h"
#include "disksim_fcl_cost.h"


float u_table[1000];

int SSD_GC(float u){
	return ((float)u*SSD_NP*(SSD_PROG+SSD_READ) + (float)SSD_ERASE);
}

int SSD_PW(float u){
	//return ((float)SSD_GC(u)/((1-u)*SSD_NP) + (float)SSD_PROG + SSD_BUS);
	return ((float)SSD_GC(u)/(((float)1-u)*SSD_NP) + (float)SSD_PROG + SSD_BUS);
}


int HDD_COST(int rw){
	int cost;
	
	if(rw == READ){		
		cost = (int)((float)HDD_CRPOS + (float)FCL_PAGE_SIZE_BYTE/(1024*1024)/HDD_BANDWIDTH*1000000);
		
	}else{	
		cost = (int)((float)HDD_CWPOS + (float)FCL_PAGE_SIZE_BYTE/(1024*1024)/HDD_BANDWIDTH*1000000);		

	}

	return cost;
}



static double ssd_predict_util2(double diskUtlz){
	return u_table[(int)(diskUtlz*1000)];
}



static double ssd_predict_util(double diskUtlz){
	double threshold = 1e-9;
	int i, max;

	double u1, u2, u;
	double disku1, disku2, disku;

	
	i = 0;
	max = 10000;
	disku1 = u1 = 0;
	disku2 = u2 = 1;
	while (u2 - u1 > threshold) {
		u = (u1+u2)/2;

		disku = (u - 1.0) / log(u);
		if (diskUtlz > disku) {
			u1 = u;
			disku1 = disku;
		} else {
			u2 = u;
			disku2 = disku;
		}

		if (i++ > max) {
			printf("Cannot find solution: %lf", diskUtlz);
			exit(1);
		}
	}

	return (double)(u1+u2)/(double)2;
}



void make_utable(){
	int i;

	for(i = 0;i < 1000;i++){
		u_table[i] = ssd_predict_util((float)i/1000);
	}

}


static int calc_rw_partition_size(int cache_size, double r_p_rate, int *r_sz,int *w_sz){
	int remain = 0;

	*r_sz = r_p_rate * cache_size ;	
	*w_sz = (1-r_p_rate) * cache_size ;

	remain = cache_size - (*r_sz + *w_sz);
	if(remain)
		*r_sz += remain;

	if( cache_size < *r_sz + *w_sz ){
		fprintf(stdout, " invalid data size = %d \n", *r_sz + *w_sz );
		fflush(stdout);
		return -1;
	}

	if(r_p_rate > 1.0 || r_p_rate < 0.0){
		fprintf(stdout, " invalid read rate = %f \n", r_p_rate);
		fflush(stdout);
		return -1;
	}

	return 0;	
}
void print_test_cost () {
	double u = 0.9;

	printf ( " HDD Read Cost = %.1f us \n", (double)HDD_COST ( READ ) );
	printf ( " HDD Write Cost = %.1f us  \n", (double)HDD_COST ( WRITE ) );

	printf ( " SSD Read Cost = %.1f us \n", (double)SSD_READ );
	printf ( " SSD Write Cost (u = %.2f)  = %.1f us \n", u, (double)SSD_PW ( ssd_predict_util ( u ) ) );

//	for ( u = 0.1 ; u < 0.9; u+= 0.1 ) {
//		printf ( " SSD Write Cost (u = %.2f)  = %.1f us \n", u, (double)SSD_PW ( ssd_predict_util ( u ) ) );
//	}

}
static float calc_write_cost(int total, int cache_size, float hit_ratio, float rhit_ratio_in_write){
	float cost;
	float u;
	float pw;
	float Cwh;

#if 1 
	u = ssd_predict_util((double)cache_size/total);
#else
	u = ssd_predict_util2((double)cache_size/total);
#endif 

	pw = SSD_PW(u);


	Cwh = (1-rhit_ratio_in_write) * (pw) + rhit_ratio_in_write * (SSD_READ + SSD_BUS);	
	cost = hit_ratio * Cwh + (float)(1-hit_ratio) * (HDD_COST(WRITE) + (SSD_READ + SSD_BUS) + pw);

	return cost;
}


static float calc_read_cost(int total, int cache_size, float hit_ratio){
	float cost;
	float u;
	float pw;
#if 1 
	u = ssd_predict_util((double)cache_size/total);
#else
	u = ssd_predict_util2((double)cache_size/total);
#endif 
	pw = SSD_PW(u);
	
	cost = hit_ratio * (SSD_READ + SSD_BUS) + (1-hit_ratio) * (HDD_COST(READ) + pw);

	return cost;
}

static float calc_total_cost(	struct cache_manager **W_HIT_TRACKER,
								struct cache_manager **R_HIT_TRACKER,
								int tracker_num,
								int total_pages, 
								double u,
								double r )
{
	float w_hit;
	float rhit_ratio_in_write;
	float r_hit;
	float w_cost;
	float r_cost;
	float c_cost;
	float r_rate;
	int w_size; 
	int r_size;
	int w_count = 0, r_count = 0;
	int cache_pages;


	cache_pages = total_pages * u;

	if(calc_rw_partition_size( cache_pages, r, &r_size, &w_size) < 0)
		return 1000000.0;

	//printf (" total page = %d, write = %d, read = %d \n", cache_pages, r_size, w_size );
	//t_size = w_size + r_size;

	w_hit = (double)fcl_predict_hit_ratio(W_HIT_TRACKER, tracker_num, w_size, total_pages, FCL_WRITE);
	rhit_ratio_in_write = (double)fcl_predict_hit_ratio(W_HIT_TRACKER, tracker_num, w_size, total_pages, FCL_READWRITE);
	r_hit = (double)fcl_predict_hit_ratio(R_HIT_TRACKER, tracker_num, r_size, total_pages, FCL_READ);

	//printf (" w_size = %d, w_hit %f, r_hit = %f, rhit in write = %f  \n", w_size, w_hit, r_hit, rhit_ratio_in_write );
	w_cost = calc_write_cost(total_pages, cache_pages, w_hit, rhit_ratio_in_write);
	r_cost = calc_read_cost(total_pages, cache_pages, r_hit);
	
	//r_rate = 0.5;
	r_rate = (double ) fcl_io_read_pages / fcl_io_total_pages;

	c_cost = r_cost * r_rate + w_cost * (1 - r_rate);

	return c_cost;
}

void fcl_find_optimal_size ( struct cache_manager **write_hit_tracker, 
						 struct cache_manager **read_hit_tracker,
						 int tracker_num,
						 int total_pages,
						 int *read_optimal_pages,
						 int *write_optimal_pages)
{
	double u = 0, r = 0;
	double cur_cost = 0.0;

	double min_cost = 0.0;
	double min_u, min_r;

	double step = (double)1/16;

	double u_start, u_end;

	int optimal_usable_pages = 0;

	if ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_RW ) {
		u_start = (double)flash_usable_pages/flash_total_pages;
		u_end = u_start +0.00001;
	} else if ( fcl_params->fpa_partitioning_scheme == FCL_CACHE_OPTIMAL ) {
		u_start = step;
		u_end = 0.999;
	}

	for ( u = u_start; u < (double) u_end ; u+= step ) {
		for ( r = 0; r < (double) 0.999; r+= step ) {
			cur_cost = calc_total_cost ( write_hit_tracker, read_hit_tracker, tracker_num, total_pages, u, r );
			
			if ( u ==  u_start && r == 0 ) {
				min_cost = cur_cost;
				min_u = u;
				min_r = r;
			}

			if ( cur_cost < min_cost ) {
				min_cost = cur_cost;
				min_u = u;
				min_r = r;
				///printf (" u =%f, r = %f \n", u, r );
			}
//			printf (" [%.2f,%.2f] cost = %f \n", u, r, cur_cost );
		}
	}

	fprintf ( stdout,  " -> Found Optimal u = %f, r = %f , min_cost = %f \n", min_u, min_r, min_cost );

	optimal_usable_pages = total_pages * min_u;
	if ( optimal_usable_pages > flash_usable_pages ) 
		optimal_usable_pages = flash_usable_pages;

	*read_optimal_pages = optimal_usable_pages * min_r;
	*write_optimal_pages = optimal_usable_pages - *read_optimal_pages;

	//ASSERT ( *read_optimal_pages + *write_optimal_pages == flash_usable_pages );
	//*write_optimal_pages = total_pages * min_u * ((double)1-min_r);

}

