#include <stdio.h>
#include "cache.h"
#include "ssd_utils.h"
#include "wow_driver.h"




void bglru_evict_blocks(struct cache_manager *c, struct bglru_node *bn);


void bglru_open(struct cache_manager *c,int cache_size, int cache_max){
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

	ll_create(&c->cm_hddrq);
	ll_create(&c->cm_hddwq);
	ll_create(&c->cm_ssdrq);
	ll_create(&c->cm_ssdwq);


	c->cm_destage_ptr = c->cm_head;
	c->cm_ref = 0;
	c->cm_hit = 0;
	c->cm_miss = 0;
	c->cm_size = cache_size;
	c->cm_free = cache_size;
	c->cm_count = 0;
	c->cm_max =  cache_max;

}

void bglru_close(struct cache_manager *c){
	listnode *node = c->cm_head->next;
	struct bglru_node *bn;
	int i;

	//fprintf(stdout, " %s hit ratio = %f\n",c->cm_name, (float)c->cm_hit/c->cm_ref);

	while(node != c->cm_head && node){		
		bn = (struct bglru_node *)node->data;
				
		bglru_evict_blocks(c, (struct bglru_node *)bn);

		free(bn);
		//c->cm_free++;
		//c->cm_count--;
		c->cm_groupcount--;
		node = node->next;
	}
	if(c->cm_groupcount != 0){
		fprintf(stderr, " check ... \n");
	}
	ll_release(c->cm_head);

	for(i = 0;i < HASH_NUM;i++){
		ll_release(c->cm_hash[i]);
	}

}

static int compare_hdd(const void *a,const void *b){
	if(((struct lru_node *)a)->cn_blkno > ((struct lru_node *)b)->cn_blkno)
		return 1;
	else if(((struct lru_node *)a)->cn_blkno < ((struct lru_node *)b)->cn_blkno) 
		return -1;
	return 0;
}

static int compare_hdd2(const void *a,const void *b){
	if(((struct lru_node *)a)->cn_blkno == (int)b)
		return 1;

	return 0;
}


static int bglru_compare_gno(const void *a,const void *b){
	if((unsigned int)(((struct bglru_node *)a)->bg_no) == (unsigned int)b)
		return 1;	
	return 0;
}




listnode *bglru_presearch(struct cache_manager *c, unsigned int blkno){
	struct bglru_node *bn = NULL;
	struct lru_node *ln = NULL;	
	listnode *gnode, *node;
	unsigned int groupno = blkno/GRP2BLK;
	int group_alloc = 0, node_alloc = 0;

	//c->cm_ref++;

	gnode = CACHE_SEARCH(c, blkno);
	if(gnode){ /* hit  in group*/
		bn = (struct bglru_node *)gnode->data;

		/* Move to MRU position*/
		//bn = CACHE_REMOVE(c, gnode);
		//CACHE_INSERT(c, bn);

		node = ll_find_node(bn->bg_block_hash[blkno%BG_HASH_NUM], (void *)blkno, compare_hdd2);
		if(node){ 
			/* Hit in group node*/				
			//c->cm_hit++;				
			node_alloc = 0;
			group_alloc = 0;

			ln = (struct lru_node *)node->data;
			return (listnode *)ln;
		}else{
			/* Miss in group node */			
			node_alloc  = 1;
		}
	}else{ 
		/* Miss in group */
		group_alloc = 1;
		node_alloc = 1;
	}

	return NULL;
}

listnode *bglru_search(struct cache_manager *c, unsigned int blkno){
	listnode *gnode;
	listnode *head;
	int groupno = blkno/GRP2BLK;
	
	head = c->cm_hash[groupno%HASH_NUM];
	gnode = ll_find_node(head,(void *)groupno, bglru_compare_gno);	
	if(gnode){ /* hit  in group*/
		return gnode;
	}

	return NULL;
}



void *bglru_remove(struct cache_manager *c, listnode *remove_ptr){
	struct bglru_node *bn;
	listnode *victim;

	bn = (struct bglru_node *)remove_ptr->data;

	// Release Node 
	ll_release_node(c->cm_head, bn->bg_node);

	// Release Hash Node		
	ll_release_node(c->cm_hash[bn->bg_no%HASH_NUM], bn->bg_hash);

	//victim  = (listnode *)ln->cn_blkno;

	//	free(ln);
	//c->cm_free++;
	//c->cm_count--;
	c->cm_groupcount--;

	bn->bg_node = NULL;
	bn->bg_hash = NULL;

	return (void *)bn;

}


void *bglru_alloc(struct bglru_node *bn, unsigned int groupno){
	int i;

	if(bn == NULL){
		bn = (struct bglru_node *)malloc(sizeof(struct bglru_node));
		if(bn == NULL){
			fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
			exit(1);
		}
		memset(bn, 0x00, sizeof(struct bglru_node));
	}

	bn->bg_no = groupno;


	ll_create(&bn->bg_block_list);
	
	for(i = 0;i <BG_HASH_NUM;i++){
		ll_create(&bn->bg_block_hash[i]);
	}

	return bn;
}



struct lru_node *bglru_insert_block_to_group(struct cache_manager *c, struct bglru_node *bn,int blk, struct lru_node *ln){
	//struct lru_node *ln;

	if(ln == NULL){
		ln = (struct lru_node *)lru_alloc(NULL, blk);
		ln->cn_ssd_blk = reverse_map_alloc_blk(blk);
	}
	
	ln->cn_node = ll_insert_at_sort(bn->bg_block_list, ln, compare_hdd);	
	bn->block_count++;
	ln->cn_hash = ll_insert_at_tail(bn->bg_block_hash[blk%BG_HASH_NUM], ln);

	c->cm_free--;
	c->cm_count++;

	return ln;
}

void bglru_insert(struct cache_manager *c,struct bglru_node *bn){

	// insert node to next of destage ptr 
	bn->bg_node = (listnode *)ll_insert_at_next(c->cm_head, c->cm_destage_ptr,(void *)bn);
	//c->cm_free--;
	c->cm_groupcount++;

	// insert node to hash
	bn->bg_hash = (listnode *)ll_insert_at_head(c->cm_hash[(bn->bg_no)%HASH_NUM],(void *)bn);

}

//extern int destage_count;

void bglru_evict_blocks(struct cache_manager *c, struct bglru_node *bn){
	struct lru_node *ln;
	listnode *node;	
	int i;

	node = bn->bg_block_list->prev;

	while(bn->block_count){
		ln = (struct lru_node *)node->data;
	
		if(ln->cn_dirty){ 
			CACHE_MAKERQ(c, c->cm_ssdrq, NULL, ln->cn_ssd_blk);
			CACHE_MAKERQ(c, c->cm_hddwq, NULL, reverse_get_blk(ln->cn_ssd_blk));
			c->cm_destage_count++;
		}	
		reverse_map_release_blk(ln->cn_ssd_blk);
		
		free(ln);

		bn->block_count--;
		c->cm_free++;
		c->cm_count--;
		NODE_PREV(node);
	}
	

	ll_release(bn->bg_block_list);

	for(i = 0;i < BG_HASH_NUM;i++){
		ll_release(bn->bg_block_hash[i]);
	}

}


listnode *bglru_replace(struct cache_manager *c, int watermark){	
	listnode *destage_ptr;
	listnode *remove_ptr;
	struct bglru_node *victim = NULL;

	if(c->cm_free < watermark + 1){
		remove_ptr = c->cm_head->prev;
		c->cm_destage_ptr = c->cm_head;
		victim = CACHE_REMOVE(c, remove_ptr);
		bglru_evict_blocks(c, (struct bglru_node *)victim);

		free(victim);
		victim = NULL;
	}

	return (listnode *)victim;
}


int bglru_inc(struct cache_manager *c, int inc_val){

	if((c->cm_size+inc_val) < (c->cm_max)){
		c->cm_size+=inc_val;
		c->cm_free+=inc_val;
		return inc_val;
	}else{
		return 0;
	}
}

int bglru_dec(struct cache_manager *c, int dec_val){
	if((c->cm_size-dec_val) > 0){
		c->cm_size-=dec_val;
		c->cm_free-=dec_val;
		return dec_val;
	}else{
		return 0;
	}



}
void bglru_make_reqlist(struct cache_manager *c, listnode *req_list, Rb_node *rbtree, int blk){
	//ll_insert_at_sort(req_list,(void *)blk, compare_blkno);
	ll_insert_at_tail(req_list, (void *)blk);
	//RBTreeInsert(rbtree, blk, 0);
	//rb_insert_a()
}
void bglru_release_reqlist(struct cache_manager *c, listnode *req_list){

	while(ll_get_size(req_list)){
		ll_release_tail(req_list);
	}
}


void bglru_init(struct cache_manager **c,char *name, int size,int max_sz, int high, int low){
	*c = (struct cache_manager *)malloc(sizeof(struct cache_manager));
	if(*c == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(1);
	}
	memset(*c, 0x00, sizeof(struct cache_manager));

	(*c)->cache_open = bglru_open;
	(*c)->cache_close = bglru_close;
	(*c)->cache_presearch = bglru_presearch;
	(*c)->cache_search = bglru_search;
	(*c)->cache_replace = bglru_replace;
	(*c)->cache_remove = bglru_remove;
	(*c)->cache_insert = bglru_insert;
	(*c)->cache_inc = bglru_inc;
	(*c)->cache_dec = bglru_dec;
	(*c)->cache_alloc = bglru_alloc;
	(*c)->cache_makerq = bglru_make_reqlist;
	(*c)->cache_releaserq = bglru_release_reqlist;

	CACHE_OPEN((*c), size, max_sz);

	(*c)->cm_name = (char *)malloc(strlen(name)+1);
	strcpy((*c)->cm_name, name);


	(*c)->cm_highwater = high;
	(*c)->cm_lowwater = low;
}



void bglru_make_req(struct cache_manager *c, int blkno, int is_read){
	struct bglru_node *bn = NULL;
	struct lru_node *ln = NULL;	
	listnode *gnode, *node;
	unsigned int groupno = blkno/GRP2BLK;
	int group_alloc = 0, node_alloc = 0;

	c->cm_ref++;

	gnode = CACHE_SEARCH(c, blkno);
	if(gnode){ /* hit  in group*/
		bn = (struct bglru_node *)gnode->data;

		/* Move to MRU position*/
		bn = CACHE_REMOVE(c, gnode);
		CACHE_INSERT(c, bn);

		node = ll_find_node(bn->bg_block_hash[blkno%BG_HASH_NUM], (void *)blkno, compare_hdd2);
		if(node){ 
			/* Hit in group node*/				
			c->cm_hit++;				
			node_alloc = 0;
			group_alloc = 0;

			ln = (struct lru_node *)node->data;
		}else{
			/* Miss in group node */			
			node_alloc  = 1;
		}
	}else{ 
		/* Miss in group */
		group_alloc = 1;
		node_alloc = 1;
	}

	if(node_alloc){
		if(c->cm_free <= c->cm_lowwater){
			while(c->cm_free < c->cm_highwater){
				CACHE_REPLACE(c, c->cm_highwater);
			}			
		}
		
		if(group_alloc){
			/* Alloc new group node */
			bn = CACHE_ALLOC(c, NULL, groupno);
			CACHE_INSERT(c, bn);			
		}
		ln = bglru_insert_block_to_group(c, bn, blkno, NULL);	
	}
	if(!is_read){
		CACHE_MAKERQ(c, c->cm_ssdwq, NULL, ln->cn_ssd_blk);
		ln->cn_dirty = 1;
	}else
		CACHE_MAKERQ(c, c->cm_ssdrq, NULL, ln->cn_ssd_blk);
	

	return;
}
int bglru_main(){	
	struct cache_manager *bglru_manager;	
	unsigned int blkno = RND(700000);
	int i;
	

	bglru_init(&bglru_manager,"BGLRU", 50000, 50000, 1024, 0);

	for(i =0;i < 1000000;i++){				
		blkno = RND(700000);
		
		bglru_make_req(bglru_manager, blkno, READ);		
	}	

	CACHE_CLOSE(bglru_manager);

	return 0;
}