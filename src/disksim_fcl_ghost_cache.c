#include <stdio.h>
#include "cache.h"
#include "ssd_utils.h"


static void ghost_open(struct cache_manager *c,int cache_size,int cache_max){
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
	c->cm_max = cache_max;
}

static void ghost_close(struct cache_manager *c){
	listnode *node = c->cm_head->next;
	struct ghost_node *cn;
	int i;

	fprintf(stdout, " %s hit ratio = %f\n",c->cm_name, (float)c->cm_hit/c->cm_ref);

	while(node != c->cm_head && node){		
		cn = (struct ghost_node *)node->data;
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

static int ghost_compare_page(const void *a,const void *b){
	if((unsigned int)(((struct ghost_node *)a)->gn_blkno) == (unsigned int)b)
		return 1;	
	return 0;
}


static listnode *ghost_search(struct cache_manager *c, unsigned int blkno){
	listnode *node;
	listnode *head;
	struct ghost_node *gn;
	c->cm_ref++;

	head = c->cm_hash[blkno%HASH_NUM];
	node = ll_find_node(head, (void *)blkno, ghost_compare_page);
	if(node){
		c->cm_hit++;		
		return node;
	}
	c->cm_miss--;

	return NULL;
}


void *ghost_remove(struct cache_manager *c, listnode *remove_ptr){
	struct ghost_node *gn;

	gn = (struct ghost_node *)remove_ptr->data; 
	
	// Release Node 	
	ll_release_node(c->cm_head, gn->gn_node);

	// Release Hash Node		
	ll_release_node(c->cm_hash[gn->gn_blkno%HASH_NUM], gn->gn_hash);

	gn->gn_node = NULL;
	gn->gn_hash = NULL;
	//free(gn);

	c->cm_free++;
	c->cm_count--;

	return (void *)gn;
}



void ghost_insert(struct cache_manager *c,struct ghost_node *gn){

	// insert node to next of destage ptr 
	gn->gn_node = ll_insert_at_next(c->cm_head, c->cm_destage_ptr,(void *)gn);
	c->cm_free--;
	c->cm_count++;

	// insert node to hash
	gn->gn_hash = ll_insert_at_head(c->cm_hash[(gn->gn_blkno)%HASH_NUM],(void *)gn);

	
}

int ghost_inc(struct cache_manager *c,int inc_val){
	if((c->cm_size+inc_val) < (c->cm_max)){
		c->cm_size+=inc_val;
		c->cm_free+=inc_val;
		return inc_val;
	}else{
		return 0;
	}
}


int ghost_dec(struct cache_manager *c,int dec_val){		
	if((c->cm_size-dec_val) > 0){
		c->cm_size-=dec_val;
		c->cm_free-=dec_val;
		return dec_val;
	}else{
		return 0;
	}
}


void *ghost_alloc(struct ghost_node *gn, unsigned int blkno){
	
	if(gn == NULL){
		gn = (struct ghost_node *)malloc(sizeof(struct ghost_node));
		if(gn == NULL){
			fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
			exit(1);
		}
		memset(gn, 0x00, sizeof(struct ghost_node));
	}

	gn->gn_blkno = blkno;	

	return gn;
}


static listnode *ghost_replace(struct cache_manager *c){
	listnode *remove_ptr;
	void *victim = NULL;

	if(c->cm_free < 1){		
		remove_ptr = c->cm_head->prev;
		c->cm_destage_ptr = c->cm_head;
		victim = CACHE_REMOVE(c, remove_ptr);
		free(victim);
	}
	
	return NULL;
}

void ghost_init(struct cache_manager **c,char *name, int size, int max_sz){
	*c = (struct cache_manager *)malloc(sizeof(struct cache_manager));
	if(*c == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(1);
	}
	memset(*c, 0x00, sizeof(struct cache_manager));


	(*c)->cache_open = ghost_open;
	(*c)->cache_close = ghost_close;
	(*c)->cache_search = ghost_search;
	(*c)->cache_replace = ghost_replace;
	(*c)->cache_remove = ghost_remove;
	(*c)->cache_insert = ghost_insert;
	(*c)->cache_inc = ghost_inc;
	(*c)->cache_dec = ghost_dec;
	(*c)->cache_alloc = ghost_alloc;
	
	CACHE_OPEN((*c), size, max_sz);

	(*c)->cm_name = (char *)malloc(strlen(name)+1);
	strcpy((*c)->cm_name, name);

}


int ghost_main(){
	struct cache_manager *ghost_manager;
	int i;

	ghost_init(&ghost_manager, "GHOST", 500, 500);
		

	for(i =0;i < 1000000;i++){
		listnode *node;
		unsigned int blkno = RND(700);

		node = CACHE_SEARCH(ghost_manager, blkno);
		if(!node){
			struct ghost_node *gn;
			CACHE_REPLACE(ghost_manager, 0);
			gn = CACHE_ALLOC(ghost_manager, NULL, blkno);
			CACHE_INSERT(ghost_manager, gn);
		}/*else{
			ghost_remove(ghost_manager, node);
		}*/
	}

	CACHE_CLOSE(ghost_manager);
	return 0;
}