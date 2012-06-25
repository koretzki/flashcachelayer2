#include <stdio.h>
#include <stdlib.h>
#include "disksim_fcl_cache.h"
#include "../ssdmodel/ssd_utils.h"


void lru_open(struct cache_manager *c,int cache_size, int cache_max){
	int i;

	ll_create(&(c->cm_head));

	c->cm_hash = (listnode **)malloc(sizeof(listnode *) * HASH_NUM);
	if(c->cm_hash == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(1);
	}
	for(i = 0;i < HASH_NUM;i++){
		ll_create(&c->cm_hash[i]);
	}

	c->cm_destage_ptr = c->cm_head;
	c->cm_ref = 0;
	c->cm_hit = 0;
	c->cm_miss = 0;
	c->cm_size = cache_size;
	c->cm_free = cache_size;
	c->cm_count = 0;
	c->cm_max =  cache_max;

	c->cm_dirty_count = 0;

}

void lru_close(struct cache_manager *c, int print){
	listnode *node = c->cm_head->next;
	struct lru_node *ln;
	int i;
	int total = 0;
	int read_count = 0;
	int write_count = 0;

	if ( print ) {
		fprintf(stdout, " %s hit ratio = %f\n",c->cm_name, (float)c->cm_hit/c->cm_ref);
		fprintf(stdout, " %s Destage Count = %d\n",c->cm_name, c->cm_destage_count);
		fprintf(stdout, " %s Stage Count = %d\n",c->cm_name, c->cm_stage_count);
	}

	while(node != c->cm_head && node){		
		ln = (struct lru_node *)node->data;
		
		total++;
		if(ln->cn_read)
			read_count++;
		if(ln->cn_dirty)
			write_count++;

		free(ln);
		c->cm_free++;
		c->cm_count--;
		node = node->next;
	}

	/*fprintf(stdout, " LRU clean rate = %f\n", (double)read_count/total);
	fprintf(stdout, " LRU dirty rate = %f\n", (double)write_count/total);
	fprintf(outputfile, " LRU clean rate = %f\n", (double)read_count/total);
	fprintf(outputfile, " LRU dirty rate = %f\n", (double)write_count/total);*/

	ASSERT ( c->cm_free == c->cm_size ); 
	if(c->cm_free != c->cm_size){
		fprintf(stderr, " check ... \n");
	}
	ll_release(c->cm_head);

	for(i = 0;i < HASH_NUM;i++){
		ll_release(c->cm_hash[i]);
	}

}

static int compare_blkno(const void *a,const void *b){
	if((int)a > (int)b)
		return 1;
	else if((int)a < (int)b)
		return -1;
	return 0;
}

void lru_release_reqlist(struct cache_manager *c, listnode *req_list){

	while(ll_get_size(req_list)){
		ll_release_tail(req_list);
	}
}

static int lru_compare_page(const void *a,const void *b){
	if((unsigned int)(((struct lru_node *)a)->cn_blkno) == (unsigned int)b)
		return 1;	
	return 0;
}

listnode *lru_presearch(struct cache_manager *c, unsigned int blkno){
	listnode *node;
	listnode *head;
	struct lru_node *ln;
	
	head = c->cm_hash[blkno%HASH_NUM];
	node = ll_find_node(head, (void *)blkno, lru_compare_page);
	if(node){
		ln = (struct lru_node *)node->data;		
		return node;
	}

	return NULL;
}


listnode *lru_search(struct cache_manager *c,unsigned int blkno){
	listnode *node;
	listnode *head;
	struct lru_node *ln;
	int mod = (blkno%HASH_NUM);
		
	c->cm_ref++;
	head = c->cm_hash[mod];
	node = ll_find_node(head,(void *)blkno, lru_compare_page);

	if(node){
		c->cm_hit++;
		ln = (struct lru_node *)node->data;
		ln->cn_recency = 1;
		ln->cn_frequency++;
		return node;
	}else{
		c->cm_miss++;
	}	

	return NULL;
}


void *lru_remove(struct cache_manager *c, listnode *remove_ptr){
	struct lru_node *ln;
	listnode *victim;

	ln = (struct lru_node *)remove_ptr->data;
	
	//if(ln->cn_frequency)
	//	ln = ln;

	// Release Node 
	ll_release_node(c->cm_head, ln->cn_node);

	// Release Hash Node		
	ll_release_node(c->cm_hash[ln->cn_blkno%HASH_NUM], ln->cn_hash);

	//victim  = (listnode *)ln->cn_blkno;

	//	free(ln);
	c->cm_free++;
	c->cm_count--;

	ln->cn_node = NULL;
	ln->cn_hash = NULL;

	return (void *)ln;

}


void *lru_alloc(struct lru_node *ln, unsigned int blkno){
	
	if(ln == NULL){
		ln = (struct lru_node *)malloc(sizeof(struct lru_node));
		if(ln == NULL){
			fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
			exit(1);
		}		
	}

	memset(ln, 0x00, sizeof(struct lru_node));
	
	ln->cn_blkno = blkno;	
	ln->cn_ssd_blk = -1;
	ln->cn_frequency = 0;
	
	return ln;
}


void lru_insert(struct cache_manager *c,struct lru_node *ln){

	// insert node to next of destage ptr 
	ln->cn_node = (listnode *)ll_insert_at_next(c->cm_head, c->cm_destage_ptr,(void *)ln);
	c->cm_free--;
	c->cm_count++;

	// insert node to hash
	ln->cn_hash = (listnode *)ll_insert_at_head(c->cm_hash[(ln->cn_blkno)%HASH_NUM],(void *) ln);

}



listnode *lru_replace(struct cache_manager *c, int watermark){	
	//listnode *destage_ptr;
	listnode *remove_ptr;
	listnode *victim = NULL;

	if(c->cm_free < watermark+1){		
		remove_ptr = c->cm_head->prev;
		c->cm_destage_ptr = c->cm_head;
		victim = CACHE_REMOVE(c, remove_ptr);
	}


	return victim;
}


int lru_inc(struct cache_manager *c, int inc_val){

	if((c->cm_size+inc_val) < (c->cm_max)){		
		c->cm_size+=inc_val;
		c->cm_free+=inc_val;
		return inc_val;
	}else{
		return 0;
	}
}

int lru_dec(struct cache_manager *c, int dec_val){
	if((c->cm_size-dec_val) > 0){
		c->cm_size-=dec_val;
		c->cm_free-=dec_val;

		if(c->cm_size <= 1024)
			c = c;
		return dec_val;
	}else{
		return 0;
	}
}

void lru_init(struct cache_manager **c,char *name, int size,int max_sz,int high,int low){
	*c = (struct cache_manager *)malloc(sizeof(struct cache_manager));
	if(*c == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(1);
	}
	memset(*c, 0x00, sizeof(struct cache_manager));

	(*c)->cache_open = lru_open;
	(*c)->cache_close = lru_close;
	(*c)->cache_presearch = lru_presearch;
	(*c)->cache_search = lru_search;
	(*c)->cache_replace = lru_replace;
	(*c)->cache_remove = lru_remove;
	(*c)->cache_insert = lru_insert;
	(*c)->cache_inc = lru_inc;
	(*c)->cache_dec = lru_dec;
	(*c)->cache_alloc = lru_alloc;

	CACHE_OPEN((*c), size, max_sz);

	(*c)->cm_name = (char *)malloc(strlen(name)+1);
	strcpy((*c)->cm_name, name);

	(*c)->cm_lowwater = low;
	(*c)->cm_highwater = high;
}
#if 0 
int main(){	
#else
int lru_main(){ 
#endif 
	struct cache_manager *lru_manager;
	int i;

	lru_init(&lru_manager,"LRU", 500, 500, 1, 0);

	for(i =0;i < 1000000;i++){
		struct lru_node *ln = NULL;
		listnode *node;
		unsigned int blkno = RND(10000);

		node = CACHE_SEARCH(lru_manager, blkno);
		if(!node){
			
			ln = CACHE_REPLACE(lru_manager, 0);
			ln = CACHE_ALLOC(lru_manager, ln, blkno);
			CACHE_INSERT(lru_manager, ln);
		}else{
			ln = CACHE_REMOVE(lru_manager, node);
			CACHE_INSERT(lru_manager, ln);
		}
	}	

	CACHE_CLOSE(lru_manager, 1);

	return 0;
}

struct lru_node *m_lru_insert(struct cache_manager **lru_manager, int k, int blkno){
	struct lru_node *ln;
	int j;

	for(j = k;j > 0;j--){
		struct lru_node *victim_ln;
		victim_ln = CACHE_REPLACE(lru_manager[j], 0);
		if(victim_ln){		
			free(victim_ln);
		}

		victim_ln = CACHE_REPLACE(lru_manager[j-1], 0);
		if(victim_ln){			
			CACHE_INSERT(lru_manager[j], victim_ln);
		}
	}

	ln = CACHE_ALLOC(lru_manager[0], NULL, blkno);
	
	CACHE_INSERT(lru_manager[0], ln);

	return ln;
}

//#define LRU_NUM 10


//static int mlru_hit = 0;
//static int mlru_ref = 0; 
//static int m

struct cache_manager **mlru_init(char *name,int lru_num, int total_size){
	struct cache_manager **lru_manager;
	char str[128];	
	int i;
	int j;


	lru_manager = (struct cache_manager **)malloc(sizeof(struct cache_manager *) * lru_num);
	if(lru_manager == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(1);
	}
	memset(lru_manager, 0x00, sizeof(struct cache_manager *) * lru_num); 


	if(total_size%lru_num){
		fprintf(stderr, " remainder of total %d / lrunum %d exists\n", total_size, lru_num);
	}
	for(i = 0;i < lru_num;i++){
		sprintf(str,"%s%d", name, i);
		lru_init(&lru_manager[i],str, total_size/lru_num, total_size/lru_num, 1, 0);
	}

	return lru_manager;
}

void mlru_exit(struct cache_manager **lru_manager,int lru_num){
	int i;

//	mlru_hit = 0;

	for(i = 0;i < lru_num;i++){		
		//lru_manager[i]->cm_ref = mlru_ref;
		//mlru_hit += lru_manager[i]->cm_hit;
		CACHE_CLOSE(lru_manager[i], 0);
	}

	//printf(" Multi LRU Hit Ratio = %f \n", (float)mlru_hit/mlru_ref);
}


void mlru_remove(struct cache_manager **lru_manager,int lru_num, int blkno){
	struct listnode *node = NULL;
	struct lru_node *ln;	
	int j;

	for(j = 0;j < lru_num;j++){
		node = CACHE_SEARCH(lru_manager[j], blkno);
		if(node){			
			break;
		}
	}

	

	if(node){ 	
		ln = CACHE_REMOVE(lru_manager[j], node);		
		free(ln);
	}
	
}



struct listnode *mlru_search(struct cache_manager **lru_manager,int lru_num, int blkno, int insert,int hit, int *hit_position){
	listnode *node = NULL;
	struct lru_node *ln;	
	int j;

	for(j = 0;j < lru_num;j++){
		node = CACHE_SEARCH(lru_manager[j], blkno);
		if(node){
			*hit_position = j;
			break;
		}
	}

	if(node){		
		ln =(struct lru_node *) node->data;
		if(ln->cn_frequency > 1)
			ln = ln;
	}


	if(!hit){
		lru_manager[0]->cm_ref--;
		if(node){			
			lru_manager[j]->cm_hit--;
		}
	}

	if(!insert){		
		return node;
	}

	if(!node){ // miss
		ln = m_lru_insert(lru_manager, lru_num - 1, blkno);
	}else{ // hit 
		ln = CACHE_REMOVE(lru_manager[j], node);
		free(ln);
		ln = m_lru_insert(lru_manager, j, blkno);
	}

	return node;
}







int lru_main2(){	
	struct cache_manager **mlru_manager;
	char str[128];	
	int i;
	int j;
	int lru_num = 10;

	mlru_manager = mlru_init("MLRU", lru_num, 500);
	
	for(i =0;i < 1000000;i++){
		struct lru_node *ln = NULL, *ln_new = NULL;
		listnode *node;
		unsigned int blkno = RND(700);
		
		mlru_search(mlru_manager, lru_num, blkno, 1, 1, NULL);
	}	

	mlru_exit(mlru_manager, lru_num);


	return 0;
}

