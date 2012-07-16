    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: LRU Cache Algorithm
* File Name		: disksim_fcl_lru.c 
*/

#include <stdio.h>
#include <stdlib.h>
#include "disksim_fcl_cache.h"
#include "disksim_fcl.h"
#include "../ssdmodel/ssd_utils.h"


void lru_open(struct cache_manager *c,int cache_size, int cache_max){
	int i;

	INIT_LIST_HEAD ( &c->cm_head );
	INIT_LIST_HEAD ( &c->cm_dirty_head ); 
	INIT_LIST_HEAD ( &c->cm_clean_head ); 

	c->cm_hash = (struct hlist_head *)malloc(sizeof(struct hlist_head) * HASH_NUM);
	if(c->cm_hash == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(1);
	}

	for(i = 0;i < HASH_NUM;i++){
		INIT_HLIST_HEAD ( &c->cm_hash[i] );
	}

	c->cm_destage_ptr = &c->cm_head;
	c->cm_ref = 0;
	c->cm_hit = 0;
	c->cm_miss = 0;
	c->cm_size = cache_size;
	c->cm_free = cache_size;
	c->cm_count = 0;
	c->cm_max =  cache_max;


	c->cm_clean_size = cache_size;
	c->cm_clean_free = cache_size;
	c->cm_clean_count = 0;

	c->cm_dirty_size = 0;
	c->cm_dirty_free = 0;
	c->cm_dirty_count = 0;

}

#if 0 
int lru_inc(struct cache_manager *c, int inc_val){

	if((c->cm_size+inc_val) <= (c->cm_max)){		
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

	//	if(c->cm_size <= 1024)
	//		c = c;
		return dec_val;
	}else{
		return 0;
	}
}
#endif 

void lru_set_dirty_size ( struct cache_manager *c, int dirty_size, int clean_size ) {
	int diff;

	diff = dirty_size - c->cm_dirty_size;
	c->cm_dirty_size += diff;
	c->cm_dirty_free += diff;

	diff = clean_size - c->cm_clean_size;
	c->cm_clean_size += diff;
	c->cm_clean_free += diff;
	
	diff = ( dirty_size + clean_size ) - c->cm_size;
	c->cm_size += diff;
	c->cm_free += diff;

	//c->cm_size = dirty_size + clean_size;
	//printf ( " clean free = %d, dirty free = %d \n", c->cm_clean_free, c->cm_dirty_free );

	//ASSERT ( dirty_size + clean_size == c->cm_size );

}

void lru_print ( struct cache_manager *c, FILE *fp ) {

	fprintf(fp, " %s hit ratio = %f\n",c->cm_name, (float)c->cm_hit/c->cm_ref);
	fprintf(fp, " %s Destage Count = %d\n",c->cm_name, c->cm_destage_count);
	fprintf(fp, " %s Stage Count = %d\n",c->cm_name, c->cm_stage_count);
	fprintf(fp, " %s Dirty Count = %d \n",c->cm_name, c->cm_dirty_count );
	fprintf(fp, " %s List Count = %d \n", c->cm_name, c->cm_count );
}

void lru_close(struct cache_manager *c){
	struct list_head *head = &c->cm_head;
	struct list_head *ptr;
	struct lru_node *ln;

	int i;
	int total = 0;
	int read_count = 0;
	int write_count = 0;


	list_for_each ( ptr, head ) {
		ln = (struct lru_node *) list_entry ( ptr, struct lru_node, cn_list );

		total++;
		if(ln->cn_read)
			read_count++;
		if(ln->cn_dirty)
			write_count++;

		c->cm_free++;
		c->cm_count--;
	}

	ASSERT ( c->cm_count == 0 );

	while ( !list_empty ( head ) ) {
		ptr = head->next;
		ln = (struct lru_node *) list_entry ( ptr, struct lru_node, cn_list );

		list_del ( & ln->cn_list );

		if ( ln->cn_dirty ) 
			list_del( & ln->cn_dirty_list );
		else 
			list_del ( & ln->cn_clean_list );

		hlist_del ( & ln->cn_hash );

		free(ln);
	}
#if 0 
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
#endif 

	/*fprintf(stdout, " LRU clean rate = %f\n", (double)read_count/total);
	fprintf(stdout, " LRU dirty rate = %f\n", (double)write_count/total);
	fprintf(outputfile, " LRU clean rate = %f\n", (double)read_count/total);
	fprintf(outputfile, " LRU dirty rate = %f\n", (double)write_count/total);*/

	ASSERT ( c->cm_free == c->cm_size ); 
	if(c->cm_free != c->cm_size){
		fprintf(stderr, " check ... \n");
	}

	ASSERT ( list_empty ( &c->cm_head ) );
	//ll_release(c->cm_head);

	for(i = 0;i < HASH_NUM;i++){
		ASSERT ( hlist_empty ( &c->cm_hash[i] ) ); 
	}

}
#if 0 
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

#endif

struct lru_node *lru_presearch(struct cache_manager *c, unsigned int blkno){
	struct hlist_node *node;
	struct hlist_head *head;
	struct lru_node *ln;
	
	head = &c->cm_hash[blkno%HASH_NUM];
	hlist_for_each ( node, head ) {
		ln = hlist_entry( node,  struct lru_node, cn_hash );

		if ( ln->cn_blkno == blkno ) 
			return ln;
	}

	return NULL;
}


struct lru_node *lru_search(struct cache_manager *c,unsigned int blkno){
	struct lru_node *ln;
		
	c->cm_ref++;
	//head = c->cm_hash[mod];
	//node = ll_find_node(head,(void *)blkno, lru_compare_page);
	ln = lru_presearch ( c, blkno );

	if(ln){
		c->cm_hit++;
		//ln = (struct lru_node *)node->data;
		ln->cn_recency = 1;
		ln->cn_frequency++;
		return ln;
	}else{
		c->cm_miss++;
	}	

	return NULL;
}


void lru_movemru(struct cache_manager *c, struct lru_node *ln ) {

	list_del ( &ln->cn_list );

	if ( ln->cn_dirty ) 
		list_del ( &ln->cn_dirty_list );
	else
		list_del ( &ln->cn_clean_list );


	list_add( &ln->cn_list, &c->cm_head );

	if ( ln->cn_dirty ) 
		list_add( &ln->cn_dirty_list, &c->cm_dirty_head );
	else
		list_add ( &ln->cn_clean_list, &c->cm_clean_head );

}
void *lru_remove(struct cache_manager *c, struct lru_node *ln ) {
	
	list_del ( &ln->cn_list );

	if ( ln->cn_dirty ) {
		list_del ( &ln->cn_dirty_list );
		c->cm_dirty_count --;
		c->cm_dirty_free ++;
	} else {
		list_del ( &ln->cn_clean_list );
		c->cm_clean_count --;
		c->cm_clean_free ++;
	}

	hlist_del ( &ln->cn_hash );

	c->cm_free++;
	c->cm_count--;

	ASSERT ( c->cm_clean_count + c->cm_dirty_count == c->cm_count );

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

void lru_move_clean_list ( struct cache_manager *c, struct lru_node *ln ) {

	list_del ( &ln->cn_dirty_list );
	c->cm_dirty_count --;
	c->cm_dirty_free ++;

	list_add ( &ln->cn_clean_list, &c->cm_clean_head );
	c->cm_clean_count ++;
	c->cm_clean_free --;


	ASSERT ( c->cm_clean_count <= c->cm_clean_size );
	ASSERT ( c->cm_clean_count + c->cm_dirty_count == c->cm_count );

}

void lru_insert(struct cache_manager *c,struct lru_node *ln){

	// insert node to next of destage ptr 
	//ln->cn_node = (listnode *)ll_insert_at_next(c->cm_head, c->cm_destage_ptr,(void *)ln);

	list_add( &ln->cn_list, &c->cm_head );

	if ( ln->cn_dirty ) {
		list_add( &ln->cn_dirty_list, &c->cm_dirty_head);
		c->cm_dirty_count ++;
		c->cm_dirty_free --;
	} else {
		list_add( &ln->cn_clean_list, &c->cm_clean_head);
		c->cm_clean_count++;
		c->cm_clean_free --;
	}

	hlist_add_head( &ln->cn_hash, &c->cm_hash[(ln->cn_blkno) % HASH_NUM] ) ; 

	c->cm_free--;
	c->cm_count++;

	ASSERT ( c->cm_clean_count + c->cm_dirty_count == c->cm_count );

}



void *lru_replace(struct cache_manager *c, int watermark, int replace_type ){	
	struct list_head *remove_ptr;
	struct lru_node *victim = NULL;
	int free;

	//printf ( " dirty = %d \n", dirty );

	switch ( replace_type ) {
		case FCL_REPLACE_DIRTY:
			//free = c->cm_dirty_size - c->cm_dirty_count;
			free = c->cm_dirty_free;
			break;
		case FCL_REPLACE_CLEAN:
			free = c->cm_clean_free;
			//free = c->cm_clean_size - c->cm_clean_count;
			break;
		case FCL_REPLACE_ANY:
			free = c->cm_free;
			break;
	}

	if ( free < watermark + 1 ) {

		switch ( replace_type ) {
			case FCL_REPLACE_DIRTY:

				remove_ptr = (struct list_head *)(&c->cm_dirty_head)->prev;
				victim = list_entry ( remove_ptr, struct lru_node, cn_dirty_list );
				break;

			case FCL_REPLACE_CLEAN:
				remove_ptr = (struct list_head *)(&c->cm_clean_head)->prev;
				victim = list_entry ( remove_ptr, struct lru_node, cn_clean_list );
				break;

			case FCL_REPLACE_ANY:
				remove_ptr = (struct list_head *)(&c->cm_head)->prev;
				victim = list_entry ( remove_ptr, struct lru_node, cn_list );
				break;
		}

		victim = CACHE_REMOVE(c, victim);
	}
#if 0 
	if(c->cm_free < watermark+1){		
		remove_ptr = (struct list_head *)(&c->cm_head)->prev;

		if ( !list_empty (&c->cm_head) ) {
			victim = list_entry ( remove_ptr, struct lru_node, cn_list );
			victim = CACHE_REMOVE(c, victim);
		}

	}
#endif 

	return victim;
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
	(*c)->cache_move_mru = lru_movemru;
	(*c)->cache_insert = lru_insert;
	//(*c)->cache_inc = lru_inc;
	//(*c)->cache_dec = lru_dec;
	(*c)->cache_alloc = lru_alloc;
	(*c)->cache_print = lru_print;

	CACHE_OPEN((*c), size, max_sz);

	(*c)->cm_name = (char *)malloc(strlen(name)+1);
	strcpy((*c)->cm_name, name);

	(*c)->cm_lowwater = low;
	(*c)->cm_highwater = high;
}

#if 0 
void lru_print ( struct cache_manager *c ) {
	struct list_head *head, *ptr;
	struct lru_node *ln;
	head = &c->cm_head;

	list_for_each( ptr, head ) {
		ln = (struct lru_node *) list_entry ( ptr, struct lru_node, cn_list );	
		printf (" blkno = %d, ssdno = %d \n", ln->cn_blkno, ln->cn_ssd_blk );
	}
	
	printf ( " lru count = %d \n", c->cm_count );


}
#endif 
struct lru_node *m_lru_insert(struct cache_manager **lru_manager, int k, int blkno){
	struct lru_node *ln;
	int j;

	for(j = k;j > 0;j--){
		struct lru_node *victim_ln;
		victim_ln = CACHE_REPLACE(lru_manager[j], 0, FCL_REPLACE_ANY);
		if(victim_ln){		
			free(victim_ln);
		}

		victim_ln = CACHE_REPLACE(lru_manager[j-1], 0, FCL_REPLACE_ANY);
		if(victim_ln){			
			victim_ln->cn_dirty = 0;
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

	int mlru_hit = 0;

	for(i = 0;i < lru_num;i++){		

		mlru_hit += lru_manager[i]->cm_hit;
		CACHE_CLOSE(lru_manager[i]);

		//printf(" %d Multi LRU Hit Ratio = %f \n", i, (float)mlru_hit/lru_manager[0]->cm_ref);
	}

}


void mlru_remove(struct cache_manager **lru_manager,int lru_num, int blkno){
//	listnode *node = NULL;
	struct lru_node *ln;	
	int j;

	for(j = 0;j < lru_num;j++){
		ln = CACHE_SEARCH(lru_manager[j], blkno);
		if(ln){			
			break;
		}
	}

	

	if(ln){ 	
		ln = CACHE_REMOVE(lru_manager[j], ln);
		free(ln);
	}
	
}



struct lru_node *mlru_search(struct cache_manager **lru_manager,int lru_num, int blkno, int insert,int hit, int *hit_position){
	//listnode *node = NULL;
	struct lru_node *ln;	
	int j;

	for(j = 0;j < lru_num;j++){
		ln = CACHE_SEARCH(lru_manager[j], blkno);

		if ( hit_position )
			*hit_position = j;

		if(ln){
			break;
		}
	}

	if(ln){		
		//ln =(struct lru_node *) node->data;
		if(ln->cn_frequency > 1)
			ln = ln;
	}

	//if ( j > 0 && j < lru_num ) {
	//	printf (" hit position = %d \n");
	//}
	if(!hit){
		lru_manager[0]->cm_ref--;
		if(ln){			
			lru_manager[j]->cm_hit--;
		}
	}

	if(!insert){		
		return ln;
	}

	if(!ln){ // miss
		ln = m_lru_insert(lru_manager, lru_num - 1, blkno);
	}else{ // hit 
		ln = CACHE_REMOVE(lru_manager[j], ln);
		free(ln);
		ln = m_lru_insert(lru_manager, j, blkno);
	}

	return ln;
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

