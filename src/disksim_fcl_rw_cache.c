#include <stdio.h>
#include "cache.h"
#include "ssd_utils.h"
#include "disksim_interface.h"
#include "disksim_hptrace.h"
#include "wow_driver.h"

#define PAGE 4096


#define READ_REQ_RATE ((float)rc_read/(float)rc_total)
#define WRITE_REQ_RATE ((float)rc_write/(float)rc_total)

#define RS_SERVICE_RATE ((float)nr_serviced_rs/(float)rc_total)
#define WS_SERVICE_RATE ((float)nr_serviced_ws/(float)rc_total)

#define UP 1
#define DOWN 0

// 
#define MAX_DIRECTION 8
#define R1U0 0 /* r+, u */
#define R1U1 1 /* r+, u+ */
#define R0U1 2 /* r , u+ */
#define R_U1 3 /* r-, u+ */
#define R_U0 4 /* r-, u */
#define R_U_ 5 /* r-, u- */
#define R0U_ 6 /* r , u- */
#define R1U_ 7 /* r+, u- */

#define DIR0 R1U0
#define DIR1 R1U1
#define DIR2 R0U1
#define DIR3 R_U1
#define DIR4 R_U0
#define DIR5 R_U_
#define DIR6 R0U_
#define DIR7 R1U_


static struct cache_manager **W_HIT_TRACKER, **R_HIT_TRACKER;
static struct cache_manager **HIT_TRACKER;
static struct cache_manager *R_CLOCK, *W_CLOCK;

static int g_GHOST_HIT = 0;
static int HIT_TRACKER_NUM;

//#define R_P_INC 5 
static int R_P_INC = 256;
static int U_INC  = 256;
int RESIZE_PERIOD = 0;
static double anti_total_cost = 0.0;
//static int USE_ADAPTIVE = 0;

//static int range = (24*1024);
static int SSD_MAX_PAGES = 512*1024; // 2GB
static int HDD_MAX_PAGES = 3*1024*1024; // 12GB
int SSD_USABLE_PAGES = 1024 * 1024;

static int DRAM_MAX_PAGES = 8*1024; //32MB

unsigned int rc_read = 0; /* */
unsigned int rc_write_in_read = 0;
unsigned int rc_write = 0;
unsigned int rc_read_in_write = 0;
unsigned int rc_total = 0;

unsigned int nr_serviced_rs = 0;
unsigned int nr_serviced_ws = 0;

static int S_READ = 0;  /* Read Cache Size */
static int S_WRITE = 0; /* Write Cache Size */
static int S_TOTAL = 0; /* Total Cache Size */
static double R_VALUE = 50; /* Read Partition Ratio */

static int *reverse_map;
static int reverse_free = 0;
static int reverse_used = 0;
static int reverse_alloc = 0;
static int reverse_max_pages = 0;
listnode *reverse_freeq;

extern double now;


//int HDD_AVGCOST = 0;
int HDD_CRPOS = 4400; //us
int HDD_CWPOS = 4900; //us
int HDD_BANDWIDTH = 72; //mb/s

int SSD_PROG = 200;
int SSD_ERASE = 1500;
int SSD_READ = 25;
int SSD_NP  = 64;
int SSD_BUS = 1000;


int HDD_COUNT_W = 0;
int HDD_COUNT_R = 0;
double HDD_TOTALCOST_W = 0.0;
double HDD_TOTALCOST_R = 0.0;

int SSD_COUNT_W = 0;
int SSD_COUNT_R = 0;
double SSD_TOTALCOST_W = 0.0;
double SSD_TOTALCOST_R = 0.0;

int SSD_COUNT_PREDICT_R = 0;
int SSD_COUNT_PREDICT_W = 0;
double SSD_TOTALCOST_PREDICT_W = 0.0;
double SSD_TOTALCOST_PREDICT_R = 0.0;

double DESTAGE_TIME = 0.0;
double DESTAGE_SSD_READ = 0.0;
double DESTAGE_HDD_WRITE = 0.0;

double STAGE_TIME = 0.0;
double STAGE_SSD_WRITE = 0.0;
double STAGE_HDD_READ = 0.0;

double SSD_READ_TIME = 0.0;
double SSD_WRITE_TIME = 0.0;
//double SSD_GC_TIME = 0.0;

extern int FCM_REQ_FLAG;

struct cache_manager *dram_cache;
struct cache_manager *seq_detector;


int SEQ_COUNT = 32;

void stat_update (statgen *statptr, double value);
float u_table[1000];


static float calc_total_cost(	struct cache_manager **W_HIT_TRACKER,
struct cache_manager **R_HIT_TRACKER,
	int tracker_num,
	int cache_size,
	double r_p_rate,
	int direction,
	int u_scale,
	int r_scale,
	int use_direction
	);
void lru_flush_reqlist(struct cache_manager *c, int devno, int rw, listnode *Qlist,int trim);


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
		cost = (int)((float)HDD_CRPOS + (float)PAGE/(1024*1024)/HDD_BANDWIDTH*1000000);
		
	}else{	
		cost = (int)((float)HDD_CWPOS + (float)PAGE/(1024*1024)/HDD_BANDWIDTH*1000000);		

	}

	return cost;
}

void rw_set_params(ssd_t *curssd){
	SSD_PROG = curssd->params.page_write_latency*1000;
	SSD_READ = curssd->params.page_read_latency*1000;
	SSD_ERASE = curssd->params.block_erase_latency*1000;
	SSD_NP = curssd->params.pages_per_block;
	SSD_BUS = 8 * (SSD_BYTES_PER_SECTOR) * curssd->params.chip_xfer_latency * 1000;
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

	//printf("%lf\n", (double)(u1+u2)/(double)2);
	return (double)(u1+u2)/(double)2;
}



void make_utable(){
	int i;

	for(i = 0;i < 1000;i++){
		u_table[i] = ssd_predict_util((float)i/1000);
	}

}


static float calc_read_cost(int total, int cache_size, float hit_ratio){
	float cost;
	float u;
	float pw;
#if 0 
	u = ssd_predict_util((double)cache_size/total);
#else
	u = ssd_predict_util2((double)cache_size/total);
#endif 
	pw = SSD_PW(u);
	
	cost = hit_ratio * (SSD_READ + SSD_BUS) + (1-hit_ratio) * (HDD_COST(READ) + pw);

	return cost;
}


static float calc_read_gc_cost(int total, int cache_size, float hit_ratio){
	float cost;
	float u;
	float pw;

	u = ssd_predict_util2((double)cache_size/total);

	pw = SSD_PW(u);
	
	cost = (1-hit_ratio)*(pw);

	return cost;
}




static float calc_write_gc_cost(int total, int cache_size, float hit_ratio, float rhit_ratio_in_write){
	float cost;
	float u;
	float pw;
	float Cwh;

#if 0 
	u = ssd_predict_util((double)cache_size/total);
#else
	u = ssd_predict_util2((double)cache_size/total);
#endif 

	pw = SSD_PW(u);

	Cwh = (1-rhit_ratio_in_write) * (pw);	
	cost = hit_ratio * Cwh + (float)(1-hit_ratio) * (pw);

	return cost;
}



static float calc_write_cost(int total, int cache_size, float hit_ratio, float rhit_ratio_in_write){
	float cost;
	float u;
	float pw;
	float Cwh;

#if 0 
	u = ssd_predict_util((double)cache_size/total);
#else
	u = ssd_predict_util2((double)cache_size/total);
#endif 

	pw = SSD_PW(u);


	Cwh = (1-rhit_ratio_in_write) * (pw) + rhit_ratio_in_write * (SSD_READ + SSD_BUS);	
	cost = hit_ratio * Cwh + (float)(1-hit_ratio) * (HDD_COST(WRITE) + (SSD_READ + SSD_BUS) + pw);

	return cost;
}

static  int sort_function(const void *a,const void *b){
	if(*(int *)a < *(int *)b)
		return 1;
	else if(*(int *)a == *(int *)b)
		return 0;
	else
		return -1;
}



void move_to_direction(int *u, int *r, int direction, int u_scale, int r_scale){
#if 0 
	if(direction == R1U0){
		(*r) += (R_P_INC * r_scale);
	}else if(direction == R1U1){
		(*r) += (R_P_INC * r_scale);			
		(*u) += (U_INC * u_scale);
	}else if(direction == R0U1){
		(*u) += (U_INC * u_scale);
	}else if(direction == R_U1){
		(*r) -= (R_P_INC * r_scale);
		(*u) += (U_INC * u_scale);
	}else if(direction == R_U0){
		(*r) -= (R_P_INC * r_scale);			
	}else if(direction == R_U_){
		(*r) -= (R_P_INC * r_scale);
		(*u) -= (U_INC * u_scale);
	}else if(direction == R0U_){
		(*u) -= (U_INC * u_scale);
	}else if(direction == R1U_){
		(*r) += (R_P_INC * r_scale);
		(*u) -= (U_INC * u_scale);
	}
#endif 
}

extern int Wow_Max_High_Water;

static int calc_rw_partition_size(int cache_size, double r_p_rate, int *r_sz,int *w_sz,int u_inc, int r_inc){
	int u_sz;	
	int remain = 0;
	u_sz = cache_size + u_inc;
	
	//if(u_sz > Wow_Max_High_Water)
	//	u_sz = Wow_Max_High_Water -1;

	r_p_rate += r_inc;
	*r_sz = r_p_rate * u_sz / 100;	
	*w_sz = (100-r_p_rate) * u_sz / 100;


	remain = u_sz - (*r_sz + *w_sz);
	if(remain)
		*r_sz += remain;

	
	//if(u_sz >= SSD_MAX_PAGES || u_sz <=0)
	//if(u_sz > Wow_Max_High_Water || u_sz <=0){

	if(u_sz > SSD_MAX_PAGES || u_sz <=0){
		fprintf(stdout, " invalid data size = %d \n", u_sz);
		fflush(stdout);
		return -1;
	}

	if(r_p_rate >= 100 || r_p_rate <= 0){
		fprintf(stdout, " invalid read rate = %d \n", r_p_rate);
		fflush(stdout);
		return -1;
	}

	return 0;	
}

void print_hit_ratio(struct cache_manager **lru_manager,int lru_num,int is_read){
	int j;
	int total1, total2 = 0;
	int window;
	int index;
	double cost; 

	window = SSD_MAX_PAGES/lru_num;
	if(is_read){
		/*index = R_CLOCK->cm_size/window;
		if(R_CLOCK->cm_size%window)
			index++;*/
		
		index = S_TOTAL/window;
		if(S_TOTAL%window)
			index++;

	}else{
		//index = W_CLOCK->cm_size/window;
		//if(W_CLOCK->cm_size%window)
		//	index++;
		index = S_TOTAL/window;
		if(S_TOTAL%window)
			index++;
	}
	
	for(j = 0;j < lru_num;j++){
		total1 = total2;
		total2 += lru_manager[j]->cm_hit; 
		
		
		cost  = (double)calc_total_cost(W_HIT_TRACKER, R_HIT_TRACKER, HIT_TRACKER_NUM, 
			SSD_MAX_PAGES/HIT_TRACKER_NUM*(j+1), 
			(double)R_VALUE, 0, 0, 0,0);

		if(is_read)
			if(j == index){
				printf("R %d = %.5f, MG = %.5f C = %.3f [*]\n", j+1, (double)total2/rc_read, (double)total2/rc_read - (double)total1/rc_read, 
					cost);
			}else{
				printf("R %d = %.5f, MG = %.5f C = %.3f \n", j+1, (double)total2/rc_read, (double)total2/rc_read - (double)total1/rc_read, 
					cost);
			}
		else{
			if(j == index){
				printf("W %d = %.5f, MG = %.5f C = %.3f [*]\n", j+1, (double)total2/rc_write, (double)total2/rc_write - (double)total1/rc_write, 
					cost);
			}else{
				printf("W %d = %.5f, MG = %.5f, C = %.3f\n", j+1, (double)total2/rc_write, (double)total2/rc_write - (double)total1/rc_write, 
					cost);
			}
		}
	}

	//fflush(stdout);
}


double predict_hit_ratio(struct cache_manager **lru_manager,int lru_num, int size,int max_pages,int is_read){
	int j;
	double total1 = 0, total2 = 0;
	double rhit1 = 0, rhit2 = 0;
	int index =0;
	int window;
	double h_window;
	double r_window;
	double n;
	double hit;

	window = max_pages/lru_num;
	index = size/window;
	if(size%window)
		index++;
		
	for(j = 0;j < index;j++){
		total1 = total2;
		rhit1 = rhit2;

		if(is_read == READ)
			total2 += lru_manager[j]->cm_hit;
		else if(is_read == WRITE)
			total2 += (lru_manager[j]->cm_hit);
		else if(is_read == READWRITE){
			total2 += (double)lru_manager[j]->cm_hit;
			rhit2 += (double)lru_manager[j]->cm_read_hit;
			if(lru_manager[j]->cm_hit < lru_manager[j]->cm_read_hit)
				j = j;
		}
	}
		
	
	n = (double)(size%window)/((double)window/10);
	h_window =(double)(total2 - total1)/10;
	//if(h_window == 0.0 && is_read == READWRITE)
	//	h_window = h_window;

	//if(h_window >= 1.0 && is_read == READWRITE)
	//	h_window = h_window;

	if(is_read == READWRITE){
		
		if(total1 != 0.0)
			rhit1/=total1;
		else
			rhit1 = 0.0;


		if(total2 != 0.0)
			rhit2/=total2;
		else
			rhit2 = 0.0;

		h_window = (double)(rhit2-rhit1)/10;
		total1 = rhit1;

	}

	if(is_read == READ)
		hit = (double)(total1 + h_window * n)/lru_manager[0]->cm_ref;
	else if(is_read == WRITE)
		hit = (double)(total1 + h_window * n)/(lru_manager[0]->cm_ref);
	else if(is_read == READWRITE){
		hit =  (double)(total1 + h_window * n);

		if(hit < 0.0 || hit > 1.0){
			hit = hit;
		}
	}


	return hit;
}

double predict_hit_count(struct cache_manager **lru_manager,int lru_num, int size,int max_pages,int is_read){
	int j;
	double total1 = 0, total2 = 0;
	double rhit1 = 0, rhit2 = 0;
	int index =0;
	int window;
	double h_window;
	double r_window;
	double n;
	double hit;

	window = max_pages/lru_num;
	index = size/window;
	if(size%window)
		index++;
		
	for(j = 0;j < index;j++){
		total1 = total2;
		rhit1 = rhit2;

		if(is_read == READ)
			total2 += lru_manager[j]->cm_hit;
		else if(is_read == WRITE)
			total2 += (lru_manager[j]->cm_hit);
		else if(is_read == READWRITE){
			total2 += (double)lru_manager[j]->cm_hit;
			rhit2 += (double)lru_manager[j]->cm_read_hit;
			if(lru_manager[j]->cm_hit < lru_manager[j]->cm_read_hit)
				j = j;
		}
	}
		
	
	n = (double)(size%window)/((double)window/10);
	h_window =(double)(total2 - total1)/10;
	//if(h_window == 0.0 && is_read == READWRITE)
	//	h_window = h_window;

	//if(h_window >= 1.0 && is_read == READWRITE)
	//	h_window = h_window;

	if(is_read == READWRITE){
		
		if(total1 != 0.0)
			rhit1/=total1;
		else
			rhit1 = 0.0;


		if(total2 != 0.0)
			rhit2/=total2;
		else
			rhit2 = 0.0;

		h_window = (double)(rhit2-rhit1)/10;
		total1 = rhit1;

	}

	if(is_read == READ)
		hit = (double)(total1 + h_window * n);
	else if(is_read == WRITE)
		hit = (double)(total1 + h_window * n);
	else if(is_read == READWRITE){
		hit =  (double)(total1 + h_window * n);

		if(hit < 0.0 || hit > 1.0){
			hit = hit;
		}
	}


	return hit;
}

static float calc_writeonly_cost(struct cache_manager **HIT_TRACKER,
	int tracker_num,
	int cache_size	
	){
		float w_hit;
		//float r_hit;
		float w_cost;
		//float r_cost;
		//float c_cost;
		int t_size;
		int w_size; 
		int r_size;
		int u_inc = 0, r_inc = 0;

		//if(use_direction)
		//	move_to_direction(&u_inc, &r_inc, direction, u_scale, r_scale);

		calc_rw_partition_size(cache_size, 0, &r_size, &w_size, u_inc, r_inc);


		t_size = w_size + r_size;

		w_hit = (double)predict_hit_ratio(W_HIT_TRACKER, tracker_num, w_size, SSD_MAX_PAGES, WRITE);
		//r_hit = (float)predict_hit_ratio(R_HIT_TRACKER, tracker_num, r_size, SSD_MAX_PAGES, READ);
		w_cost = calc_write_cost(SSD_MAX_PAGES, t_size, w_hit, 0.0);
		//r_cost = calc_read_cost(SSD_MAX_PAGES, t_size, r_hit);
		//c_cost = r_cost * READ_REQ_RATE + w_cost * (1 - READ_REQ_RATE);

		//printf(" u %f, whit %f\n", (double)w_size/SSD_MAX_PAGES, w_hit);

		return w_cost;
}


static float calc_readonly_cost(struct cache_manager **HIT_TRACKER,
	int tracker_num,
	int cache_size	
	){
		float w_hit;
		float r_hit;
		float w_cost;
		float r_cost;
		float c_cost;
		int t_size;
		int w_size; 
		int r_size;
		int u_inc = 0, r_inc = 0;

		//if(use_direction)
		//	move_to_direction(&u_inc, &r_inc, direction, u_scale, r_scale);

		calc_rw_partition_size(cache_size, 100, &r_size, &w_size, u_inc, r_inc);
		

		t_size = w_size + r_size;

		//w_hit = (float)predict_hit_ratio(W_HIT_TRACKER, tracker_num, w_size, SSD_MAX_PAGES, WRITE);
		r_hit = (double)predict_hit_ratio(R_HIT_TRACKER, tracker_num, r_size, SSD_MAX_PAGES, READ);
		//w_cost = calc_write_cost(SSD_MAX_PAGES, t_size, w_hit);
		r_cost = calc_read_cost(SSD_MAX_PAGES, t_size, r_hit);
		//c_cost = r_cost * READ_REQ_RATE + w_cost * (1 - READ_REQ_RATE);

		//printf(" u %f, whit %f\n", (double)w_size/SSD_MAX_PAGES, w_hit);

		return r_cost;
}



static float calc_total_cost(	struct cache_manager **W_HIT_TRACKER,
								struct cache_manager **R_HIT_TRACKER,
								int tracker_num,
								int cache_size,
								double r_p_rate,
								int direction,
								int u_scale,
								int r_scale,
								int use_direction
	){
	float w_hit;
	float rhit_ratio_in_write;
	float r_hit;
	float w_cost;
	float r_cost;
	float c_cost;
	float r_rate;
	int t_size;
	int w_size; 
	int r_size;
	int w_count = 0, r_count = 0;
	int u_inc = 0, r_inc = 0;

	if(use_direction)
		move_to_direction(&u_inc, &r_inc, direction, u_scale, r_scale);

	if(calc_rw_partition_size(cache_size, r_p_rate, &r_size, &w_size, u_inc, r_inc) < 0)
		return 10000.0;

	t_size = w_size + r_size;

	//w_hit = (double)predict_hit_ratio(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_USABLE_PAGES, WRITE);
	//rhit_ratio_in_write = (double)predict_hit_ratio(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_USABLE_PAGES, READWRITE);	
	//r_hit = (double)predict_hit_ratio(R_HIT_TRACKER, HIT_TRACKER_NUM, r_size, SSD_USABLE_PAGES, READ);
	if(W_CLOCK->cm_policy == CACHE_LRU_RWO_ADAPTIVE){
		w_hit = (double)predict_hit_ratio(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_MAX_PAGES, WRITE);
		rhit_ratio_in_write = (double)predict_hit_ratio(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_MAX_PAGES, READWRITE);	
		r_hit = (double)predict_hit_ratio(R_HIT_TRACKER, HIT_TRACKER_NUM, r_size, SSD_MAX_PAGES, READ);
	}else{
		w_hit = (double)predict_hit_ratio(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_USABLE_PAGES, WRITE);
		rhit_ratio_in_write = (double)predict_hit_ratio(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_USABLE_PAGES, READWRITE);	
		r_hit = (double)predict_hit_ratio(R_HIT_TRACKER, HIT_TRACKER_NUM, r_size, SSD_USABLE_PAGES, READ);
	}
	


	w_cost = calc_write_cost(SSD_MAX_PAGES, t_size, w_hit, rhit_ratio_in_write);
	r_cost = calc_read_cost(SSD_MAX_PAGES, t_size, r_hit);
	

	//r_count = (double)predict_hit_count(R_HIT_TRACKER, HIT_TRACKER_NUM, r_size, SSD_MAX_PAGES, READ);
	//w_count = (double)predict_hit_count(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_MAX_PAGES, WRITE);

	
	//READ_REQ_RATE는 read only req만 이다... 
	r_rate = READ_REQ_RATE;
	//r_rate = (double)(r_count)/(r_count+w_count);
	//if(r_rate == 0.0){
	//	r_rate = READ_REQ_RATE;
	//}

	c_cost = r_cost * r_rate + w_cost * (1 - r_rate);

	//if(r_count)
	//	printf(" R rate = %f, R serviced = %f\n", r_rate, (double)r_count/(r_count+w_count));
	fflush(stdout);



	return c_cost;
}



static float calc_gc_total_cost(	struct cache_manager **W_HIT_TRACKER,
								struct cache_manager **R_HIT_TRACKER,
								int tracker_num,
								int cache_size,
								double r_p_rate,
								int direction,
								int u_scale,
								int r_scale,
								int use_direction
	){
	float w_hit;
	float rhit_ratio_in_write;
	float r_hit;
	float w_cost;
	float r_cost;
	float c_cost;
	float r_rate;
	int t_size;
	int w_size; 
	int r_size;
	int w_count = 0, r_count = 0;
	int u_inc = 0, r_inc = 0;

	if(use_direction)
		move_to_direction(&u_inc, &r_inc, direction, u_scale, r_scale);

	if(calc_rw_partition_size(cache_size, r_p_rate, &r_size, &w_size, u_inc, r_inc) < 0)
		return 10000.0;

	t_size = w_size + r_size;

	
	if(W_CLOCK->cm_policy == CACHE_LRU_RWO_ADAPTIVE){
		w_hit = (double)predict_hit_ratio(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_MAX_PAGES, WRITE);
		rhit_ratio_in_write = (double)predict_hit_ratio(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_MAX_PAGES, READWRITE);	
		r_hit = (double)predict_hit_ratio(R_HIT_TRACKER, HIT_TRACKER_NUM, r_size, SSD_MAX_PAGES, READ);
	}else{
		w_hit = (double)predict_hit_ratio(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_USABLE_PAGES, WRITE);
		rhit_ratio_in_write = (double)predict_hit_ratio(W_HIT_TRACKER, HIT_TRACKER_NUM, w_size, SSD_USABLE_PAGES, READWRITE);	
		r_hit = (double)predict_hit_ratio(R_HIT_TRACKER, HIT_TRACKER_NUM, r_size, SSD_USABLE_PAGES, READ);
	}
	


	w_cost = calc_write_gc_cost(SSD_MAX_PAGES, t_size, w_hit, rhit_ratio_in_write);
	r_cost = calc_read_gc_cost(SSD_MAX_PAGES, t_size, r_hit);
	
	
	//READ_REQ_RATE는 read only req만 이다... 
	r_rate = READ_REQ_RATE;
	//r_rate = (double)(r_count)/(r_count+w_count);
	//if(r_rate == 0.0){
	//	r_rate = READ_REQ_RATE;
	//}

	c_cost = r_cost * r_rate + w_cost * (1 - r_rate);

	//if(r_count)
	//	printf(" R rate = %f, R serviced = %f\n", r_rate, (double)r_count/(r_count+w_count));
	fflush(stdout);



	return c_cost;
}




static resize_count = 0;

static int adjust_afcm_cache_size(struct cache_manager **w_tracker,
								struct cache_manager **r_tracker, 
								int tracker_num,
								int u_scale,
								int r_scale)
{
	float cost[MAX_DIRECTION];
	float gain[MAX_DIRECTION];
	float cur_cost;	
	float max_gain;
	float *global_cost;
	float min_cost;
	int min_i,min_j;
	int min_direction = 0;	
	int i, j;
	int r_step;
	int step;
	int target_total;
	int max_step = SSD_MAX_PAGES/HIT_TRACKER_NUM;
	int new_s_write = 0, new_s_read = 0;
#if 0 
	/* current */		
	cur_cost = calc_total_cost(w_tracker, r_tracker, tracker_num, S_TOTAL, R_VALUE, MAX_DIRECTION, u_scale, r_scale, 1);
	for(i = 0;i < MAX_DIRECTION;i++){
		cost[i] = calc_total_cost(w_tracker, r_tracker, tracker_num,S_TOTAL, R_VALUE, i, u_scale, r_scale, 1);
		gain[i] = cur_cost - cost[i];
	}

	min_direction = -1;
	max_gain = 0.0;
	if(gain[0] > 0){
		max_gain = gain[0];
		min_direction = 0;
	}

	for(i = 1;i< MAX_DIRECTION;i++){
		if(max_gain < gain[i]){
			max_gain = gain[i];
			min_direction = i;
		}
	}


	if(min_direction >= 0){
		move_to_direction(&S_TOTAL, &R_VALUE, min_direction, u_scale, r_scale);
		calc_rw_partition_size(S_TOTAL, R_VALUE, &S_READ, &S_WRITE, 0, 0);
		//printf(" min direction = %d, max_gain = %.2f\n", min_direction, max_gain);		
	}
#endif 


#if 1 
//	r_step = 100/R_P_INC;
	r_step = HIT_TRACKER_NUM;
	//r_step *= 8;
	r_step *= 2;
	//if(100 % R_P_INC)
	//	r_step++;

	global_cost = (float *)malloc(sizeof(float) * tracker_num * r_step);
	memset(global_cost, 0x00, sizeof(float) * tracker_num * r_step);

	
	for(i = 1;i < HIT_TRACKER_NUM; i++){
		for(j = 1;j < r_step;j++){
#if 0 
			global_cost[i*r_step + j] = calc_total_cost(w_tracker, r_tracker, HIT_TRACKER_NUM, 
				SSD_MAX_PAGES/HIT_TRACKER_NUM*i, 				
				(double)j*100/r_step,
				0, 0, 0,0);
#else
			global_cost[i*r_step + j] = calc_gc_total_cost(w_tracker, r_tracker, HIT_TRACKER_NUM, 
				SSD_MAX_PAGES/HIT_TRACKER_NUM*i, 				
				(double)j*100/r_step,
				0, 0, 0,0);
#endif 
			
			if(i == 1 && j == 1){
				min_cost = global_cost[i*r_step + j];
				min_i = 0;
				min_j = 0;
			}else{
				if(min_cost > global_cost[i*r_step + j]){
				//if(min_cost >= global_cost[i*r_step + j]){
					min_cost = global_cost[i*r_step + j];
					min_i = i;
					min_j = j;
//					fprintf(stdout, " %d %d %f\n", i, j, min_cost);
				}
			}
		}
	}	

	free(global_cost);
	if(min_i == 15)
		min_i = min_i;
	
	
	R_VALUE = (double)(min_j)*100/r_step; // * R_P_INC;
	//target_total =(min_i)*(SSD_MAX_PAGES/TRACKER_NUM);
	if(HIT_TRACKER_NUM < 16){
		target_total =(min_i)*(SSD_MAX_PAGES/HIT_TRACKER_NUM);
		target_total += (SSD_MAX_PAGES/16);
	}else{
		target_total =(min_i)*(SSD_MAX_PAGES/HIT_TRACKER_NUM);
		//target_total =(min_i+1)*(SSD_MAX_PAGES/HIT_TRACKER_NUM);
		fprintf(stdout, " Optimal u = %f, r = %f\n", (double)target_total/SSD_MAX_PAGES, (double)min_j/r_step);
	}
	
	
	//printf(" Optimal u = %f, r = %f\n", (double)(min_i+1)/HIT_TRACKER_NUM, (double)min_j/r_step);
	



	//if(S_TOTAL > target_total && resize_count){
	//	int d = S_TOTAL - target_total;
	//	step = SSD_MAX_PAGES/TRACKER_NUM/2;
	//	if(d > step)
	//		target_total = target_total;
	//}
	
	//smoothing algorithm
#if 0
	if(S_TOTAL-max_step > target_total){
		target_total = S_TOTAL - max_step/2;		
		target_total = S_TOTAL - max_step;
		fprintf(stdout, " *Conducting smoothing algorithm .. \n");
	}
#endif 
	S_TOTAL = target_total;

	resize_count++;

	if(S_TOTAL > Wow_Max_High_Water){
		//printf(" S Total = %d, Wow Max High Water = %d\n", S_TOTAL, Wow_Max_High_Water);
		S_TOTAL = Wow_Max_High_Water -1;
	}
	/*printf(" S Total = %d, Wow Max High Water = %d\n", S_TOTAL, Wow_Max_High_Water);
	printf(" min u = %d, min r rate = %d, Cost = %f\n", min_i, min_j *R_P_INC,  global_cost[min_i*r_step + min_j]);	
	printf(" SSD R %.3f(Predicted = %f), W %.3f(Predicted = %f), Disk R %.3f, W %.3f \n", 
		SSD_TOTALCOST_R/SSD_COUNT_R,
		SSD_TOTALCOST_PREDICT_R/SSD_COUNT_PREDICT_R, 
		SSD_TOTALCOST_W/SSD_COUNT_W,
		SSD_TOTALCOST_PREDICT_W/SSD_COUNT_PREDICT_W,
		HDD_TOTALCOST_R/HDD_COUNT_R, 
		HDD_TOTALCOST_W/HDD_COUNT_W);*/
	
	
	calc_rw_partition_size(S_TOTAL, R_VALUE, &new_s_read, &new_s_write, 0, 0);
#endif 
	
//	일단 rw-fcl이 smoothing 알고리즘을 적용하여 성능이 향상 되었는지 검사.
//	rw-fcl처럼 smoothing 알고리즘을 적용
	

#if 0 
	//smoothing algorithm
	if(S_WRITE-max_step > new_s_write){
		new_s_write = S_WRITE - max_step;
		new_s_read = S_TOTAL - new_s_write;
		fprintf(stdout, " Conducting smoothing algorithm .. \n");
	}

	if(new_s_write < 1024)
		new_s_write = 2048;

	new_s_read = S_TOTAL - new_s_write;
#endif 

	S_READ = new_s_read;
	S_WRITE = new_s_write;
	//fprintf(stdout, " W Hit = %.3f(%2d), R Hit = %.3f(%2d)\n", CACHE_HIT(W_CLOCK), W_CLOCK->cm_size/(SSD_MAX_PAGES/TRACKER_NUM), 
	//	CACHE_HIT(R_CLOCK), R_CLOCK->cm_size/(SSD_MAX_PAGES/TRACKER_NUM));
	//fprintf(outputfile, " W Hit = %.3f(%2d), R Hit = %.3f(%2d)\n", CACHE_HIT(W_CLOCK), W_CLOCK->cm_size/(SSD_MAX_PAGES/TRACKER_NUM), 
	//	CACHE_HIT(R_CLOCK), R_CLOCK->cm_size/(SSD_MAX_PAGES/TRACKER_NUM));

	/*fprintf(outputfile, "%d	%d	%d	%f	%f\n", ++resize_count, 
		(S_READ+S_WRITE)*100/SSD_MAX_PAGES, 
		S_READ*100/(S_READ+S_WRITE),
		(double)4*((S_READ+S_WRITE)*100/SSD_MAX_PAGES)/100, 
		(double)4*((S_READ)*100/SSD_MAX_PAGES)/100
		);*/

	


	return min_direction;
}


#if 0
static int adjust_fcmrw_cache_size(struct cache_manager **w_tracker,
struct cache_manager **r_tracker, 
	int tracker_num,
	int u_scale,
	int r_scale)
{
	int i;
	int max;
	int w_diff;
	int target_write = 0, target_read = 0;
	int win = 0;
	double max_hit = 0;
	double max_pos = 0; 

	max = SSD_USABLE_PAGES/(SSD_MAX_PAGES/tracker_num);
	win = SSD_MAX_PAGES/tracker_num;

	for(i = 1;i < max;i++){
		double whit = predict_hit_ratio(w_tracker, tracker_num, (SSD_MAX_PAGES/tracker_num) * i, SSD_MAX_PAGES, 0);
		double rhit = predict_hit_ratio(r_tracker, tracker_num, (SSD_MAX_PAGES/tracker_num) * (max-i), SSD_MAX_PAGES, 0);

		if(whit + rhit > max_hit){
			max_hit = whit+rhit;
			max_pos = i;
		}
	}
#if 0  
	target_write = (double)SSD_USABLE_PAGES * ((double)max_pos/max);
	target_read = SSD_USABLE_PAGES - target_write;
#else
	target_write = (double)SSD_USABLE_PAGES * (1 - READ_REQ_RATE);
	if(target_write < 1024)
		target_write = 2048;

	target_read = SSD_USABLE_PAGES - target_write;

	if(target_read < 1024){
		target_read = 1024;

		target_write = SSD_USABLE_PAGES - target_read;
	}

	
#endif 

	if(S_WRITE-U_INC > target_write){
		S_WRITE -= U_INC;
		S_READ = SSD_USABLE_PAGES - S_WRITE;
	}else{
		S_WRITE = target_write;
		S_READ = target_read;
	}
	
	//printf(" Read %d, Write = %d \n", target_read, target_write);
	//fflush(stdout);
	//S_WRITE = target_write;
	//S_READ = target_read;

	return 0;;
}
#else

static int adjust_fcmrw_cache_size(struct cache_manager **w_tracker,
struct cache_manager **r_tracker, 
	int tracker_num,
	int u_scale,
	int r_scale)
{	
	//float cur_cost;	
	//float max_gain;
	float *global_cost;
	float min_cost;
	int min_j;
	int min_direction = 0;	
	int j;
	int r_step;
	int step;
	int new_s_read = 0, new_s_write = 0;
	int max_step = 0;
	static int resize_conducted = 0;
	int cur_write_free = W_CLOCK->cm_free;
	int cur_read_free = R_CLOCK->cm_free;
	int min_read_size = 0;
	/*if(!resize_conducted && W_CLOCK->cm_count + R_CLOCK->cm_count < SSD_USABLE_PAGES-2048){
		return 0;
	}*/

	//if(!resize_conducted && 
	//	((W_CLOCK->cm_count <= S_WRITE) || (R_CLOCK->cm_count <= S_READ))){
	//	return 0;
	//}


	resize_conducted = 1;


	//r_step = HIT_TRACKER_NUM;
	//r_step *= 4;
	r_step = SSD_MAX_PAGES/R_P_INC;
	

	r_step *= 2;

	max_step = SSD_USABLE_PAGES/r_step;
	min_read_size = SSD_USABLE_PAGES/r_step;


	global_cost = (float *)malloc(sizeof(float) *  r_step);
	memset(global_cost, 0x00, sizeof(float) * r_step);

	/* step을 잘 조율 할 것.*/
	/* hit ratio 커브도 잘 확인 할 것... */
	
	for(j = 1;j < r_step;j++){
		global_cost[j] = calc_total_cost(w_tracker, r_tracker, HIT_TRACKER_NUM, 
			SSD_USABLE_PAGES, 			
			(double)j*100/r_step,
			0, 0, 0,0);
		
		//printf(" global cost[%d] = %f\n", j, global_cost[j]);

		if(j == 1){
			min_cost = global_cost[j];
			//min_i = 0;
			min_j = 1;
		}else{
			if(min_cost > global_cost[j]){
			//if(min_cost >= global_cost[i*r_step + j]){
				min_cost = global_cost[j];
				//min_i = i;
				min_j = j;
			}
		}
	}


	free(global_cost);

	
	R_VALUE = (double)(min_j)*100/r_step; // * R_P_INC;		
	S_TOTAL = SSD_USABLE_PAGES;

	

	fprintf(stdout, " Read %.2f, Write %.2f, total %f\n", (double)R_CLOCK->cm_count/SSD_MAX_PAGES, (double)W_CLOCK->cm_count/SSD_MAX_PAGES, 
		(double)R_CLOCK->cm_count/SSD_MAX_PAGES + (double)W_CLOCK->cm_count/SSD_MAX_PAGES);

	resize_count++;
	calc_rw_partition_size(S_TOTAL, R_VALUE, &new_s_read, &new_s_write, 0, 0);

	/* if there are not steady state ... */
//#define Test
#ifdef TEST
	if(cur_read_free && cur_write_free >= 1024){
		//fprintf(stdout, "*Simple Read Rate = %.2f\n", );
		new_s_write = (double)SSD_USABLE_PAGES * (1 - READ_REQ_RATE);
	}else{
#endif 
		new_s_write = new_s_write;
		if(cur_write_free >= 1024){
			cur_write_free = cur_write_free;
		}
		if(cur_read_free)
			cur_read_free = cur_read_free;
	//}

	fprintf(stdout, "*Optimal Read Rate = %.2f, simeple = %.2f \n", R_VALUE, READ_REQ_RATE*100);
	fprintf(outputfile, "*Optimal Read Rate = %.2f, simeple = %.2f \n", R_VALUE, READ_REQ_RATE*100);


	if(new_s_write < 1024)
		new_s_write = 2048;

	new_s_read = SSD_USABLE_PAGES - new_s_write;

	if(new_s_read < min_read_size){
		new_s_read = min_read_size;

		new_s_write = SSD_USABLE_PAGES - new_s_read;
	}

	//smoothing algorithm
	if(S_WRITE-max_step > new_s_write){
		new_s_write = S_WRITE - max_step;
		new_s_read = S_TOTAL - new_s_write;
		fprintf(stdout, " Conducting smoothing algorithm Write.. \n");
	}

	//if(S_READ-max_step > new_s_read){
	//	new_s_read = S_READ - max_step;
	//	new_s_write = S_TOTAL - new_s_read;
	//	fprintf(stdout, " Conducting smoothing algorithm Read.. \n");
	//}



	if(new_s_write < 1024)
		new_s_write = 2048;

	new_s_read = SSD_USABLE_PAGES - new_s_write;

	// 여유 페이지가 없어야 새로운 것이 적용이 된다.
#ifdef TEST
	if(cur_write_free <= 1024 || cur_read_free <= 0){ // free pages are remain
#endif 
		S_WRITE = new_s_write;
		S_READ = new_s_read;
		//printf(" change cache size ... \n");
#ifdef TEST
	}else{
		printf(" no change cache size ... \n");
	}
#endif
	

	
	return 0;;
}

#endif 
static int printf_writeonly_cost_table(struct cache_manager **w_tracker,
struct cache_manager **r_tracker, 
	int tracker_num
	)
{
	float cost[MAX_DIRECTION];
	float gain[MAX_DIRECTION];
	float cur_cost;	
	float max_gain;
	float *global_cost;
	float min_cost;
	int min_i = 0,min_j = 0;
	float max_cost;
	int max_i = 0, max_j = 0;
	int min_direction;	
	int i;
	//int r_step;
	int u_scale = 1;
	int r_scale = 1;



	fprintf(outputfile, "\n Write Only Cost Table ... \n");


	//r_step = 100/R_P_INC;
	//if(100 % R_P_INC)
	//r_step++;

	global_cost = (float *)malloc(sizeof(float) * tracker_num);
	memset(global_cost, 0x00, sizeof(float) * tracker_num);


	for(i = 1;i < tracker_num; i++){

		global_cost[i] = calc_writeonly_cost(w_tracker, tracker_num, 
			SSD_MAX_PAGES/tracker_num*i);

		fprintf(outputfile, "%d	%f\n", i*100/tracker_num, global_cost[i]/1000);

		if(i == 1){
			min_cost = global_cost[i];
			min_i = 0;
			min_j = 0;
			max_cost = min_cost;
			max_i = min_i;
			max_j = min_j;

		}else{
			if(min_cost > global_cost[i]){
				min_cost = global_cost[i];
				min_i = i;
				//min_j = j;
			}
			if(max_cost < global_cost[i]){
				max_cost = global_cost[i];
				max_i = i;
				//max_j = j;
			}

		}		
	}	
	fprintf(outputfile, "\n");

	fprintf(outputfile, " Min util = %d, Min read ratio = %d, Min cost %f\n\n", min_i*100/tracker_num, min_j*100/tracker_num, min_cost);
	fprintf(outputfile, " Max util = %d, Max read ratio = %d, Max cost %f\n\n", max_i*100/tracker_num, max_j*100/tracker_num, max_cost);


	for(i = 1;i < tracker_num; i++){
		//for(j = 1;j < r_step;j++){
		global_cost[i] = calc_writeonly_cost(w_tracker, tracker_num, 
			SSD_MAX_PAGES/tracker_num*i);

		fprintf(outputfile, "%d	%f\n", i*100/tracker_num, global_cost[i]/min_cost);
		//}

	}
	fprintf(outputfile, "\n");

	free(global_cost);

	return 0;
}

static int printf_readonly_cost_table(struct cache_manager **w_tracker,
struct cache_manager **r_tracker, 
	int tracker_num
	)
{
	float cost[MAX_DIRECTION];
	float gain[MAX_DIRECTION];
	float cur_cost;	
	float max_gain;
	float *global_cost;
	float min_cost;
	int min_i = 0,min_j = 0;
	float max_cost;
	int max_i = 0, max_j = 0;
	int min_direction;	
	int i;
	//int r_step;
	int u_scale = 1;
	int r_scale = 1;



	fprintf(outputfile, "\n Read Only Cost Table ... \n");


	//r_step = 100/R_P_INC;
	//if(100 % R_P_INC)
	//r_step++;

	global_cost = (float *)malloc(sizeof(float) * tracker_num);
	memset(global_cost, 0x00, sizeof(float) * tracker_num);


	for(i = 1;i < tracker_num; i++){
		
			global_cost[i] = calc_readonly_cost(r_tracker, tracker_num, 
				SSD_MAX_PAGES/tracker_num*i);

			fprintf(outputfile, "%d	%f\n", i*100/tracker_num, global_cost[i]/1000);

			if(i == 1){
				min_cost = global_cost[i];
				min_i = 0;
				min_j = 0;
				max_cost = min_cost;
				max_i = min_i;
				max_j = min_j;

			}else{
				if(min_cost > global_cost[i]){
					min_cost = global_cost[i];
					min_i = i;
					//min_j = j;
				}
				if(max_cost < global_cost[i]){
					max_cost = global_cost[i];
					max_i = i;
					//max_j = j;
				}

			}		
	}	
	fprintf(outputfile, "\n");

	fprintf(outputfile, " Min util = %d, Min read ratio = %d, Min cost %f\n\n", min_i*100/tracker_num, min_j*100/tracker_num, min_cost);
	fprintf(outputfile, " Max util = %d, Max read ratio = %d, Max cost %f\n\n", max_i*100/tracker_num, max_j*100/tracker_num, max_cost);


	for(i = 1;i < tracker_num; i++){
		//for(j = 1;j < r_step;j++){
			global_cost[i] = calc_readonly_cost(r_tracker, tracker_num, 
				SSD_MAX_PAGES/tracker_num*i);

			fprintf(outputfile, "%d	%f\n", i*100/tracker_num, global_cost[i]/min_cost);
		//}
		
	}
	fprintf(outputfile, "\n");

	free(global_cost);

	return 0;
}

static int printf_cost_table(struct cache_manager **w_tracker,
struct cache_manager **r_tracker, 
	int tracker_num
	)
{
	float cost[MAX_DIRECTION];
	float gain[MAX_DIRECTION];
	float cur_cost;	
	float max_gain;
	float *global_cost;
	float min_cost;
	int min_i = 0,min_j = 0;
	float max_cost;
	int max_i = 0, max_j = 0;
	int min_direction;	
	int i, j;
	int r_step;
	int u_scale = 1;
	int r_scale = 1;



	fprintf(outputfile, "\n Expected Cost Table ... \n");


	//r_step = 100/R_P_INC;
	r_step = tracker_num;
	//if(100 % R_P_INC)
		//r_step++;

	global_cost = (float *)malloc(sizeof(float) * tracker_num * r_step);
	memset(global_cost, 0x00, sizeof(float) * tracker_num * r_step);


	for(i = 1;i < tracker_num; i++){
		for(j = 1;j < r_step;j++){
			global_cost[i*r_step + j] = calc_total_cost(w_tracker, r_tracker, tracker_num, 
				SSD_MAX_PAGES/tracker_num*i, 
				//R_P_INC*j,
				(double)j*100/r_step,
				0, 0, 0,0);

			fprintf(outputfile, "%d %d %f\n", i*100/tracker_num, j*100/r_step, global_cost[i*r_step + j]);

			if(i == 1 && j == 1){
				min_cost = global_cost[i*r_step + j];
				min_i = 0;
				min_j = 0;
				max_cost = min_cost;
				max_i = min_i;
				max_j = min_j;

			}else{
				if(min_cost > global_cost[i*r_step + j]){
					min_cost = global_cost[i*r_step + j];
					min_i = i;
					min_j = j;
				}
				if(max_cost < global_cost[i*r_step + j]){
					max_cost = global_cost[i*r_step + j];
					max_i = i;
					max_j = j;
				}

			}
		}
		fprintf(outputfile, "\n");
	}	
	
	fprintf(outputfile, " Min util = %d, Min read ratio = %d, Min cost %f\n\n", min_i*100/tracker_num, min_j*100/r_step, min_cost);
	fprintf(outputfile, " Max util = %d, Max read ratio = %d, Max cost %f\n\n", max_i*100/tracker_num, max_j*100/r_step, max_cost);
	
	fprintf(stdout, " Min util = %d, Min read ratio = %d, Min cost %f\n\n", min_i*100/tracker_num, min_j*100/r_step, min_cost);
	fprintf(stdout, " Max util = %d, Max read ratio = %d, Max cost %f\n\n", max_i*100/tracker_num, max_j*100/r_step, max_cost);

	for(i = 1;i < tracker_num; i++){
		for(j = 1;j < r_step;j++){
			global_cost[i*r_step + j] = calc_total_cost(w_tracker, r_tracker, tracker_num, 
				SSD_MAX_PAGES/tracker_num*i, 
				//R_P_INC*j,
				(double)j*100/r_step,
				0, 0, 0,0);

			fprintf(outputfile, "%d %d %f\n", i*100/tracker_num, j*100/r_step, global_cost[i*r_step + j]/min_cost);
		}
		fprintf(outputfile, "\n");
	}

	free(global_cost);

	return 0;
}

void decay_hit_tracker(struct cache_manager **lru_manager,int lru_num){
	int i = 0;

	for(i = 0;i < lru_num;i++){		
		lru_manager[i]->cm_hit/=2;
		lru_manager[i]->cm_read_hit/=2;
		lru_manager[i]->cm_ref /= 2;
		lru_manager[i]->cm_read_ref /= 2;
	}
}
int update_hit_tracker(struct cache_manager **lru_manager,int lru_num, int blkno,int is_read){
	int hit_position = -1;
	listnode *node; 
	struct lru_node *ln;

	node = mlru_search(lru_manager, lru_num, blkno, 1, 1, &hit_position);
	

	if(is_read){
		if(hit_position > S_READ/(SSD_MAX_PAGES/lru_num))
			g_GHOST_HIT = 1;
		else
			g_GHOST_HIT = 0;

		if(node){
			lru_manager[hit_position]->cm_read_hit++;
			lru_manager[0]->cm_read_ref++;
		}
	}else{
		if(hit_position > S_WRITE/(SSD_MAX_PAGES/lru_num))
			g_GHOST_HIT = 1;
		else
			g_GHOST_HIT = 0;
	}

	return hit_position;
}


int reverse_get_blk(int ssdblk){	
	return reverse_map[ssdblk];
}

void reverse_map_create(int max){
	int i;

	reverse_max_pages = max;

	reverse_map = (int *)malloc(sizeof(int) * reverse_max_pages);
	if(reverse_map == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(0);
	}

	for(i = 0;i < reverse_max_pages;i++){
		reverse_map[i] = -1;
	}
	reverse_used = 0;
	reverse_free = reverse_max_pages-1;
	reverse_alloc = 1;

	fprintf(stdout, " Reverse Map Allocation = %fKB\n", (double)sizeof(int)*reverse_max_pages/1024);

	ll_create(&reverse_freeq);
}




int reverse_map_alloc_blk(int hdd_blk){
	int i;
	int alloc_blk = -1;

	
	if(ll_get_size(reverse_freeq)){
		reverse_alloc = (int)ll_get_tail(reverse_freeq);		
		ll_release_tail(reverse_freeq);
	}

	for(i = 1;i < reverse_max_pages;i++){

		if(reverse_map[reverse_alloc] == -1){
			alloc_blk = reverse_alloc;
			break;
		}

		reverse_alloc++;
		if(reverse_alloc == reverse_max_pages)
			reverse_alloc = 1;
	}


	if(alloc_blk > 0){
		reverse_free--;
		if(reverse_free < 0){
			fprintf(stderr, " check reverse_map_alloc_blk \n");
			reverse_free = reverse_free;
		}
		reverse_used++;
		reverse_map[reverse_alloc] = hdd_blk;
	}
	if(alloc_blk == -1){
		fprintf(stderr, " Cannot allocate block .. \n");
		fprintf(stderr, " SSD Usable Pages = %d \n", SSD_USABLE_PAGES);
		fprintf(stderr, " Reverse max pages = %d\n", reverse_max_pages);
		fprintf(stderr, " S_Read = %d, S_Write = %d, total = %d \n", S_READ, S_WRITE, S_READ + S_WRITE);
		fprintf(stderr, " R_CLOCK count = %d, W_CLOCK count = %d, total = %d\n", R_CLOCK->cm_count, W_CLOCK->cm_count, R_CLOCK->cm_count+W_CLOCK->cm_count);
		fprintf(stderr, " R_CLOCK free = %d, W_CLOCK free = %d\n", R_CLOCK->cm_free, W_CLOCK->cm_free);
		fprintf(stderr, " R_CLOCK size = %d, W_CLOCK size = %d\n", R_CLOCK->cm_size, W_CLOCK->cm_size);
		fprintf(stderr, " HDD WQ = %d, HDD RQ = %d\n", ll_get_size(W_CLOCK->cm_hddwq), ll_get_size(W_CLOCK->cm_hddrq));
		fprintf(stderr, " SSD WQ = %d, SSD RQ = %d\n", ll_get_size(W_CLOCK->cm_ssdwq), ll_get_size(W_CLOCK->cm_ssdrq));

		exit(0);
	}
	if(reverse_free == 0){
		reverse_free = reverse_free;
	}

	//if(alloc_blk == 3866)
	//	alloc_blk = alloc_blk;
	return alloc_blk;
}


int reverse_map_check_sync(int hdd_blk){
	//int i;
	
	return 0;
}


int reverse_map_release_blk(int ssd_blk){
	int i;	

	
	if(ssd_blk < 1 || ssd_blk >= reverse_max_pages){
		fprintf(stderr, " invalid ssd blkno = %d \n", ssd_blk);
		return -1;
	}

	//if(ssd_blk == 9630){
	//		printf(" release = 9630\n");
	//}
//	ssd_trim_command(SSD, ssd_blk * BLOCK2SECTOR);


	reverse_free++;
	reverse_used--;
	reverse_map[ssd_blk] = -1;
	reverse_alloc = ssd_blk;

	ll_insert_at_head(reverse_freeq, (void *)ssd_blk);

	return ssd_blk;
}


void reverse_map_free(){
	free(reverse_map);
	ll_release(reverse_freeq);
}

void measure_avg_iotime(int devno, int rw, double now, double temp, int count){
	double diff = (now-temp)/count;
	
	if(DISK == devno){
		if(rw == READ){
			HDD_COUNT_R++;
			HDD_TOTALCOST_R+= diff;
		}else{
			HDD_COUNT_W++;
			HDD_TOTALCOST_W+= diff;
		}				
	}

	if(SSD == devno){
		if(rw == READ){			
			SSD_COUNT_R++;
			SSD_TOTALCOST_R+= diff;

			SSD_COUNT_PREDICT_R++;
			SSD_TOTALCOST_PREDICT_R += (double)(SSD_READ + SSD_BUS)/1000;
		}else{
			SSD_COUNT_W++;
			SSD_TOTALCOST_W+= diff;

			SSD_COUNT_PREDICT_W++;
			SSD_TOTALCOST_PREDICT_W+= (double)SSD_PW(ssd_predict_util((double)S_TOTAL/SSD_MAX_PAGES))/1000;

			//SSD_GC_TIME += (now-temp - (double)(count * (SSD_PROG + SSD_BUS))/1000);
		}		
	}
#if 0 
	if((SSD_COUNT_W + SSD_COUNT_R) % 8192 == 0)
		printf(" SSD R %.3f(Predicted = %f), W %.3f(Predicted = %f), Disk R %.3f, W %.3f \n", 
		SSD_TOTALCOST_R/SSD_COUNT_R,
		SSD_TOTALCOST_PREDICT_R/SSD_COUNT_PREDICT_R, 
		SSD_TOTALCOST_W/SSD_COUNT_W,
		SSD_TOTALCOST_PREDICT_W/SSD_COUNT_PREDICT_W,
		HDD_TOTALCOST_R/HDD_COUNT_R, 
		HDD_TOTALCOST_W/HDD_COUNT_W);

#endif 

}
void rw_issue_req(int devno, int blk, int rw){
	struct disksim_request r;
	double temp = now;

	wow_make_request(&r, now, devno, rw, blk*BLOCK2SECTOR, BLOCK2SECTOR);
	wow_issue_request(&r);
}


void lru_ssd_read(struct cache_manager *c, int devno, int rw, listnode *Qlist,int trim){
	double temp = now;

	lru_flush_reqlist(c, devno, rw, Qlist,  trim);

	SSD_READ_TIME += (now-temp);
}

void lru_ssd_write(struct cache_manager *c, int devno, int rw, listnode *Qlist,int trim){
	double temp = now;
	lru_flush_reqlist(c, devno, rw, Qlist,  trim);

	SSD_WRITE_TIME += (now-temp);
}
void lru_stage(struct cache_manager *clock){
	double temp = now;
	double temp2;

	lru_flush_reqlist(clock, DISK, READ, clock->cm_hddrq, 0);
	STAGE_HDD_READ += (now-temp);
	temp2 = now;
	lru_flush_reqlist(clock, SSD, WRITE, clock->cm_ssdwq, 0);
	STAGE_SSD_WRITE += (now-temp2);

	STAGE_TIME += (now-temp);
}

void lru_destage(struct cache_manager *clock){
	double temp = now;
	double temp2;

	lru_flush_reqlist(clock, SSD, READ, clock->cm_ssdrq, 1);
	DESTAGE_SSD_READ += (now-temp);
	
	temp2 = now;
	lru_flush_reqlist(clock, DISK, WRITE, clock->cm_hddwq, 0);
	DESTAGE_HDD_WRITE += (now-temp2);

	DESTAGE_TIME += (now-temp);
}


int search_dram_cache(struct cache_manager *d_cache, int blkno, int is_read){
	struct lru_node *ln = NULL;
	listnode *dram_node;
	int dram_hit = 0;
	dram_node = CACHE_SEARCH(d_cache, blkno);
	if(!dram_node){
		ln = CACHE_REPLACE(d_cache, 0);
		ln = CACHE_ALLOC(d_cache, ln, blkno);
		CACHE_INSERT(d_cache, ln);
	}else{
		ln = CACHE_REMOVE(d_cache, dram_node);
		CACHE_INSERT(d_cache, ln);
		dram_hit = 1;
		if(is_read == READ)
			is_read = is_read;

	}

	dram_hit = 0;
	return dram_hit;
}

void rw_replacement(struct cache_manager *flash_cache,int is_read){
	int count;
	struct clock_node *cn = NULL;	


	count = flash_cache->cm_highwater;
	while(flash_cache->cm_free < flash_cache->cm_highwater && count){
		cn = CACHE_REPLACE(flash_cache, flash_cache->cm_highwater);

		//write도 안했는데 destaging 하는 경우....

		if(cn && cn->cn_ssd_blk > 0){
			//if(cn->cn_ssd_blk == 3866){
			//	printf(" destage = 3866\n");
			//}

			/* Destaging ... Write ReQ */
			if(cn->cn_dirty){ 
				CACHE_MAKERQ(flash_cache, flash_cache->cm_ssdrq, NULL, cn->cn_ssd_blk);
				CACHE_MAKERQ(flash_cache, flash_cache->cm_hddwq, NULL, reverse_get_blk(cn->cn_ssd_blk));
				search_dram_cache(dram_cache, reverse_get_blk(cn->cn_ssd_blk), is_read);
				flash_cache->cm_destage_count++;
			}else{						
				ssd_trim_command(SSD, cn->cn_ssd_blk * BLOCK2SECTOR);
			}	
			reverse_map_release_blk(cn->cn_ssd_blk);
			free(cn);
		}
		if(flash_cache->cm_count == 0)
			break;

		//if(!is_read)
		/* 2012 01 07 11:16 */
		//if(flash_cache->cm_policy == CACHE_LRU_RWO_ADAPTIVE)
			//count--;
	}
}
static void rw_make_req(struct cache_manager *flash_cache, int blkno, int is_read,int dram_hit, int seq_io){
	struct clock_node *cn = NULL;	
	listnode *node = NULL;
	int is_adaptive;
	
	is_adaptive = (flash_cache->cm_policy == CACHE_LRU_RWO_ADAPTIVE || flash_cache->cm_policy == CACHE_LRU_RW_ADAPTIVE  || flash_cache->cm_policy == CACHE_BGCLOCK_RWO_ADAPTIVE);

	node = CACHE_SEARCH(flash_cache, blkno);
	if(seq_io){
		if(node){ 
			//invalidate 			
			cn = CACHE_REMOVE(flash_cache, node);
			ssd_trim_command(SSD, cn->cn_ssd_blk * BLOCK2SECTOR);
			reverse_map_release_blk(cn->cn_ssd_blk);
			free(cn);
		}

		if(is_read){
			CACHE_MAKERQ(flash_cache, flash_cache->cm_hddrq, NULL, blkno);
		}else{
			CACHE_MAKERQ(flash_cache, flash_cache->cm_hddwq, NULL, blkno);
		}
		return;
	}

	//if(is_adaptive && R_CLOCK->cm_free < 0 && !is_read){
	//	rw_replacement(R_CLOCK, READ);
	//}


	if(!node){ /* miss */

		if(flash_cache->cm_free <= flash_cache->cm_lowwater){
			int count;
			if(is_read && !is_adaptive){
				lru_stage(flash_cache);				
				lru_ssd_read(flash_cache, SSD, READ, flash_cache->cm_ssdrq, 0);
			}
			
			if(is_adaptive){
				if(is_read){
					while(R_CLOCK->cm_free == 0  && W_CLOCK->cm_free > 1024 && W_CLOCK->cm_size > 2048){					
						CACHE_INC(R_CLOCK, 1);
						CACHE_DEC(W_CLOCK, 1);
					}
				}else{
					while(W_CLOCK->cm_free == 0 && R_CLOCK->cm_free > 0){					
						CACHE_INC(W_CLOCK, 1);
						CACHE_DEC(R_CLOCK, 1);
					}
				}
			}
			
			rw_replacement(flash_cache, is_read);
			
			
			if(is_read && !is_adaptive){				
				lru_destage(flash_cache);
			}
		}

		cn = CACHE_REPLACE(flash_cache, flash_cache->cm_lowwater);
		if(cn && cn->cn_ssd_blk > 0){						
			/* Destaging ... Write ReQ */
			if(cn->cn_dirty){ 
				CACHE_MAKERQ(flash_cache, flash_cache->cm_ssdrq, NULL, cn->cn_ssd_blk);
				CACHE_MAKERQ(flash_cache, flash_cache->cm_hddwq, NULL, reverse_get_blk(cn->cn_ssd_blk));
				search_dram_cache(dram_cache, reverse_get_blk(cn->cn_ssd_blk), is_read);
				flash_cache->cm_destage_count++;
			}	
			reverse_map_release_blk(cn->cn_ssd_blk);
		}

		cn = CACHE_ALLOC(flash_cache, cn, blkno);		
		cn->cn_ssd_blk = reverse_map_alloc_blk(blkno);
		//if(cn->cn_ssd_blk == 9630){
		//	printf(" alloc = 9630 %d\n", is_read);
		//}
			
		//if(cn->cn_ssd_blk == -1 && W_CLOCK->cm_free < 0){
		//	if(is_read){
		//		fprintf(stdout, "is read ... \n");
		//	}else{
		//		fprintf(stdout, "is write ... \n");
		//	}
		//	FCM_REQ_FLAG = WRITE;				
		//	lru_destage(W_CLOCK);			
		//	//lru_ssd_write(W_CLOCK, SSD, WRITE, W_CLOCK->cm_ssdwq, 0);

		//	cn->cn_ssd_blk = reverse_map_alloc_blk(blkno);
		//}

		CACHE_INSERT(flash_cache, cn);
		
		/*  Staging  Read ReQ */
		if(is_read){
			CACHE_MAKERQ(flash_cache, flash_cache->cm_hddrq, NULL, blkno);
			CACHE_MAKERQ(flash_cache, flash_cache->cm_ssdwq, NULL, cn->cn_ssd_blk);
			flash_cache->cm_stage_count++;
//			search_dram_cache(dram_cache, blkno, is_read);
			dram_hit = 1;
		}

	}else{ /* hit */
		cn = (struct clock_node *)node->data;
		//cn->cn_frequency++;
		cn = CACHE_REMOVE(flash_cache, node);
		CACHE_INSERT(flash_cache, cn);
	}

	if(is_read){
		if(!dram_hit){
			CACHE_MAKERQ(flash_cache, flash_cache->cm_ssdrq, NULL, cn->cn_ssd_blk);
		}
		cn->cn_read = 1;
		if(cn->cn_dirty){
			cn = cn;
		}
	}else{
		CACHE_MAKERQ(flash_cache, flash_cache->cm_ssdwq, NULL, cn->cn_ssd_blk);
		cn->cn_dirty = 1;
		if(cn->cn_read)
			cn = cn;
	}
	
}
void print_statistic_information(FILE *output){

}





void resize_rw_cache_size(struct cache_manager *r_clock, struct cache_manager *w_clock){
	int r_diff;
	int w_diff;
	int r_diff2;
	int w_diff2;
	
	if(S_WRITE == w_clock->cm_size 
		&& S_READ == r_clock->cm_size)
		return;


	if(S_READ + S_WRITE > Wow_Max_High_Water)
		return;

	r_diff = S_READ - r_clock->cm_size;
	w_diff = S_WRITE - w_clock->cm_size;	


	r_diff2 = S_READ - r_clock->cm_count;
	w_diff2 = S_WRITE - w_clock->cm_count;
	if(r_diff > 0){
		lru_inc(r_clock, r_diff);
	}else if(r_diff < 0){
		lru_dec(r_clock, -r_diff);
	}

	if(w_diff > 0){
		lru_inc(w_clock, w_diff);
	}else if(w_diff < 0){
		lru_dec(w_clock, -w_diff);
	}
	if(w_clock->cm_free < 0)
		w_clock = w_clock;

	if(r_diff2 < 0 || w_diff2 < 0)
		r_diff2 = r_diff2;

	printf(" r_diff = %fMB, w_diff = %fMB\n", (float)r_diff/256, (float)w_diff/256); 
	printf(" r_diff2 = %fMB, w_diff2 = %fMB\n", (float)r_diff2/256, (float)w_diff2/256); 
	fprintf(outputfile, " r_diff2 = %fMB, w_diff2 = %fMB\n", (float)r_diff2/256, (float)w_diff2/256); 
	//fprintf(stdout, " u = %.2f, r = %.2f, whit = %.2f, rhit = %.2f\n", 
	//	(float)S_TOTAL/SSD_MAX_PAGES, (float)R_VALUE/100, CACHE_HIT(w_clock), CACHE_HIT(r_clock));
	//fflush(stdout);
}

void adaptive_gen_req_with_bgclock(struct cache_manager *r_clock, struct cache_manager *w_clock, ioreq_event *req, int blkno,int dram_hit,int seq_io){
	listnode *node, *gnode;
	struct lru_node *ln;
	struct bglru_node *bn;
	listnode *node2;

	/* Write Req */
	if(!(req->flags & READ)){
		rc_write++;

		if(!seq_io){
			node = CACHE_PRESEARCH(r_clock, blkno);
			if(node){
				ln = CACHE_REMOVE(r_clock, node);
				gnode = CACHE_SEARCH(w_clock, blkno);

				if(gnode == NULL){
					/* Alloc new group node */
					bn = CACHE_ALLOC(w_clock, NULL, blkno/GRP2BLK);
					CACHE_INSERT(w_clock, bn);			
				}else{
					bn = (struct bglru_node *)gnode->data;
				}

				ln = bgclock_insert_block_to_group(w_clock, bn, blkno, ln);
			}

			update_hit_tracker(W_HIT_TRACKER,HIT_TRACKER_NUM, blkno, req->flags);
		}
		
		bgclock_make_req(w_clock, blkno, req->flags, dram_hit, seq_io);
		
		
	/* Read Req */
	}else if(req->flags & READ){
		int ghost_hit;
		rc_read++;
 
		
		if(!seq_io){
			update_hit_tracker(R_HIT_TRACKER,HIT_TRACKER_NUM, blkno, req->flags);
		}		
		if(CACHE_PRESEARCH(w_clock, blkno)){			
			bgclock_make_req(w_clock, blkno, req->flags, dram_hit, seq_io);
		}else{
			rw_make_req(r_clock, blkno, req->flags, dram_hit, seq_io);
		}

		
	}

}


void gen_req_with_bgclock(struct cache_manager *clock, ioreq_event *req, int blkno,int dram_hit, int seq_io){
	listnode *node, *gnode;
	struct lru_node *ln;
	struct bglru_node *bn;
	listnode *node2;

	/* Write Req */
	if(!(req->flags & READ)){		
		bgclock_make_req(clock, blkno, req->flags, dram_hit, seq_io);
		rc_write++;	
	/* Read Req */
	}else if(req->flags & READ){		
		bgclock_make_req(clock, blkno, req->flags, dram_hit, seq_io);
		rc_read++;
	}
}

/* op-fcl을 돌려서 ideal 90%으로 나오면 고칠 필요가 없다 */
#if 0 
	//쓰기캐시에서 읽기가 발생하면 
	if(mlru_search(W_HIT_TRACKER, tracker_num, blkno+j, 0, 0, &hit_position)){
		update_hit_tracker(W_HIT_TRACKER, tracker_num, blkno+j, req.flags);
	}else{
		update_hit_tracker(R_HIT_TRACKER, tracker_num, blkno+j, req.flags);
	}

	//읽기에서 쓰기가 발생하면
	node = mlru_search(R_HIT_TRACKER, tracker_num, blkno+j, 0, 0, &hit_position);
	if(node){
		mlru_remove(R_HIT_TRACKER, tracker_num, blkno+j);
	}
	update_hit_tracker(W_HIT_TRACKER, tracker_num, blkno+j, req.flags);
#endif 

void adaptive_gen_req(struct cache_manager *r_clock, struct cache_manager *w_clock, ioreq_event *req, int blkno, int dram_hit, int seq_io, int selective){
	listnode *node;
	struct lru_node *cn;
	listnode *node2;
	int temp;

	/* Write Req */
	if(!(req->flags & READ)){
		rc_write++;
		nr_serviced_ws++;

		if(!seq_io){
			node = CACHE_PRESEARCH(r_clock, blkno);
			if(node){
				cn = CACHE_REMOVE(r_clock, node);
				CACHE_INSERT(w_clock, cn);
				rc_write_in_read++;
			}
			//update_hit_tracker(W_HIT_TRACKER,TRACKER_NUM, blkno, req->flags);
			node = mlru_search(R_HIT_TRACKER, HIT_TRACKER_NUM, blkno, 0, 0, &temp);
			if(node){
				mlru_remove(R_HIT_TRACKER, HIT_TRACKER_NUM, blkno);
			}
			update_hit_tracker(W_HIT_TRACKER, HIT_TRACKER_NUM, blkno, req->flags);
		}
		
		rw_make_req(w_clock, blkno, req->flags, dram_hit, seq_io);
		
		/* 
		  read hit tracker에서 write req가 hit 되면 write hit tracker로 옮긴다.
		*/
	/* Read Req */
	}else if(req->flags & READ){				
		int ghost_hit = 0;		
		//rc_read++;

				
		if(CACHE_PRESEARCH(w_clock, blkno)){
			rc_write++;
			rc_read_in_write++;
			nr_serviced_ws++;
			if(!seq_io){
				update_hit_tracker(W_HIT_TRACKER, HIT_TRACKER_NUM, blkno, req->flags);			
			}
			rw_make_req(w_clock, blkno, req->flags, dram_hit, seq_io);
		}else{
			if(!seq_io){
				ghost_hit = update_hit_tracker(R_HIT_TRACKER, HIT_TRACKER_NUM, blkno, req->flags);			
			}
			rc_read++;
			nr_serviced_rs++;

			//selective caching
			if(selective){
				if(ghost_hit != -1){
					rw_make_req(r_clock, blkno, req->flags, dram_hit, seq_io);
				}else{			
					CACHE_MAKERQ(r_clock, r_clock->cm_hddrq, NULL, blkno);
				}
			}else{
				rw_make_req(r_clock, blkno, req->flags, dram_hit, seq_io);
			}			
		}		
	}

}

void init_rw_cache_size(){
	int window = SSD_MAX_PAGES/HIT_TRACKER_NUM;
	int r, w;

	r = R_CLOCK->cm_count/window;
	if(R_CLOCK->cm_count % window)
		r++;

	w = W_CLOCK->cm_count/window;
	if(W_CLOCK->cm_count % window)
		w++;

	
	S_TOTAL = r * window + w * window;
	R_VALUE = (double)r * window * 100 / S_TOTAL;
	
	calc_rw_partition_size(S_TOTAL, R_VALUE, &S_READ, &S_WRITE, 0, 0);
}

static int compare_blkno(const void *a,const void *b){
	//if((int)a < (int)b)
	//	return 1;
	//else if((int)a > (int)b)
	//	return -1;
	//return 0;

	return *(int *)a - *(int *)b;
}



void lru_sort_reqlist(listnode *Qlist){
	listnode *node;
	int *a;
	int i, count;
	count = ll_get_size(Qlist);

	a = malloc(sizeof(int) * count);
	if(a == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(1);
	}

	memset(a, 0x00, sizeof(int) * count);

	i = 0;
	node = Qlist->next;
	while(node != Qlist){
		a[i++] = (int)node->data;		
		NODE_NEXT(node);
	}

	qsort(a, count, sizeof(int), compare_blkno);
	
	while(ll_get_size(Qlist)){
		ll_release_tail(Qlist);
	}

	for(i = 0;i < count;i++){
		ll_insert_at_head(Qlist,(void *)a[i]);
	}

	free(a);

}

void lru_flush_reqlist(struct cache_manager *c, int devno, int rw, listnode *Qlist,int trim){
	struct disksim_request r;
	SysTime currTime = now;
	SysTime temp = now;
	listnode *rq;
	int i, rq_count;

	rq_count = ll_get_size(Qlist);

	if(rq_count == 0)
		return;


	if(rq_count > 1){
		lru_sort_reqlist(Qlist);		
	}

	wow_init_request(&r);
	rq = Qlist->prev;

	for(i = 0;i < rq_count;i++){
		int blk = (int)rq->data;

		//if(blk == 9630)
		//	blk = blk;
		blk *= BLOCK2SECTOR;

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

		NODE_PREV(rq);
	}
	if(r.bytecount)
		wow_issue_request(&r);


	measure_avg_iotime(devno, rw, now, temp, rq_count);


	if(trim){

		rq = Qlist->prev;
		for(i = 0;i < rq_count;i++){
			int blk = (int)rq->data;

			//if(blk == 9630){
			//	printf(" trim = 9630\n");
			//}
			ssd_trim_command(SSD, blk * BLOCK2SECTOR);

			NODE_PREV(rq);
		}		
	}


	CACHE_RELEASERQ(c, Qlist);
}

void print_hit_ratio_curve(struct cache_manager **lru_manager, int ref, int lru_num, FILE *fp){
	int i;
	int	hit = 0;
	int prev_hit = 0;
	fprintf(fp, " Printf Hit Ratio Curve\n");
	fprintf(fp, "# Index	Hit Ratio	Marginal Gain\n");
	for(i = 0;i < lru_num;i++){		
		prev_hit = hit;
		hit += lru_manager[i]->cm_hit;
		fprintf(fp, " %d	%.3f	%.3f	%.3f	%d\n", 
			(i+1)*100/lru_num, 
			(double)hit*100/ref, 
			(double)hit*100/ref-(double)prev_hit*100/ref, 
			(double)lru_manager[i]->cm_hit/ref,
			lru_manager[i]->cm_hit);
		//CACHE_CLOSE(lru_manager[i]);
	}

	//printf(" Multi LRU Hit Ratio = %f \n", (float)mlru_hit/mlru_ref);
}
extern int Wow_HDD_Max_Pages;
#define BITSIZE 29
int make_key(int blkno, int devno){
	int temp = devno << BITSIZE;
	unsigned int d = ~0;
	blkno &= (d>>2);
	
	
	return blkno + temp;
}
int workload_simulation(char *filename,int cache_size,int hdd_max, int ssd_max, int bglru, int tracker_num){	
	FILE *tracefile= NULL;
	ioreq_event req;	
	struct disksim_request r;
	int i;	
	int org_hdd_pages = hdd_max;
	int tot_write_bytes = 0.0;
	int tot_read_bytes = 0.0;
	int tot_write_count = 0;
	int tot_read_count = 0;
	double inter_arrival = 0.0;
	int inter_count = 0;
	double arrival_time = 0.0;


	SSD_MAX_PAGES = ssd_max;
	HDD_MAX_PAGES = 268435456;
	HDD_MAX_PAGES = 35913728;
	Wow_HDD_Max_Pages = HDD_MAX_PAGES;

	wow_wss_init(HDD_MAX_PAGES);

	anti_total_cost = 0.0;
#ifdef __linux__
	tracefile = fopen64(filename, "r");
#else
	tracefile = fopen(filename, "r");
#endif 
	if(tracefile == NULL){
		fprintf(stderr," Cannot open file %s\n", filename);
		fprintf(outputfile," Cannot open file %s\n", filename);
		exit(1);
	}
	S_TOTAL = cache_size;
	R_HIT_TRACKER = (struct cache_manager **)mlru_init("R_HIT_TRACKER", tracker_num, SSD_MAX_PAGES);
	W_HIT_TRACKER = (struct cache_manager **)mlru_init("W_HIT_TRACKER", tracker_num, SSD_MAX_PAGES);
	HIT_TRACKER = (struct cache_manager **)mlru_init("HIT_TRACKER", tracker_num, SSD_MAX_PAGES);
	//R_P_INC = 100/tracker_num;
	//printf_cost_table(W_HIT_TRACKER, R_HIT_TRACKER, tracker_num);

	for(i = 0;;i++){		
		int blkno;
		int num, j;
		int count;
		int page_count;
		SysTime prev_now = now;
		
		if(iotrace_ascii_get_ioreq_event(tracefile, &req) == NULL){
			break;
		}

		/* avg req size ...*/			
		if(req.flags == READ){
			tot_read_bytes += req.bcount;
			tot_read_count++;
		}else{
			tot_write_bytes += req.bcount;
			tot_write_count++;
		}
	
	//	if(i){
		inter_arrival += (req.time - arrival_time);
		inter_count++;
		arrival_time = req.time;
		//}
		



		//RealTime req.time;
		RealTime = req.time;

		//sector unit
		blkno = (req.blkno/BLOCK2SECTOR)%HDD_MAX_PAGES;		
		num = req.bcount/BLOCK2SECTOR;
		if(req.bcount%BLOCK2SECTOR)
			num++;

		blkno = make_key(blkno, req.devno);
		/*blkno = (req.blkno)%HDD_MAX_PAGES;		
		num = req.bcount;*/
		

		for(j = 0;j < num;j++){
			int hit_position = -1;
			//wow_wss_refresh(blkno+j, req.flags);			
			rc_total++;
			if(req.flags == READ){
				rc_read++;
								
				if(mlru_search(W_HIT_TRACKER, tracker_num, blkno+j, 0, 0, &hit_position)){
					rc_read_in_write++;
					update_hit_tracker(W_HIT_TRACKER, tracker_num, blkno+j, req.flags);
				}else{
					update_hit_tracker(R_HIT_TRACKER, tracker_num, blkno+j, req.flags);
				}



			}else{
				struct listnode *node;
				rc_write++;

				node = mlru_search(R_HIT_TRACKER, tracker_num, blkno+j, 0, 0, &hit_position);
				if(node){
					mlru_remove(R_HIT_TRACKER, tracker_num, blkno+j);
				}
				update_hit_tracker(W_HIT_TRACKER, tracker_num, blkno+j, req.flags);
			}
			
			update_hit_tracker(HIT_TRACKER, tracker_num, blkno+j, req.flags);
		}

		
		if(i % 50000 == 0){
				fprintf(stdout, "\n Read %d lines ... \n", i);
				fflush(stdout);
		}

				
		count = num / (MAX_REQ_BYTES/PAGE);
		if(num % (MAX_REQ_BYTES/PAGE)){
			count++;
		}
		
#if 0 
		for(j = 0;j < count;j++){
			int req_count;
			
			if(num >= (MAX_REQ_BYTES/PAGE)){
				req_count = (MAX_REQ_BYTES/PAGE);				
			}else{
				req_count = num;
			}
			num -= req_count;

			wow_make_request(&r, now, DISK, req.flags, (blkno%(org_hdd_pages-MAX_REQ_BYTES/PAGE))*BLOCK2SECTOR, req_count*BLOCK2SECTOR);
			wow_issue_request(&r);

			blkno += (MAX_REQ_BYTES/PAGE);

		}
#endif 
		
		//stat_update (&currssd->stat.responsestats,(double)(now - prev_now));
		//wow_response_tracking((double)(now - prev_now));
		//if(i == 10000)
		//	printf_cost_table(W_HIT_TRACKER, R_HIT_TRACKER, tracker_num);

	}

	fprintf(outputfile, " Syssim: Total Request Count	=	%d	%.3f	MB\n", rc_total, (double)rc_total/256);
	fprintf(outputfile, " Syssim: Total Request Count(read)	=	%d	%.3f	MB\n", rc_read, (double)rc_read/256);
	fprintf(outputfile, " Syssim: Total Request Count(write)	=	%d	%.3f	MB\n", rc_write, (double)rc_write/256);
	fprintf(outputfile, " Syssim: Total Request Count(read in writelru)	=	%d	%.3f	MB)\n", rc_read_in_write, (double)rc_read_in_write/256);
	fprintf(outputfile, " Syssim: Total Request Count(write in readlru)	=	%d	%.3f	MB)\n", rc_write_in_read, (double)rc_write_in_read/256);
	fprintf(outputfile, " Syssim: Avg Read Req Size	=	%f KB\n", (double)SECTOR*tot_read_bytes/tot_read_count/1024);
	fprintf(outputfile, " Syssim: Avg Write Req Size	=	%f KB\n", (double)SECTOR*tot_write_bytes/tot_write_count/1024);
	fprintf(outputfile, " Syssim: Inter arrival time	=	%f ms\n", (double)inter_arrival/inter_count);

	fprintf(outputfile, " syssim: Read Reference Ratio	=	%.2f\n", (double)rc_read/rc_total);
	
	
	printf_cost_table(W_HIT_TRACKER, R_HIT_TRACKER, tracker_num);

	fprintf(outputfile, "\n Tracked Hit Ratio Curve ... \n");
	print_hit_ratio_curve(HIT_TRACKER, HIT_TRACKER[0]->cm_ref, tracker_num, outputfile);
	fprintf(outputfile, "\n Read Tracked Hit Ratio Curve ... \n");
	print_hit_ratio_curve(R_HIT_TRACKER, R_HIT_TRACKER[0]->cm_ref, tracker_num, outputfile);
	fprintf(outputfile, "\n Write Tracked Hit Ratio Curve ... \n");
	print_hit_ratio_curve(W_HIT_TRACKER, W_HIT_TRACKER[0]->cm_ref, tracker_num, outputfile);

	mlru_exit(R_HIT_TRACKER, tracker_num);
	mlru_exit(W_HIT_TRACKER, tracker_num);
	mlru_exit(HIT_TRACKER, tracker_num);
	fclose(tracefile);


	return 0;
}

void zipf_main(struct cache_manager **lru_manager, int N, int N2, float theta);
int zipf_simulation(char *filename,int cache_size,int hdd_max, int ssd_max, int bglru){	
	FILE *tracefile= NULL;
	ioreq_event req;	
	struct disksim_request r;
	int i;
	int tracker_num = 64;
	int org_hdd_pages = hdd_max;
	float theta;

	SSD_MAX_PAGES = ssd_max;
	HDD_MAX_PAGES = 268435456;
	HDD_MAX_PAGES = 35913728;

	Wow_HDD_Max_Pages = HDD_MAX_PAGES;

	wow_wss_init(HDD_MAX_PAGES);

	anti_total_cost = 0.0;
#ifdef __linux__
	tracefile = fopen64(filename, "r");
#else
	tracefile = fopen(filename, "r");
#endif 
	if(tracefile == NULL){
		fprintf(stderr," Cannot open file %s\n", filename);
		fprintf(outputfile," Cannot open file %s\n", filename);
		exit(1);
	}
	S_TOTAL = cache_size;
	R_HIT_TRACKER = (struct cache_manager **)mlru_init("R_HIT_TRACKER", tracker_num, SSD_MAX_PAGES);
	W_HIT_TRACKER = (struct cache_manager **)mlru_init("W_HIT_TRACKER", tracker_num, SSD_MAX_PAGES);
	HIT_TRACKER = (struct cache_manager **)mlru_init("HIT_TRACKER", tracker_num, SSD_MAX_PAGES);
	//R_P_INC = 100/tracker_num;
	//printf_cost_table(W_HIT_TRACKER, R_HIT_TRACKER, tracker_num);
#if 0 
	for(i = 0; ;i++){		
		int blkno;
		int num, j;
		int count;
		int page_count;
		SysTime prev_now = now;

		if(iotrace_ascii_get_ioreq_event(tracefile, &req) == NULL){
			break;
		}

		//RealTime req.time;
		RealTime = req.time;

		//sector unit
		blkno = (req.blkno/BLOCK2SECTOR)%HDD_MAX_PAGES;		
		num = req.bcount/BLOCK2SECTOR;
		if(req.bcount%BLOCK2SECTOR)
			num++;
		/*blkno = (req.blkno)%HDD_MAX_PAGES;		
		num = req.bcount;*/


		for(j = 0;j < num;j++){
			int hit_position = -1;
			wow_wss_refresh(blkno+j, req.flags);			
			rc_total++;
			if(req.flags == READ){
				rc_read++;

				if(mlru_search(W_HIT_TRACKER, tracker_num, blkno+j, 0, 0, &hit_position)){
					update_hit_tracker(W_HIT_TRACKER, tracker_num, blkno+j, req.flags);
				}else{
					update_hit_tracker(R_HIT_TRACKER, tracker_num, blkno+j, req.flags);
				}



			}else{
				struct listnode *node;
				rc_write++;

				node = mlru_search(R_HIT_TRACKER, tracker_num, blkno+j, 0, 0, &hit_position);
				if(node){
					mlru_remove(R_HIT_TRACKER, tracker_num, blkno+j);
				}
				update_hit_tracker(W_HIT_TRACKER, tracker_num, blkno+j, req.flags);
			}

			update_hit_tracker(HIT_TRACKER, tracker_num, blkno+j, req.flags);
		}


		if(i % 50000 == 0){
			fprintf(stdout, "\n Read %d lines ... \n", i);
			fflush(stdout);
		}


		count = num / (MAX_REQ_BYTES/PAGE);
		if(num % (MAX_REQ_BYTES/PAGE)){
			count++;
		}
	}
#endif 

	//Write
	theta = 0.1;
	zipf_main(W_HIT_TRACKER, tracker_num, tracker_num * 1,  (float)theta);
	 
	for(i=0; i < tracker_num; i++){
	   W_HIT_TRACKER[i]->cm_read_hit = (double)W_HIT_TRACKER[i]->cm_hit*0.1;     
	   //printf(" p[%d] = %f cum[%d] = %f \n", i, zdist[i].prob, i, zdist[i].cum_prob);
   }


	//Read 
	theta = 0.3;
	zipf_main(R_HIT_TRACKER, tracker_num, tracker_num * 1.5, (float)theta);

	printf_readonly_cost_table(W_HIT_TRACKER, R_HIT_TRACKER, tracker_num);
	printf_writeonly_cost_table(W_HIT_TRACKER, R_HIT_TRACKER, tracker_num);
	rc_read = 60;
	rc_write = 40;
	rc_total = rc_read + rc_write;
	printf_cost_table(W_HIT_TRACKER, R_HIT_TRACKER, tracker_num);

	
	

	//fprintf(outputfile, "\n Tracked Hit Ratio Curve ... \n");
	//print_hit_ratio_curve(HIT_TRACKER, HIT_TRACKER[0]->cm_ref, tracker_num, outputfile);
	fprintf(outputfile, "\n Read Tracked Hit Ratio Curve ... \n");
	print_hit_ratio_curve(R_HIT_TRACKER, R_HIT_TRACKER[0]->cm_ref, tracker_num, outputfile);
	fprintf(outputfile, "\n Write Tracked Hit Ratio Curve ... \n");
	print_hit_ratio_curve(W_HIT_TRACKER, W_HIT_TRACKER[0]->cm_ref, tracker_num, outputfile);

	mlru_exit(R_HIT_TRACKER, tracker_num);
	mlru_exit(W_HIT_TRACKER, tracker_num);
	mlru_exit(HIT_TRACKER, tracker_num);
	fclose(tracefile);


	return 0;
}

int ssd_simulation(char *filename,int cache_size,int hdd_max, int ssd_max, ssd_t *ssd){
	//struct cache_manager *flash_cache;

	FILE *tracefile= NULL;
	ioreq_event req;	
	int i;


	SSD_MAX_PAGES = ssd_max;
	HDD_MAX_PAGES = hdd_max;

	anti_total_cost = 0.0;
#ifdef __linux__
	tracefile = fopen64(filename, "r");
#else
	tracefile = fopen(filename, "r");
#endif 
	if(tracefile == NULL){
		fprintf(stderr," Cannot open file %s\n", filename);
		exit(1);
	}
	S_TOTAL = cache_size;
	

	lru_init(&dram_cache, "DRAM_CACHE", DRAM_MAX_PAGES, DRAM_MAX_PAGES, 1, 0);

	reverse_map_create(cache_size);

	
	//init 
	for(i = 0;i < S_TOTAL;i++){
		CACHE_MAKERQ(dram_cache, dram_cache->cm_ssdwq, NULL, i);
		lru_ssd_write(dram_cache, SSD, WRITE, dram_cache->cm_ssdwq, 0);
		//if(i % (S_TOTAL/10) == 0){
		//	fprintf(stdout, " Progress %d\n", i*100/(S_TOTAL));
		//}
	}

	//for(i = 0;i < S_TOTAL;i++){
	//	CACHE_MAKERQ(dram_cache, dram_cache->cm_ssdrq, NULL, i);
	//	lru_ssd_read(dram_cache, SSD, READ, dram_cache->cm_ssdrq, 0);
		//if(i % (S_TOTAL/10) == 0){
		//	fprintf(stdout, " Progress %d\n", i*100/(S_TOTAL));
		//}
	//}
	fprintf(stdout, " Init SSD \n");
	fprintf(outputfile, " Init SSD \n");
	fflush(stdout);
	fflush(outputfile);

	now = 0.0;
	SSD_WRITE_TIME = 0.0;
	for (i = 0; i < ssd->params.nelements; i ++) {
		ssd_element_stat *stat = &(ssd->elements[i].stat);
		stat->num_clean = 0;
	}


	for(i = 0; ;i++){		
		int blkno;
		int num, j;
		int seq_io = 0;
		SysTime prev_now = now;

		if(iotrace_ascii_get_ioreq_event(tracefile, &req) == NULL){
			break;
		}

		blkno = (req.blkno/BLOCK2SECTOR);
		num = req.bcount/BLOCK2SECTOR;
		if(req.bcount%BLOCK2SECTOR)
			num++;

		if(num >= SEQ_COUNT)
			seq_io = 1;

		for(j = 0;j < num;j++){
			int dram_hit = 0;
			int b = (blkno+ j) % S_TOTAL;

			//wow_wss_refresh(b, req.flags);

			dram_hit = search_dram_cache(dram_cache, b, req.flags);

			
			if(req.flags == READ){
				if(!dram_hit){
					CACHE_MAKERQ(dram_cache, dram_cache->cm_ssdrq, NULL, b);
				}
			}else{
				CACHE_MAKERQ(dram_cache, dram_cache->cm_ssdwq, NULL, b);
			}
				
		}

		if(req.flags == READ){			
			/* Staging */			
			lru_stage(dram_cache);
			/* SSD Read */
			//lru_flush_reqlist(dram_cache, SSD, READ, dram_cache->cm_ssdrq, 0);
			lru_ssd_read(dram_cache, SSD, READ, dram_cache->cm_ssdrq, 0);
		}else{			
			/* Destaging */			
			lru_destage(dram_cache);
			/* SSD Write */
			//lru_flush_reqlist(dram_cache, SSD, WRITE, dram_cache->cm_ssdwq, 0);
			lru_ssd_write(dram_cache, SSD, WRITE, dram_cache->cm_ssdwq, 0);
		}

		if(i % 50000 == 0){
			fprintf(stdout, "\n Read %d lines ... \n", i);
			fflush(stdout);
		}

		stat_update (&currssd->stat.responsestats,(double)(now - prev_now));
	//wow_response_tracking((double)(now - prev_now), 0.0, 0.0, 0.0, 0.0, 0.0, 2, 1, 1, 0.0, 0.0, 0.0);

		//fprintf(stdout, " Avg I/O %.2fms, whit = %.2f, rhit = %.2f\r", anti_total_cost/i, CACHE_HIT(flash_cache));
	}

	fprintf(stdout, " %d Total Cost = %.2fsec, avg Cost = %.2fus\n", cache_size, anti_total_cost/1000000, anti_total_cost/i);

	//print_statistic_information(stdout);
	//print_statistic_information(outputfile);

	fprintf(outputfile, " Syssim : Execution Time =	%f\n", now);
	//fprintf(outputfile, " Syssim : Flash Hit Ratio =	%f\n", CACHE_HIT(dram_cache));
	fprintf(outputfile, " Syssim : Dram Hit Ratio =	%f\n", CACHE_HIT(dram_cache));
	fprintf(outputfile, " Syssim : Stage Count =	%d\n", dram_cache->cm_stage_count);
	fprintf(outputfile, " Syssim : Destage Count =	%d\n", dram_cache->cm_destage_count);
	fprintf(outputfile, " Syssim : SSD Read Time	=	%f\n",  SSD_READ_TIME);
	fprintf(outputfile, " Syssim : SSD Write Time	=	%f\n",  SSD_WRITE_TIME);
	fprintf(outputfile, " Syssim : Stage Time	=	%f\n",  STAGE_TIME);
	fprintf(outputfile, " Syssim : Destage Time	=	%f\n",  DESTAGE_TIME);
	fprintf(outputfile, " Syssim : Disk Read	=	%.3f\n", HDD_TOTALCOST_R/HDD_COUNT_R);
	fprintf(outputfile, " Syssim : Disk Write	=	%.3f\n", HDD_TOTALCOST_W/HDD_COUNT_W);
	fprintf(outputfile, " Syssim : SSD Read	=	%.3f\n", SSD_TOTALCOST_R/SSD_COUNT_R);
	fprintf(outputfile, " Syssim : SSD Write	=	%.3f\n", SSD_TOTALCOST_W/SSD_COUNT_W);



	//CACHE_CLOSE(flash_cache);
	reverse_map_free();
	fclose(tracefile);
	CACHE_CLOSE(dram_cache);

	return 0;
}


int disk_simulation(char *filename,int cache_size,int hdd_max, int ssd_max, int bglru){
	//struct cache_manager *flash_cache;

	FILE *tracefile= NULL;
	ioreq_event req;	
	int i;


	SSD_MAX_PAGES = ssd_max;
	HDD_MAX_PAGES = hdd_max;

	anti_total_cost = 0.0;
#ifdef __linux__
	tracefile = fopen64(filename, "r");
#else
	tracefile = fopen(filename, "r");
#endif 
	if(tracefile == NULL){
		fprintf(stderr," Cannot open file %s\n", filename);
		exit(1);
	}
	S_TOTAL = cache_size;
	//if(!bglru){
	//	lru_init(&flash_cache,"LRU", S_TOTAL-1, SSD_MAX_PAGES, 1024, 0);
	//	flash_cache->cm_policy = CACHE_LRU_RW;
	//}else{
	//	bgclock_init(&flash_cache,"BGCLOCK", S_TOTAL-1, SSD_MAX_PAGES-1, 1024, 0);	
	//	W_CLOCK->cm_policy == CACHE_BGCLOCK_RW;
	//}

	lru_init(&dram_cache, "DRAM_CACHE", DRAM_MAX_PAGES, DRAM_MAX_PAGES, 1, 0);

	reverse_map_create(cache_size);

	for(i = 0; ;i++){		
		int blkno;
		int num, j;
		int seq_io = 0;
		SysTime prev_now = now;

		if(iotrace_ascii_get_ioreq_event(tracefile, &req) == NULL){
			break;
		}

		blkno = (req.blkno/BLOCK2SECTOR)%HDD_MAX_PAGES;
		num = req.bcount/BLOCK2SECTOR;
		if(req.bcount%BLOCK2SECTOR)
			num++;

		if(num >= SEQ_COUNT)
			seq_io = 1;

		for(j = 0;j < num;j++){
			int dram_hit = 0;

			//wow_wss_refresh(blkno+j, req.flags);

			dram_hit = search_dram_cache(dram_cache, blkno + j, req.flags);
			
		
			if(req.flags == READ){
				if(!dram_hit){
					CACHE_MAKERQ(dram_cache, dram_cache->cm_hddrq, NULL, blkno + j);
				}
			}else{
				CACHE_MAKERQ(dram_cache, dram_cache->cm_hddwq, NULL, blkno + j);
			}
			
		}

		if(req.flags == READ){			
			/* Staging */			
			lru_stage(dram_cache);
			/* SSD Read */
			//lru_flush_reqlist(dram_cache, SSD, READ, dram_cache->cm_ssdrq, 0);
			lru_ssd_read(dram_cache, SSD, READ, dram_cache->cm_ssdrq, 0);
		}else{			
			/* Destaging */			
			lru_destage(dram_cache);
			/* SSD Write */
			//lru_flush_reqlist(dram_cache, SSD, WRITE, dram_cache->cm_ssdwq, 0);
			lru_ssd_write(dram_cache, SSD, WRITE, dram_cache->cm_ssdwq, 0);
		}

		if(i % 50000 == 0){
			fprintf(stdout, "\n Read %d lines ... \n", i);
			fflush(stdout);
		}
		stat_update (&currssd->stat.responsestats, (double)(now - prev_now));
//		wow_response_tracking((double)(now - prev_now), 0.0,0.0,0.0, 0.0,0.0, 0,0,0, 0, 0, 0);
		//fprintf(stdout, " Avg I/O %.2fms, whit = %.2f, rhit = %.2f\r", anti_total_cost/i, CACHE_HIT(flash_cache));
	}

	fprintf(stdout, " %d Total Cost = %.2fsec, avg Cost = %.2fus\n", cache_size, anti_total_cost/1000000, anti_total_cost/i);

	//print_statistic_information(stdout);
	//print_statistic_information(outputfile);

	fprintf(outputfile, " Syssim : Execution Time =	%f\n", now);
	fprintf(outputfile, " Syssim : Flash Hit Ratio =	%f\n", CACHE_HIT(dram_cache));
	fprintf(outputfile, " Syssim : Dram Hit Ratio =	%f\n", CACHE_HIT(dram_cache));
	fprintf(outputfile, " Syssim : Stage Count =	%d\n", dram_cache->cm_stage_count);
	fprintf(outputfile, " Syssim : Destage Count =	%d\n", dram_cache->cm_destage_count);
	fprintf(outputfile, " Syssim : SSD Read Time	=	%f\n",  SSD_READ_TIME);
	fprintf(outputfile, " Syssim : SSD Write Time	=	%f\n",  SSD_WRITE_TIME);
	fprintf(outputfile, " Syssim : Stage Time	=	%f\n",  STAGE_TIME);
	fprintf(outputfile, " Syssim : Destage Time	=	%f\n",  DESTAGE_TIME);
	fprintf(outputfile, " Syssim : Disk Read	=	%.3f\n", HDD_TOTALCOST_R/HDD_COUNT_R);
	fprintf(outputfile, " Syssim : Disk Write	=	%.3f\n", HDD_TOTALCOST_W/HDD_COUNT_W);
	fprintf(outputfile, " Syssim : SSD Read	=	%.3f\n", SSD_TOTALCOST_R/SSD_COUNT_R);
	fprintf(outputfile, " Syssim : SSD Write	=	%.3f\n", SSD_TOTALCOST_W/SSD_COUNT_W);



	//CACHE_CLOSE(flash_cache);
	reverse_map_free();
	fclose(tracefile);
	CACHE_CLOSE(dram_cache);

	return 0;
}
//
//static int iotrace_month_convert (char *monthstr, int year)
//{
//	if (strcmp(monthstr, "Jan") == 0) {
//		return(0);
//	} else if (strcmp(monthstr, "Feb") == 0) {
//		return(31);
//	} else if (strcmp(monthstr, "Mar") == 0) {
//		return((year % 4) ? 59 : 60);
//	} else if (strcmp(monthstr, "Apr") == 0) {
//		return((year % 4) ? 90 : 91);
//	} else if (strcmp(monthstr, "May") == 0) {
//		return((year % 4) ? 120 : 121);
//	} else if (strcmp(monthstr, "Jun") == 0) {
//		return((year % 4) ? 151 : 152);
//	} else if (strcmp(monthstr, "Jul") == 0) {
//		return((year % 4) ? 181 : 182);
//	} else if (strcmp(monthstr, "Aug") == 0) {
//		return((year % 4) ? 212 : 213);
//	} else if (strcmp(monthstr, "Sep") == 0) {
//		return((year % 4) ? 243 : 244);
//	} else if (strcmp(monthstr, "Oct") == 0) {
//		return((year % 4) ? 273 : 274);
//	} else if (strcmp(monthstr, "Nov") == 0) {
//		return((year % 4) ? 304 : 305);
//	} else if (strcmp(monthstr, "Dec") == 0) {
//		return((year % 4) ? 334 : 335);
//	}
//	assert(0);
//	return(-1);
//}
//
//static int iotrace_read_int32 (FILE *tracefile, int32_t *intP)
//{
//	int i;
//	intchar swapval;
//	intchar intcharval;
//
//	StaticAssert (sizeof(int) == 4);
//	if (fread(&intcharval.value, sizeof(int), 1, tracefile) != 1) {
//		return(-1);
//	}
//	if (disksim->endian != disksim->traceendian) {
//		for (i=0; i<sizeof(int); i++) {
//			swapval.byte[i] = intcharval.byte[(sizeof(int) - i - 1)];
//		}
//		/*
//		fprintf (outputfile, "intptr.value %x, swapval.value %x\n", intcharval.value, swapval.value);
//		*/
//		intcharval.value = swapval.value;
//	}
//	*intP = intcharval.value;
//	return(0);
//}
//
//
//static void iotrace_hpl_srt_convert_flags (ioreq_event *curr)
//{
//	int flags;
//
//	flags = curr->flags;
//	curr->flags = 0;
//	if (flags & HPL_READ) {
//		curr->flags |= READ;
//		hpreads++;
//	} else {
//		hpwrites++;
//	}
//	if (!(flags & HPL_ASYNC)) {
//		curr->flags |= TIME_CRITICAL;
//		if (curr->flags & READ) {
//			syncreads++;
//		} else {
//			syncwrites++;
//		}
//	}
//	if (flags & HPL_ASYNC) {
//		if (curr->flags & READ) {
//			curr->flags |= TIME_LIMITED;
//			asyncreads++;
//		} else {
//			asyncwrites++;
//		}
//	}
//}
//
//ioreq_event * iotrace_hpl_get_ioreq_event (FILE *tracefile, ioreq_event *new)
//{
//	int32_t size;
//	int32_t id;
//	int32_t sec;
//	int32_t usec;
//	int32_t val;
//	int32_t junkint;
//	unsigned int failure = 0;
//
//	while (TRUE) {
//		failure |= iotrace_read_int32(tracefile, &size);
//		failure |= iotrace_read_int32(tracefile, &id);
//		failure |= iotrace_read_int32(tracefile, &sec);
//		failure |= iotrace_read_int32(tracefile, &usec);
//		if (failure) {
//			addtoextraq((event *) new);
//			return(NULL);
//		}
//		if (((id >> 16) < 1) || ((id >> 16) > 4)) {
//			fprintf(stderr, "Error in trace format - id %x\n", id);
//			exit(1);
//		}
//		if (((id & 0xFFFF) != HPL_SHORTIO) && ((id & 0xFFFF) != HPL_SUSPECTIO)) {
//			fprintf(stderr, "Unexpected record type - %x\n", id);
//			exit(1);
//		}
//		new->time = (double) sec * (double) MILLI;
//		new->time += (double) usec / (double) MILLI;
//
//		if ((disksim->traceheader == FALSE) && (new->time == 0.0)) {
//			tracebasetime = simtime;
//		}
//
//		failure |= iotrace_read_int32(tracefile, &val);    /* traced request start time */
//		new->tempint1 = val;
//		failure |= iotrace_read_int32(tracefile, &val);    /* traced request stop time */
//		new->tempint2 = val;
//		new->tempint2 -= new->tempint1;
//		failure |= iotrace_read_int32(tracefile, &val);
//		new->bcount = val;
//		if (new->bcount & 0x000001FF) {
//			fprintf(stderr, "HPL request for non-512B multiple size: %d\n", new->bcount);
//			exit(1);
//		}
//		new->bcount = new->bcount >> 9;
//		failure |= iotrace_read_int32(tracefile, &val);
//		new->blkno = val;
//		failure |= iotrace_read_int32(tracefile, &val);
//		new->devno = (val >> 8) & 0xFF;
//		failure |= iotrace_read_int32(tracefile, &val);       /* drivertype */
//		failure |= iotrace_read_int32(tracefile, &val);       /* cylno */
//		/* for convenience and historical reasons, this cast is being allowed */
//		/* (the value is certain to be less than 32 sig bits, and will not be */
//		/* used as a pointer).                                                */
//		new->buf = (void *) val;
//		failure |= iotrace_read_int32(tracefile, &val);
//		new->flags = val;
//		iotrace_hpl_srt_convert_flags(new);
//		failure |= iotrace_read_int32(tracefile, &junkint);           /* info */
//		size -= 13 * sizeof(int32_t);
//		if ((id >> 16) == 4) {
//			failure |= iotrace_read_int32(tracefile, &val);  /* queuelen */
//			new->slotno = val;
//			size -= sizeof(int32_t);
//		}
//		if ((id & 0xFFFF) == HPL_SUSPECTIO) {
//			failure |= iotrace_read_int32(tracefile, &junkint);    /* susflags */
//			size -= sizeof(int32_t);
//		}
//		if (failure) {
//			addtoextraq((event *) new);
//			return(NULL);
//		}
//		if (size) {
//			fprintf(stderr, "Unmatched size for record - %d\n", size);
//			exit(1);
//		}
//		new->cause = 0;
//		new->opid = 0;
//		new->busno = 0;
//		if ((id & 0xFFFF) == HPL_SHORTIO) {
//			return(new);
//		}
//	}
//}
//
//
//void iotrace_hpl_srt_tracefile_start (char *tracedate)
//{
//	char crap[40];
//	char monthstr[40];
//	int day;
//	int hour;
//	int minute;
//	int second;
//	int year;

//	if (sscanf(tracedate, "%s\t= \"%s %s %d %d:%d:%d %d\";\n", crap, crap, monthstr, &day, &hour, &minute, &second, &year) != 8) {
//		fprintf(stderr, "Format problem with 'tracedate' line in HPL trace - %s\n", tracedate);
//	//	exit(1);
//		return;
//	}
//	if (baseyear == 0) {
//		baseyear = year;
//	}
//	day = day + iotrace_month_convert(monthstr, year);
//	if (year != baseyear) {
//		day += (baseyear % 4) ? 365 : 366;
//	}
//	if (baseday == 0) {
//		baseday = day;
//	}
//	second = second + (60 * minute) + (3600 * hour) + (86400 * (day - baseday));
//	if (basesecond == 0) {
//		basesecond = second;
//	}
//	second -= basesecond;
//	tracebasetime += (double) 1000 * (double) second;
//}
//
//
//void iotrace_hpl_initialize_file (FILE *tracefile, int print_tracefile_header)
//{
//	char letter = '0';
//	char line[201];
//	char linetype[40];
//
//	//if (disksim->traceheader == FALSE) {
//	//	return;
//	//}
//	while (1) {
//		if (fgets(line, 200, tracefile) == NULL) {
//			fprintf(stderr, "No 'tracedate' line in HPL trace\n");
//			exit(1);
//		}
//		sscanf(line, "%s", linetype);
//		if (strcmp(linetype, "tracedate") == 0) {
//			break;
//		}
//	}
//	iotrace_hpl_srt_tracefile_start(line);
//	while (letter != 0x0C) {
//		if (fscanf(tracefile, "%c", &letter) != 1) {
//			fprintf(stderr, "End of header information never found - end of file\n");
//			exit(1);
//		}
//		if ((print_tracefile_header) && (letter != 0x0C)) {
//			printf("%c", letter);
//		}
//	}
//}


int rw_simulation(char *filename,int cache_size,int hdd_max, int ssd_max, int bglru){
	struct cache_manager *flash_cache;
	
	FILE *tracefile= NULL;
	ioreq_event req;
	int i;
	int inc = 32768;

	SSD_MAX_PAGES = ssd_max;
	HDD_MAX_PAGES = hdd_max;

	anti_total_cost = 0.0;
#ifdef __linux__
	tracefile = fopen64(filename, "r");
#else
	tracefile = fopen(filename, "r");
#endif 
	
	if(tracefile == NULL){
		fprintf(stderr," Cannot open file %s\n", filename);
		exit(1);
	}


	SSD_USABLE_PAGES = S_TOTAL = cache_size-1;

	
	if(S_TOTAL+inc > SSD_MAX_PAGES){
		inc = 8192;
		SSD_USABLE_PAGES -= inc;
		S_TOTAL -= inc;
		Wow_Max_High_Water -= inc;		
	}else{
		inc = 0;
	}

	reverse_map_create(S_TOTAL+inc);
	printf(" Max = %d, Spare Pages = %d \n", SSD_USABLE_PAGES, inc);
	


	
	
	if(!bglru){
		lru_init(&flash_cache,"LRU", S_TOTAL-1, SSD_MAX_PAGES, 1024, 0);
		flash_cache->cm_policy = CACHE_LRU_RW;
	}else{
		bgclock_init(&flash_cache,"BGCLOCK", S_TOTAL-1, SSD_MAX_PAGES-1, 1024, 0);	
		W_CLOCK->cm_policy == CACHE_BGCLOCK_RW;
	}
		
	lru_init(&dram_cache, "DRAM_CACHE", DRAM_MAX_PAGES, DRAM_MAX_PAGES, 1, 0);

	//if(adj_overprov)
	//	reverse_map_create(cache_size);
	//else
	

	
	///reverse_map_create(S_TOTAL);




	for(i = 0;;i++){		
		int blkno;
		int num, j;
		int seq_io = 0;
		SysTime prev_now = now;
		SysTime prev_stage = STAGE_TIME;
		SysTime prev_destage = DESTAGE_TIME;
		SysTime prev_ssd_read = SSD_READ_TIME;
		SysTime prev_ssd_write = SSD_WRITE_TIME;
		SysTime prev_gc0 = currssd->elements[0].stat.tot_clean_time;//	= SSD_GC_TIME;
		//SysTime prev_gc1 = currssd->elements[1].stat.tot_clean_time;
		SysTime curr_gc0;
		//SysTime curr_gc1;


		if(iotrace_ascii_get_ioreq_event(tracefile, &req) == NULL){
			break;
		}
		
		//if(i >= 100)
		//	break;
		blkno = (req.blkno/BLOCK2SECTOR)%HDD_MAX_PAGES;
		num = req.bcount/BLOCK2SECTOR;
		if(req.bcount%BLOCK2SECTOR)
			num++;

		if(num >= SEQ_COUNT)
			seq_io = 1;

		for(j = 0;j < num;j++){
			int dram_hit = 0;
			
			//wow_wss_refresh(blkno+j, req.flags);

			dram_hit = search_dram_cache(dram_cache, blkno + j, req.flags);
			
			if(!bglru){				
				rw_make_req(flash_cache, blkno + j, req.flags, dram_hit, seq_io);
			}else{
				gen_req_with_bgclock(flash_cache, &req, blkno + j, dram_hit, seq_io);
			}			
		}

		if(req.flags == READ){			
			FCM_REQ_FLAG = WRITE;
			/* Staging */			
			lru_stage(flash_cache);
			/* SSD Read */
			//lru_flush_reqlist(flash_cache, SSD, READ, flash_cache->cm_ssdrq, 0);
			lru_ssd_read(flash_cache, SSD, READ, flash_cache->cm_ssdrq, 0);
		}else{
			FCM_REQ_FLAG = WRITE;
			/* Destaging */			
			lru_destage(flash_cache);
			/* SSD Write */
			//lru_flush_reqlist(flash_cache, SSD, WRITE, flash_cache->cm_ssdwq, 0);
			lru_ssd_write(flash_cache, SSD, WRITE, flash_cache->cm_ssdwq, 0);
		}
		
			

		if(i && i % 50000 == 0){
			fprintf(stdout, "\n FP-FCL: Cur Read %d lines hit = %.2f(%.2fMB) res = %.3fms... \n",
					i, CACHE_HIT(flash_cache), (double)flash_cache->cm_count/256,
					(double)currssd->stat.responsestats.runval/currssd->stat.responsestats.count);
			fprintf(stdout, " Actual Pages = %d, Ideal Pages = %d\n", flash_cache->cm_count, cache_size);
				fflush(stdout);
		}

		stat_update (&currssd->stat.responsestats,(double)(now - prev_now));
		
		curr_gc0 = currssd->elements[0].stat.tot_clean_time;//	= SSD_GC_TIME;
//		curr_gc1 = currssd->elements[1].stat.tot_clean_time;

//		if((curr_gc0 - prev_gc0) < (curr_gc1 - prev_gc1)){
//			curr_gc0 = curr_gc1;
//			prev_gc0 = prev_gc1;
//		}


		if(0 && flash_cache->cm_ref){
			wow_response_tracking(
				now,
				(double)(now - prev_now),
				(double)(STAGE_TIME - prev_stage),
				(double)(DESTAGE_TIME - prev_destage),
				(double)(SSD_READ_TIME - prev_ssd_read),
				(double)(SSD_WRITE_TIME - prev_ssd_write),
				(double)(curr_gc0 - prev_gc0),
				SSD_MAX_PAGES,
				flash_cache->cm_count,
				flash_cache->cm_count,
				S_TOTAL,
				S_TOTAL,
				flash_cache->cm_hit*100/flash_cache->cm_ref,
				0.0,
				(double)currssd->stat.responsestats.runval/currssd->stat.responsestats.count
				);
		}
		
		

		//fprintf(stdout, " Avg I/O %.2fms, whit = %.2f, rhit = %.2f\r", anti_total_cost/i, CACHE_HIT(flash_cache));
	}

	fprintf(stdout, " %d Total Cost = %.2fsec, avg Cost = %.2fus\n", cache_size, anti_total_cost/1000000, anti_total_cost/i);

	//print_statistic_information(stdout);
	//print_statistic_information(outputfile);

	fprintf(outputfile, " Syssim : Execution Time =	%f\n", now);
	//fprintf(outputfile, " Syssim : Flash Hit Ratio =	%f\n", CACHE_HIT(flash_cache));
	fprintf(outputfile, " Syssim : Total Hit Ratio =	%.3f\n", CACHE_HIT(flash_cache));
	fprintf(outputfile, " Syssim : Dram Hit Ratio =	%f\n", CACHE_HIT(dram_cache));
	fprintf(outputfile, " Syssim : Stage Count =	%d\n", flash_cache->cm_stage_count);
	fprintf(outputfile, " Syssim : Destage Count =	%d\n", flash_cache->cm_destage_count);
	fprintf(outputfile, " Syssim : SSD Read Time	=	%f\n",  SSD_READ_TIME);
	fprintf(outputfile, " Syssim : SSD Write Time	=	%f\n",  SSD_WRITE_TIME);
	fprintf(outputfile, " Syssim : Stage Time	=	%f\n",  STAGE_TIME);
	fprintf(outputfile, " Syssim : Destage Time	=	%f\n",  DESTAGE_TIME);
	fprintf(outputfile, " Syssim : Disk Read	=	%.3f\n", HDD_TOTALCOST_R/HDD_COUNT_R);
	fprintf(outputfile, " Syssim : Disk Write	=	%.3f\n", HDD_TOTALCOST_W/HDD_COUNT_W);
	fprintf(outputfile, " Syssim : SSD Read	=	%.3f\n", SSD_TOTALCOST_R/SSD_COUNT_R);
	fprintf(outputfile, " Syssim : SSD Write	=	%.3f\n", SSD_TOTALCOST_W/SSD_COUNT_W);
	
	//fprintf(outputfile, " Syssim: Total Request Count(read in writelru)	=	%d	(%.3fMB)\n", rc_read_in_write, (double)rc_read/256);
	//fprintf(outputfile, " Syssim: Total Request Count(write in readlru)	=	%d	(%.3fMB)\n", rc_write_in_read, (double)rc_write/256);
		
	
	
	CACHE_CLOSE(flash_cache);
	reverse_map_free();
	fclose(tracefile);
	CACHE_CLOSE(dram_cache);

	return 0;
}


void print_dirty_rate(struct cache_manager *c, int r_size, int w_size, FILE *fp){
	listnode *node = c->cm_head->next;
	struct lru_node *ln;
	int i;
	int total = 0;
	int read_count = 0;
	int write_count = 0;
	int clean_size = 0;
	int dirty_size = 0;
	int total_size = r_size + w_size;

	while(node != c->cm_head && node){		
		ln = (struct lru_node *)node->data;
		
		total++;
		if(ln->cn_read)
			read_count++;
		if(ln->cn_dirty)
			write_count++;
		
		node = node->next;
	}

	dirty_size = (double)w_size * (((double)write_count-read_count)/total);
	clean_size = (double)w_size * ((double)read_count/total);
	
	fprintf(fp, " Syssim: Read Cache Rate = %.2f (%.3fMB)\n", (double)r_size/SSD_MAX_PAGES, (double)r_size/256);
	fprintf(fp, " Syssim: Write Cache Rate = %.2f (%.3fMB)\n", (double)dirty_size/SSD_MAX_PAGES, (double)dirty_size/256);
	fprintf(fp, " Syssim: RW Cache Rate = %.2f (%.3fMB)\n",   (double)clean_size/SSD_MAX_PAGES, (double)clean_size/256);
	fprintf(fp, " Syssim: Over Cache Rate = %.2f (%.3fMB)\n",  (double)(SSD_MAX_PAGES-total_size)/SSD_MAX_PAGES, (double)(SSD_MAX_PAGES-total_size)/256);

	fprintf(fp, " LRU clean rate = %f\n", (double)read_count/total);
	fprintf(fp, " LRU dirty rate = %f\n", (double)write_count/total);
	//fprintf(outputfile, " LRU clean rate = %f\n", (double)read_count/total);
	//fprintf(outputfile, " LRU dirty rate = %f\n", (double)write_count/total);

	


}


int adaptive_simulation(char *filename,int tracker_num, int cache_size,int HDD_MAX, int SSD_MAX,int bglru, int adj_overprov, int selective){
	listnode *node;	
	struct clock_node *cn;
	FILE *tracefile= NULL;
	ioreq_event req;	
	int i;
	int adj_count = 0;
	int seq_detected = 0;
	int seq_detected_count = 0;
	int seq_detected_count2 = 0;
	void *temp[4];
	int testblk = 0;
	anti_total_cost = 0.0;

	SSD_MAX_PAGES = SSD_MAX;
	HDD_MAX_PAGES = HDD_MAX;
#ifndef __linux__
//	HDD_MAX_PAGES = 35913728;
	Wow_HDD_Max_Pages = HDD_MAX_PAGES;
#endif 
	//cache_size--;
	SSD_USABLE_PAGES = cache_size-2;

	//printf(" U inc = %d\n", U_INC);


	if(adj_overprov)
		reverse_map_create(cache_size);
	else{
		int inc = 32768;
		if(cache_size+inc > SSD_MAX_PAGES){
			inc = -(4096+2048);
			SSD_USABLE_PAGES += inc;
			Wow_Max_High_Water += inc;
		}
		reverse_map_create(cache_size+inc);
		

		printf(" Max = %d, Spare Pages = %d \n", SSD_USABLE_PAGES, inc);
	}


	//S_TOTAL = cache_size;
	S_TOTAL = SSD_USABLE_PAGES;
	R_VALUE = (double)50;
	calc_rw_partition_size(S_TOTAL, R_VALUE, &S_READ, &S_WRITE, 0, 0);

	lru_init(&R_CLOCK,"R_CLOCK", S_READ-1, SSD_MAX_PAGES-1, 1, 0);
	if(bglru){				
		bgclock_init(&W_CLOCK,"W_CLOCK", S_WRITE-1, SSD_MAX_PAGES-1, 1024, 0);
		R_CLOCK->cm_policy = W_CLOCK->cm_policy = CACHE_BGCLOCK_RWO_ADAPTIVE;			
	}else{
		lru_init(&W_CLOCK,"W_CLOCK", S_WRITE-1, SSD_MAX_PAGES-1, 1024, 0);
		if(adj_overprov)
			R_CLOCK->cm_policy = W_CLOCK->cm_policy = CACHE_LRU_RWO_ADAPTIVE;
		else
			R_CLOCK->cm_policy = W_CLOCK->cm_policy = CACHE_LRU_RW_ADAPTIVE;
	}

	//print_dirty_rate(W_CLOCK);
	
	lru_init(&dram_cache, "DRAM_CACHE", DRAM_MAX_PAGES, DRAM_MAX_PAGES, 1, 0);
	seq_detect_init(&seq_detector,"SEQ_DETECTOR", SEQ_COUNT, SEQ_COUNT, 1, 0);
	
	
	if(!RESIZE_PERIOD){
		RESIZE_PERIOD = SSD_MAX_PAGES/tracker_num;
	}	
	HIT_TRACKER_NUM = tracker_num;
	//HIT_TRACKER_NUM *= 4;
	if(adj_overprov){
		W_HIT_TRACKER = (struct cache_manager **)mlru_init("W_HIT_TRACKER", HIT_TRACKER_NUM, SSD_MAX_PAGES);
		R_HIT_TRACKER = (struct cache_manager **)mlru_init("R_HIT_TRACKER", HIT_TRACKER_NUM, SSD_MAX_PAGES);
	}else{
		W_HIT_TRACKER = (struct cache_manager **)mlru_init("W_HIT_TRACKER", HIT_TRACKER_NUM, SSD_USABLE_PAGES);
		R_HIT_TRACKER = (struct cache_manager **)mlru_init("R_HIT_TRACKER", HIT_TRACKER_NUM, SSD_USABLE_PAGES);
	}
	
	
	U_INC = SSD_MAX_PAGES/tracker_num;
	R_P_INC = U_INC;
	fprintf(stdout, "Resize Period = %d \n", RESIZE_PERIOD);
	fprintf(stdout, "Hit Ratio Tracker Window Count = %d\n", HIT_TRACKER_NUM);
	fprintf(stdout, "U step = %d, R step = %d\n", U_INC, R_P_INC);

	fprintf(outputfile, "Resize Period = %d \n", RESIZE_PERIOD);
	fprintf(outputfile, "Hit Ratio Tracker Window Count = %d\n", HIT_TRACKER_NUM);
	

#ifdef __linux__
	tracefile = fopen64(filename, "r");
#else
	tracefile = fopen(filename, "r");
#endif 
	if(tracefile == NULL){
		fprintf(stderr," Cannot open file %s\n", filename);
		exit(1);
	}

	temp[0] = W_CLOCK->cm_ssdwq;
	temp[1] = W_CLOCK->cm_ssdrq;
	temp[2] = W_CLOCK->cm_hddwq;
	temp[3] = W_CLOCK->cm_hddrq;

	W_CLOCK->cm_ssdwq = R_CLOCK->cm_ssdwq;
	W_CLOCK->cm_ssdrq = R_CLOCK->cm_ssdrq;
	W_CLOCK->cm_hddwq = R_CLOCK->cm_hddwq;
	W_CLOCK->cm_hddrq = R_CLOCK->cm_hddrq;


	make_utable();
	
	for(i = 0;;i++){		
		int blkno;
		int num, j;
		int res;
		int seq_io = 0;
		SysTime prev_now = now;
		SysTime prev_stage = STAGE_TIME;
		SysTime prev_destage = DESTAGE_TIME;
		SysTime prev_ssd_read = SSD_READ_TIME;
		SysTime prev_ssd_write = SSD_WRITE_TIME;
		SysTime prev_gc0 = currssd->elements[0].stat.tot_clean_time;//	= SSD_GC_TIME;
		//SysTime prev_gc1 = currssd->elements[1].stat.tot_clean_time;
		SysTime curr_gc0;
		//SysTime curr_gc1;

		if(iotrace_ascii_get_ioreq_event(tracefile, &req) == NULL){
			break;
		}

		
		/* 2012 01 07 11:16 */		
		//if(req.flags & READ){
		//	if(W_CLOCK->cm_free < -1024){
		//		rw_replacement(W_CLOCK, 0);
		//		lru_destage(W_CLOCK);
		//	}
		//}
		
		blkno = (req.blkno/BLOCK2SECTOR)%HDD_MAX_PAGES;
		num = req.bcount/BLOCK2SECTOR;
		if(req.bcount%BLOCK2SECTOR)
			num++;

//#define SEQ_TEST
#ifdef SEQ_TEST  
		blkno = testblk++;
		num = 1;
#endif 	

		if(num >= SEQ_COUNT && SEQ_COUNT > 0){
			seq_io = 1;
			seq_detected_count++;
			//printf(" Sequential Detection = %fMB\n", (float)num/256);
		}else{
			seq_io = 0;
		}
			
		for(j = 0;j < num;j++){
			int dram_hit = 0;
			int bno = blkno+j;
			rc_total++;
			
			g_GHOST_HIT = 0;

			//wow_wss_refresh(bno, req.flags);

			if(adj_overprov){
//#ifdef SEQ_TEST  
				//seq_detected = seq_detect_make_req(seq_detector, R_CLOCK, W_CLOCK, bno, req.flags, req.time);
				//if(seq_detected)
				//	seq_detected_count2++;
//#endif 
				//seq_detected = 0;
			}

			if(!seq_detected){
				dram_hit = search_dram_cache(dram_cache, bno, req.flags);
				if(bglru)
					adaptive_gen_req_with_bgclock(R_CLOCK, W_CLOCK, &req, bno, dram_hit, seq_io);
				else
					adaptive_gen_req(R_CLOCK, W_CLOCK, &req, bno, dram_hit, seq_io, selective);
			}							

			g_GHOST_HIT = 0;

			if((rc_total/(unsigned int)RESIZE_PERIOD) == (adj_count+1) || (g_GHOST_HIT && !seq_io)){
				if(!g_GHOST_HIT)
					adj_count++;

				if(adj_overprov){
					adjust_afcm_cache_size(W_HIT_TRACKER, R_HIT_TRACKER, HIT_TRACKER_NUM, 1, 1);
				}else{
					adjust_fcmrw_cache_size(W_HIT_TRACKER, R_HIT_TRACKER, HIT_TRACKER_NUM, 1, 1);
				}
				resize_rw_cache_size(R_CLOCK, W_CLOCK);			
				
				if(adj_overprov){
					decay_hit_tracker(W_HIT_TRACKER, HIT_TRACKER_NUM);
					decay_hit_tracker(R_HIT_TRACKER, HIT_TRACKER_NUM);
				}

				if(adj_count % 2 == 0){
					//rc_total /= 2;//rc_write /= 2;//rc_read /= 2;//nr_serviced_rs /= 2;//nr_serviced_ws /= 2;					
				}
			}		
		}

		if(R_CLOCK->cm_count + W_CLOCK->cm_count != reverse_used){
			fprintf(stderr, " count error \n");
		}
	
		if(req.flags == READ){
			FCM_REQ_FLAG = READ;
			/* Staging */			
			lru_stage(R_CLOCK);
			/* SSD Read */
			//lru_flush_reqlist(R_CLOCK, SSD, READ, R_CLOCK->cm_ssdrq, 0);
			lru_ssd_read(R_CLOCK, SSD, READ, R_CLOCK->cm_ssdrq, 0);
		}else{
			FCM_REQ_FLAG = WRITE;
			/* Destaging */			
			lru_destage(W_CLOCK);
			/* SSD Write */
			//lru_flush_reqlist(W_CLOCK, SSD, WRITE, W_CLOCK->cm_ssdwq, 0);
			lru_ssd_write(W_CLOCK, SSD, WRITE, W_CLOCK->cm_ssdwq, 0);
		}


		
		/* 2012 01 09 11:19 */		
		//if(req.flags & READ){
		if(W_CLOCK->cm_free < 0){
			rw_replacement(W_CLOCK, 0);
			lru_destage(W_CLOCK);
		}
		

		if(R_CLOCK->cm_free < 0){
			rw_replacement(R_CLOCK, 0);
			//lru_stage(R_CLOCK);
		}


		
		if(i && i % 50000 == 0){
				if(adj_overprov){
					fprintf(stdout, "\n OP-FCL: Cur Read %d lines rhit = %.2f(%.2fMB), whit = %.2f(%.2fMB)... \n",
					i, CACHE_HIT(R_CLOCK), (double)R_CLOCK->cm_count/256, CACHE_HIT(W_CLOCK), (double)W_CLOCK->cm_count/256);
				}else{
					fprintf(stdout, "\n RW-FCL: Cur Read %d lines rhit = %.2f(%.2fMB), whit = %.2f(%.2fMB) %d... \n",
					i, CACHE_HIT(R_CLOCK), (double)R_CLOCK->cm_count/256, CACHE_HIT(W_CLOCK), (double)W_CLOCK->cm_count/256, R_CLOCK->cm_count+W_CLOCK->cm_count);
				}

				fprintf(stdout, " Actual Pages = %d, Ideal Pages = %d\n", W_CLOCK->cm_count+R_CLOCK->cm_count, S_READ+S_WRITE);

				fprintf(stdout, " Rec Read %d lines rhit = %.2f(%.2fMB), whit = %.2f(%.2fMB)... \n",
					i, CACHE_HIT(R_CLOCK), (double)S_READ/256, CACHE_HIT(W_CLOCK), (double)S_WRITE/256);
				fprintf(stdout, " Total hit = %.3f, Read rate = %.3f, Write rate = %.3f \n", (double)(W_CLOCK->cm_hit+R_CLOCK->cm_hit)/rc_total, (double)rc_read/rc_total, (double)rc_write/rc_total);
				//fprintf(stdout, " Write rate = %.3f, WS Serv Rate = %.3f \n", (double)rc_write/rc_total, WS_SERVICE_RATE);				
				fprintf(stdout, " Response Time =%.3fms \n", (double)currssd->stat.responsestats.runval/currssd->stat.responsestats.count);
				fflush(stdout);

//				print_dirty_rate(W_CLOCK, R_CLOCK->cm_count, W_CLOCK->cm_count, stdout);

		}
		
		stat_update (&currssd->stat.responsestats,(double)(now - prev_now));

		curr_gc0 = currssd->elements[0].stat.tot_clean_time;//	= SSD_GC_TIME;
		//curr_gc1 = currssd->elements[1].stat.tot_clean_time;

		
		if(R_CLOCK->cm_ref && W_CLOCK->cm_ref && W_CLOCK->cm_policy == CACHE_LRU_RWO_ADAPTIVE){
			wow_response_tracking(now, (double)(now - prev_now),
				(double)(STAGE_TIME - prev_stage),
				(double)(DESTAGE_TIME - prev_destage),
				(double)(SSD_READ_TIME - prev_ssd_read),
				(double)(SSD_WRITE_TIME - prev_ssd_write),
				(double)(curr_gc0 - prev_gc0),
				SSD_MAX_PAGES,
				S_READ,
				S_WRITE,
				R_CLOCK->cm_count,
				W_CLOCK->cm_count,
				R_CLOCK->cm_hit*100/(R_CLOCK->cm_ref),
				W_CLOCK->cm_hit*100/(W_CLOCK->cm_ref),
				(double)currssd->stat.responsestats.runval/currssd->stat.responsestats.count

				);
		}		
	}

	fprintf(outputfile, " Syssim : Execution Time =	%f\n", now);
	fprintf(outputfile, " Syssim : Dram Hit Ratio =	%f\n", CACHE_HIT(dram_cache));
	fprintf(outputfile, " Syssim : Flash Hit Ratio RHit Ratio =	%f\n", CACHE_HIT(R_CLOCK));
	fprintf(outputfile, " Syssim : Flash Hit Ratio WHit Ratio =	%f\n", CACHE_HIT(W_CLOCK));
	fprintf(outputfile, " Syssim : Total Hit Ratio =	%.3f\n", (double)(R_CLOCK->cm_hit + W_CLOCK->cm_hit)/rc_total);
	//fprintf(outputfile, " Syssim : Flash Hit Ratio =	%f\n", CACHE_HIT(R_CLOCK) * READ_REQ_RATE + 
														//CACHE_HIT(W_CLOCK) * (1 - READ_REQ_RATE));

	fprintf(outputfile, " Syssim : Stage Count =	%d\n", R_CLOCK->cm_stage_count + W_CLOCK->cm_stage_count);
	fprintf(outputfile, " Syssim : Destage Count =	%d\n", R_CLOCK->cm_destage_count +W_CLOCK->cm_destage_count);
	fprintf(outputfile, " Syssim : SSD Read Time	=	%f\n",  SSD_READ_TIME);
	fprintf(outputfile, " Syssim : SSD Write Time	=	%f\n",  SSD_WRITE_TIME);
	fprintf(outputfile, " Syssim : Stage Time	=	%f\n",  STAGE_TIME);
	fprintf(outputfile, " Syssim : Stage Time SSD WRITE	=	%f\n",  STAGE_SSD_WRITE);
	fprintf(outputfile, " Syssim : Stage Time HDD_READ	=	%f\n",  STAGE_HDD_READ);
	fprintf(outputfile, " Syssim : Destage Time	=	%f\n",  DESTAGE_TIME);
	fprintf(outputfile, " Syssim : Destage Time SSD READ	=	%f\n",  DESTAGE_SSD_READ);
	fprintf(outputfile, " Syssim : Destage Time HDD WRITE	=	%f\n",  DESTAGE_HDD_WRITE);


	fprintf(outputfile, " Syssim : Utilization =	%f\n", (double)(R_CLOCK->cm_count+W_CLOCK->cm_count)/SSD_MAX_PAGES);
	fprintf(outputfile, " Syssim : Read Partition Ratio = %f\n", R_VALUE);
	fprintf(outputfile, " Syssim : Disk Read	=	%.3f\n", HDD_TOTALCOST_R/HDD_COUNT_R);
	fprintf(outputfile, " Syssim : Disk Write	=	%.3f\n", HDD_TOTALCOST_W/HDD_COUNT_W);
	fprintf(outputfile, " Syssim : SSD Read	=	%.3f\n", SSD_TOTALCOST_R/SSD_COUNT_R);
	fprintf(outputfile, " Syssim : SSD Write	=	%.3f\n", SSD_TOTALCOST_W/SSD_COUNT_W);
	fprintf(outputfile, " Syssim : SSD Read	(Predicted) =	%.3f\n", SSD_TOTALCOST_PREDICT_R/SSD_COUNT_PREDICT_R);
	fprintf(outputfile, " Syssim : SSD Write (Predicted)=	%.3f\n", SSD_TOTALCOST_PREDICT_W/SSD_COUNT_PREDICT_W);	

	fprintf(outputfile, " Syssim : Read Req Ratio	= %f\n", (double)rc_read/rc_total);
	fprintf(outputfile, " Syssim : Write Req Ratio	= %f\n", (double)rc_write/rc_total);

	fprintf(outputfile, " Syssim : I/O rate serviced in Read Space = %f\n", RS_SERVICE_RATE);
	fprintf(outputfile, " Syssim : I/O rate serviced in Write Space = %f\n", WS_SERVICE_RATE);


	fprintf(outputfile, " Syssim : Seq detected count = %d\n", seq_detected_count);
	fprintf(outputfile, " Syssim : Seq detected count2 = %d\n", seq_detected_count2);
	fprintf(outputfile, " Syssim: Total Request Count	=	%d	(%.3fMB)\n", rc_total, (double)rc_total/256);
	fprintf(outputfile, " Syssim: Total Request Count(read)	=	%d	(%.3fMB)\n", rc_read, (double)rc_read/256);
	fprintf(outputfile, " Syssim: Total Request Count(write)	=	%d	(%.3fMB)\n", rc_write, (double)rc_write/256);
	fprintf(outputfile, " Syssim: Total Request Count(read in writelru)	=	%d	(%.3fMB) %f\n", rc_read_in_write, (double)rc_read_in_write/256, (double)rc_read_in_write/rc_read);
	fprintf(outputfile, " Syssim: Total Request Count(write in readlru)	=	%d	(%.3fMB) %f\n", rc_write_in_read, (double)rc_write_in_read/256, (double)rc_write_in_read/rc_write);
	fprintf(outputfile, " Syssim: *Ideal utilization = %.2f\n", (double)S_TOTAL/SSD_MAX_PAGES);
	//fprintf(outputfile, " Syssim: Read Cache Rate = %.2f (%.3fMB)\n", (double)S_READ/SSD_MAX_PAGES, (double)S_READ/256);
	//fprintf(outputfile, " Syssim: Write Cache Rate = %.2f (%.3fMB)\n", (double)S_WRITE/SSD_MAX_PAGES, (double)S_WRITE/256);



	W_CLOCK->cm_ssdwq = temp[0];
	W_CLOCK->cm_ssdrq= temp[1];
	W_CLOCK->cm_hddwq = temp[2];
	W_CLOCK->cm_hddrq = temp[3];


	mlru_exit(W_HIT_TRACKER, HIT_TRACKER_NUM);
	mlru_exit(R_HIT_TRACKER, HIT_TRACKER_NUM);
	
	//print_dirty_rate(R_CLOCK);
	print_dirty_rate(W_CLOCK, R_CLOCK->cm_count, W_CLOCK->cm_count, outputfile);
	print_dirty_rate(W_CLOCK, R_CLOCK->cm_count, W_CLOCK->cm_count, stdout);

	fprintf(outputfile, "\n Read Tracked Hit Ratio Curve ... \n");
	print_hit_ratio_curve(R_HIT_TRACKER, R_HIT_TRACKER[0]->cm_ref, HIT_TRACKER_NUM, outputfile);
	fprintf(outputfile, "\n Write Tracked Hit Ratio Curve ... \n");
	print_hit_ratio_curve(W_HIT_TRACKER, W_HIT_TRACKER[0]->cm_ref, HIT_TRACKER_NUM, outputfile);

	CACHE_CLOSE(R_CLOCK);
	CACHE_CLOSE(W_CLOCK);
	CACHE_CLOSE(dram_cache);
	CACHE_CLOSE(seq_detector);
	reverse_map_free();
	fclose(tracefile);

	return 0;
}



int wow_simulation(char *filename,int tracker_num, int cache_size,int HDD_MAX, int SSD_MAX){
	listnode *node;
	struct clock_node *cn;
	FILE *tracefile= NULL;
	ioreq_event req;	
	int i;	
	anti_total_cost = 0.0;


	printf(" Start WOW Simulation ... \n");
	fflush(stdout);

	SSD_MAX_PAGES = SSD_MAX;
	HDD_MAX_PAGES = HDD_MAX;

	S_TOTAL = cache_size;

	bgclock_init(&W_CLOCK,"W_CLOCK", S_TOTAL-1, SSD_MAX_PAGES-1, 1024, 0);	
	W_CLOCK->cm_policy == CACHE_BGCLOCK_RW;

	reverse_map_create(cache_size);

#ifdef __linux__
	tracefile = fopen64(filename, "r");
#else
	tracefile = fopen(filename, "r");
#endif 
	if(tracefile == NULL){
		fprintf(stderr," Cannot open file %s\n", filename);
		fprintf(outputfile," Cannot open file %s\n", filename);
		exit(1);
	}


	for(i = 0;;i++){		
		int blkno;
		int num, j;
		int res;
		int seq_io;
		if(iotrace_ascii_get_ioreq_event(tracefile, &req) == NULL){
			break;
		}

		blkno = (req.blkno/BLOCK2SECTOR)%HDD_MAX_PAGES;
		num = req.bcount/BLOCK2SECTOR;
		if(req.bcount%BLOCK2SECTOR)
			num++;

		if(num >= SEQ_COUNT)
			seq_io = 1;
		for(j = 0;j < num;j++){
			rc_total++;

			gen_req_with_bgclock(W_CLOCK, &req, blkno + j,0, seq_io);
		}

		if(W_CLOCK->cm_count != reverse_used){
			fprintf(stderr, " count error \n");
		}

		if(req.flags == READ){
			/* Staging */			
			lru_stage(W_CLOCK);
			/* SSD Read */
			//lru_flush_reqlist(W_CLOCK, SSD, READ, W_CLOCK->cm_ssdrq, 0);
			lru_ssd_read(W_CLOCK, SSD, READ, W_CLOCK->cm_ssdrq, 0);
		}else{
			/* Destaging */			
			lru_destage(W_CLOCK);
			/* SSD Write */
			//lru_flush_reqlist(W_CLOCK, SSD, WRITE, W_CLOCK->cm_ssdwq, 0);
			lru_ssd_write(W_CLOCK, SSD, WRITE, W_CLOCK->cm_ssdwq, 0);
		}
	}


	fprintf(outputfile, " Syssim : Execution Time =	%f\n", now);
	fprintf(outputfile, " Syssim : Hit Ratio =	%f\n", CACHE_HIT(W_CLOCK));
	fprintf(outputfile, " Syssim : Stage Count =	%d\n", W_CLOCK->cm_stage_count);
	fprintf(outputfile, " Syssim : Destage Count =	%d\n", W_CLOCK->cm_destage_count);
	fprintf(outputfile, " Syssim : Disk Read	=	%.3f\n", HDD_TOTALCOST_R/HDD_COUNT_R);
	fprintf(outputfile, " Syssim : Disk Write	=	%.3f\n", HDD_TOTALCOST_W/HDD_COUNT_W);
	fprintf(outputfile, " Syssim : SSD Read	=	%.3f\n", SSD_TOTALCOST_R/SSD_COUNT_R);
	fprintf(outputfile, " Syssim : SSD Write	=	%.3f\n", SSD_TOTALCOST_W/SSD_COUNT_W);

	CACHE_CLOSE(W_CLOCK);
	reverse_map_free();
	fclose(tracefile);

	return 0;
}


//
//static int issue_write_req(struct cache_manager *clock, struct cache_manager *ghost, int blkno){	
//	struct clock_node *cn = NULL;
//	struct ghost_node *gn;
//	listnode *node = NULL;		
//	int res = REQ_NORMAL;
//
//
//	node = CACHE_SEARCH(clock, blkno);
//	if(!node){
//		
//		node = CACHE_SEARCH(ghost, blkno);			
//		
//		if(node){ // hit in ghost cache						
//			res = REQ_GHIT;			
//			cn = CACHE_REMOVE(ghost, node);
//			cn->cn_frequency++;				
//		}
//
//		while(clock->cm_free < 1){			
//
//			gn = CACHE_REPLACE(clock);
//
//			while(ghost->cm_free < 1){					
//				CACHE_REPLACE(ghost);					
//			}
//
//			// insert evicted node to ghost cache
//			if(gn && gn->gn_blkno>= 0){					
//				CACHE_INSERT(ghost,(struct ghost *)gn);
//				anti_total_cost += SSD_READ;
//				anti_total_cost += HDD_COST;
//			}
//		}
//
//		cn = CACHE_ALLOC(clock, cn, blkno);
//		CACHE_INSERT(clock, cn);
//	}else{
//		cn = (struct clock_node *)node->data;
//		cn->cn_frequency++;
//		res = REQ_HIT;
//	}
//	
//	anti_total_cost += SSD_PW(ssd_predict_util((double)clock->cm_count/SSD_MAX_PAGES));
//
//	return res;
//}
//
//static int issue_read_req(struct cache_manager *clock, struct cache_manager *ghost, int blkno){	
//	struct clock_node *cn = NULL;
//	struct ghost_node *gn;
//	listnode *node = NULL;		
//	int res = REQ_NORMAL;
//
//
//	node = CACHE_SEARCH(clock, blkno);
//	if(!node){
//
//		node = CACHE_SEARCH(ghost, blkno);			
//
//		if(node){ // hit in ghost cache						
//			res = REQ_GHIT;			
//			cn = CACHE_REMOVE(ghost, node);
//			cn->cn_frequency++;				
//		}
//
//		while(clock->cm_free < 1){			
//
//			gn = CACHE_REPLACE(clock);
//
//			while(ghost->cm_free < 1){					
//				CACHE_REPLACE(ghost);					
//			}
//			
//			if(gn && gn->gn_blkno>= 0){					
//				CACHE_INSERT(ghost,(struct ghost *)gn);				
//			}
//		}
//
//		anti_total_cost += HDD_COST;
//		anti_total_cost += SSD_PW(ssd_predict_util((double)clock->cm_count/SSD_MAX_PAGES));
//
//		cn = CACHE_ALLOC(clock, cn, blkno);
//		CACHE_INSERT(clock, cn);
//	}else{
//		cn = (struct clock_node *)node->data;
//		cn->cn_frequency++;
//		res = REQ_HIT;
//	}
//
//	anti_total_cost += SSD_READ;
//
//	return res;
//}
