#include <stdio.h>
#include "cache.h"
#include "ssd_utils.h"

void clock_open(struct cache_manager *c,int cache_size, int cache_max){
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
}

void clock_close(struct cache_manager *c){
	listnode *node = c->cm_head->next;
	struct clock_node *cn;
	int i;
	
	fprintf(stdout, " %s hit ratio = %f\n",c->cm_name, (float)c->cm_hit/c->cm_ref);

	while(node != c->cm_head && node){		
		cn = (struct clock_node *)node->data;
		free(cn);
		c->cm_free++;
		c->cm_count--;
		node = node->next;
	}
	if(c->cm_free != c->cm_size){
		fprintf(stderr, " check ... \n");
	}
	ll_release(c->cm_head);

	for(i = 0;i < HASH_NUM;i++){
		ll_release(c->cm_hash[i]);
	}

}

int clock_compare_page(const void *a,const void *b){
	if((unsigned int)(((struct clock_node *)a)->cn_blkno) == (unsigned int)b)
		return 1;	
	return 0;
}


listnode *clock_search(struct cache_manager *c, unsigned int blkno){
	listnode *node;
	listnode *head;
	struct clock_node *cn;
	c->cm_ref++;

	head = c->cm_hash[blkno%HASH_NUM];
	node = ll_find_node(head,(void *)blkno, clock_compare_page);
	if(node){
		c->cm_hit++;

		cn = (struct clock_node *)node->data;
		cn->cn_recency = 1;
		return node;
	}

	c->cm_miss++;

	return NULL;
}

listnode *clock_select_victim(struct cache_manager *c, listnode *destage_ptr){
	struct clock_node *cn;
	
	while(1){

		if(destage_ptr == c->cm_head)
			destage_ptr = destage_ptr->prev;

		cn = (struct clock_node *)(destage_ptr->data);
		if(!cn->cn_recency){				
			break;
		}
		cn->cn_recency = 0;
		destage_ptr = destage_ptr->prev;
	}

	return destage_ptr;
}


void *clock_remove(struct cache_manager *c, listnode *remove_ptr){
	struct clock_node *cn;
	listnode *victim;

	cn = (struct clock_node *)remove_ptr->data;

	// Release Node 
	ll_release_node(c->cm_head, cn->cn_node);

	// Release Hash Node		
	ll_release_node(c->cm_hash[cn->cn_blkno%HASH_NUM], cn->cn_hash);

	c->cm_free++;
	c->cm_count--;

	cn->cn_node = NULL;
	cn->cn_hash = NULL;

	return (void *)cn;

}


void *clock_alloc(struct clock_node *cn, unsigned int blkno){
	
	if(cn == NULL){
		cn = (struct clock_node *)malloc(sizeof(struct clock_node));
		if(cn == NULL){
			fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
			exit(1);
		}
		memset(cn, 0x00, sizeof(struct clock_node));
	}
	
	cn->cn_blkno = blkno;
	cn->cn_recency = 1;
	

	return cn;
}


void clock_insert(struct cache_manager *c,struct clock_node *cn){
		
	// insert node to next of destage ptr 
	cn->cn_node = (listnode *)ll_insert_at_next(c->cm_head, c->cm_destage_ptr,(void *)cn);
	c->cm_free--;
	c->cm_count++;

	// insert node to hash
	cn->cn_hash = (listnode *)ll_insert_at_head(c->cm_hash[(cn->cn_blkno)%HASH_NUM],(void *) cn);

}



listnode *clock_replace(struct cache_manager *c){
	listnode *destage_ptr;
	listnode *remove_ptr;
	listnode *victim = NULL;

	if(c->cm_free < 1){
		destage_ptr = c->cm_destage_ptr;
		destage_ptr = clock_select_victim(c, destage_ptr);		

		remove_ptr = destage_ptr;
		c->cm_destage_ptr = destage_ptr->prev;		
		victim = CACHE_REMOVE(c, remove_ptr);
	}

	return victim;
}


int clock_inc(struct cache_manager *c, int inc_val){
	if((c->cm_size+inc_val) < (c->cm_max)){
		c->cm_size+=inc_val;
		c->cm_free+=inc_val;
		return inc_val;
	}else{
		return 0;
	}
}

int clock_dec(struct cache_manager *c,  int dec_val){
	if((c->cm_size-dec_val) > 0){
		c->cm_size-=dec_val;
		c->cm_free-=dec_val;
		return dec_val;
	}else{
		return 0;
	}
}

void clock_init(struct cache_manager **c,char *name, int size,int max_sz){
	*c = (struct cache_manager *)malloc(sizeof(struct cache_manager));
	if(*c == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(1);
	}
	memset(*c, 0x00, sizeof(struct cache_manager));

	(*c)->cache_open = clock_open;
	(*c)->cache_close = clock_close;
	(*c)->cache_search = clock_search;
	(*c)->cache_replace = clock_replace;
	(*c)->cache_remove = clock_remove;
	(*c)->cache_insert = clock_insert;
	(*c)->cache_inc = clock_inc;
	(*c)->cache_dec = clock_dec;
	(*c)->cache_alloc = clock_alloc;

	CACHE_OPEN((*c), size, max_sz);

	(*c)->cm_name = (char *)malloc(strlen(name)+1);
	strcpy((*c)->cm_name, name);

}

int clock_main(){	
	struct cache_manager *clock_manager;
	int i;
		
	clock_init(&clock_manager,"CLOCK", 500, 500);

	for(i =0;i < 1000000;i++){
		listnode *node;
		unsigned int blkno = RND(700);

		node = CACHE_SEARCH(clock_manager, blkno);
		if(!node){
			struct clock_node *cn = NULL;
			cn = CACHE_REPLACE(clock_manager, 0);
			cn = CACHE_ALLOC(clock_manager, cn, blkno);
			CACHE_INSERT(clock_manager, cn);
		}
	}	

	CACHE_CLOSE(clock_manager);

	return 0;
}


