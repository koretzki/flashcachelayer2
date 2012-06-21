#include <stdio.h>
#include "cache.h"
#include "ssd_utils.h"
#include "wow_driver.h"




void bgclock_evict_blocks(struct cache_manager *c, struct bglru_node *bn);
void bgclock_make_req(struct cache_manager *c, int blkno, int is_read,int dram_hit,int ghost_hit);


void bgclock_open(struct cache_manager *c,int cache_size, int cache_max){
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

void bgclock_close(struct cache_manager *c){
	listnode *node = c->cm_head->next;
	struct bglru_node *bn;
	int i;

	//fprintf(stdout, " %s hit ratio = %f\n",c->cm_name, (float)c->cm_hit/c->cm_ref);

	while(node != c->cm_head && node){		
		bn = (struct bglru_node *)node->data;

		bgclock_evict_blocks(c, (struct bglru_node *)bn);

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
static int compare_hdd3(const void *a,const void *b){
	if(((struct bglru_node *)a)->bg_no > ((struct bglru_node *)b)->bg_no)
		return 1;
	else if(((struct bglru_node *)a)->bg_no < ((struct bglru_node *)b)->bg_no) 
		return -1;
	return 0;
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


static int bgclock_compare_gno(const void *a,const void *b){
	if((unsigned int)(((struct bglru_node *)a)->bg_no) == (unsigned int)b)
		return 1;	
	return 0;
}




listnode *bgclock_presearch(struct cache_manager *c, unsigned int blkno){
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

listnode *bgclock_search(struct cache_manager *c, unsigned int blkno){
	listnode *gnode;
	listnode *head;
	int groupno = blkno/GRP2BLK;

	head = c->cm_hash[groupno%HASH_NUM];
	gnode = ll_find_node(head,(void *)groupno, bgclock_compare_gno);	
	if(gnode){ /* hit  in group*/
		return gnode;
	}

	return NULL;
}



void *bgclock_remove(struct cache_manager *c, listnode *remove_ptr){
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


void *bgclock_alloc(struct bglru_node *bn, unsigned int groupno){
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
	bn->bg_recency = 0;

	ll_create(&bn->bg_block_list);

	for(i = 0;i <BG_HASH_NUM;i++){
		ll_create(&bn->bg_block_hash[i]);
	}

	return bn;
}



struct lru_node *bgclock_insert_block_to_group(struct cache_manager *c, struct bglru_node *bn,int blk, struct lru_node *ln){
	//struct lru_node *ln;

	if(ln == NULL){
		ln = (struct lru_node *)lru_alloc(NULL, blk);
		ln->cn_ssd_blk = reverse_map_alloc_blk(blk);
	}

	ln->cn_node = ll_insert_at_tail(bn->bg_block_list, ln);
	bn->block_count++;
	ln->cn_hash = ll_insert_at_head(bn->bg_block_hash[blk%BG_HASH_NUM], ln);

	c->cm_free--;
	c->cm_count++;

	return ln;
}

void bgclock_insert(struct cache_manager *c,struct bglru_node *bn){

	// insert node to next of destage ptr 
	bn->bg_node = (listnode *)ll_insert_at_sort(c->cm_head,(void *)bn, compare_hdd3);
	//c->cm_free--;
	c->cm_groupcount++;

	// insert node to hash
	bn->bg_hash = (listnode *)ll_insert_at_head(c->cm_hash[(bn->bg_no)%HASH_NUM],(void *)bn);

}

extern struct cache_manager *dram_cache;

void bgclock_evict_blocks(struct cache_manager *c, struct bglru_node *bn){
	struct lru_node *ln;
	listnode *node;	
	int i;

	node = bn->bg_block_list->prev;

	while(bn->block_count){
		ln = (struct lru_node *)node->data;

		if(ln->cn_dirty){ 
			CACHE_MAKERQ(c, c->cm_ssdrq, NULL, ln->cn_ssd_blk);
			CACHE_MAKERQ(c, c->cm_hddwq, NULL, reverse_get_blk(ln->cn_ssd_blk));
			search_dram_cache(dram_cache, reverse_get_blk(ln->cn_ssd_blk), WRITE);
			c->cm_destage_count++;
		}else{
			ssd_trim_command(SSD, ln->cn_ssd_blk * BLOCK2SECTOR);
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
listnode *bgclock_select_victim(struct cache_manager *c, listnode *destage_ptr){
	struct bglru_node *bn;

	while(1){

		if(destage_ptr == c->cm_head)
			destage_ptr = destage_ptr->prev;

		bn = (struct bglru_node *)(destage_ptr->data);
		if(!bn->bg_recency){				
			break;
		}
		bn->bg_recency = 0;
		destage_ptr = destage_ptr->prev;
	}

	return destage_ptr;
}


listnode *bgclock_replace(struct cache_manager *c, int watermark){	
	listnode *destage_ptr;
	listnode *remove_ptr;
	struct bglru_node *victim = NULL;

	if(c->cm_free < watermark + 1){
		destage_ptr = c->cm_destage_ptr;
		destage_ptr = bgclock_select_victim(c, destage_ptr);

		remove_ptr = destage_ptr;
		c->cm_destage_ptr = destage_ptr->prev;

		victim = CACHE_REMOVE(c, remove_ptr);
		bgclock_evict_blocks(c, (struct bglru_node *)victim);

		free(victim);
		victim = NULL;
	}

	return (listnode *)victim;
}


int bgclock_inc(struct cache_manager *c, int inc_val){

	if((c->cm_size+inc_val) < (c->cm_max)){
		c->cm_size+=inc_val;
		c->cm_free+=inc_val;
		return inc_val;
	}else{
		return 0;
	}
}

int bgclock_dec(struct cache_manager *c, int dec_val){
	if((c->cm_size-dec_val) > 0){
		c->cm_size-=dec_val;
		c->cm_free-=dec_val;
		return dec_val;
	}else{
		return 0;
	}



}
void bgclock_make_reqlist(struct cache_manager *c, listnode *req_list, Rb_node *rbtree, int blk){
	//ll_insert_at_sort(req_list,(void *)blk, compare_blkno);
	ll_insert_at_tail(req_list, (void *)blk);
	//RBTreeInsert(rbtree, blk, 0);
	//rb_insert_a()
}
void bgclock_release_reqlist(struct cache_manager *c, listnode *req_list){

	while(ll_get_size(req_list)){
		ll_release_tail(req_list);
	}
}


void bgclock_init(struct cache_manager **c,char *name, int size,int max_sz, int high, int low){
	*c = (struct cache_manager *)malloc(sizeof(struct cache_manager));
	if(*c == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(1);
	}
	memset(*c, 0x00, sizeof(struct cache_manager));

	(*c)->cache_open = bgclock_open;
	(*c)->cache_close = bgclock_close;
	(*c)->cache_presearch = bgclock_presearch;
	(*c)->cache_search = bgclock_search;
	(*c)->cache_replace = bgclock_replace;
	(*c)->cache_remove = bgclock_remove;
	(*c)->cache_insert = bgclock_insert;
	(*c)->cache_inc = bgclock_inc;
	(*c)->cache_dec = bgclock_dec;
	(*c)->cache_alloc = bgclock_alloc;
	(*c)->cache_makerq = bgclock_make_reqlist;
	(*c)->cache_releaserq = bgclock_release_reqlist;

	CACHE_OPEN((*c), size, max_sz);

	(*c)->cm_name = (char *)malloc(strlen(name)+1);
	strcpy((*c)->cm_name, name);


	(*c)->cm_highwater = high;
	(*c)->cm_lowwater = low;
}



void bgclock_make_req(struct cache_manager *c, int blkno, int is_read,int dram_hit,int seq_io){
	struct bglru_node *bn = NULL;
	struct lru_node *ln = NULL;	
	listnode *gnode = NULL, *node = NULL;
	unsigned int groupno = blkno/GRP2BLK;
	int group_alloc = 0, node_alloc = 0;

	c->cm_ref++;

	gnode = CACHE_SEARCH(c, blkno);
	if(gnode){ /* hit  in group*/
		bn = (struct bglru_node *)gnode->data;

		/* Set recency bit */
		bn->bg_recency = 1;

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

	if(seq_io){
		if(node){ 
			//printf(" check code ... \n");
		//	//invalidate 			
		//	cn = CACHE_REMOVE(flash_cache, node);
			ll_release_node(bn->bg_block_hash[blkno%BG_HASH_NUM], ln->cn_hash);
			ll_release_node(bn->bg_block_list, ln->cn_node);

			ssd_trim_command(SSD, ln->cn_ssd_blk * BLOCK2SECTOR);
			reverse_map_release_blk(ln->cn_ssd_blk);
			free(ln);
			
			bn->block_count--;
			c->cm_free++;
			c->cm_count--;
		}

		if(is_read){
			CACHE_MAKERQ(c, c->cm_hddrq, NULL, blkno);
		}else{
			CACHE_MAKERQ(c, c->cm_hddwq, NULL, blkno);
		}
		return;
	}


	if(node_alloc){
		
		if(c->cm_free <= c->cm_lowwater){
			if(is_read && c->cm_policy == CACHE_BGCLOCK_RW){
				lru_stage(c);				
				lru_ssd_read(c, SSD, READ, c->cm_ssdrq, 0);
			}

			while(c->cm_free < c->cm_highwater){
				CACHE_REPLACE(c, c->cm_highwater);
			}			
			
			if(is_read && c->cm_policy == CACHE_BGCLOCK_RW){
				lru_destage(c);
			}
		}

		
		if(group_alloc){
			/* Alloc new group node */
			bn = CACHE_ALLOC(c, NULL, groupno);
			CACHE_INSERT(c, bn);			
		}
		
		ln = bgclock_insert_block_to_group(c, bn, blkno, NULL);	

		/*  Staging  Read ReQ */
		if(is_read){
			CACHE_MAKERQ(c, c->cm_hddrq, NULL, blkno);
			CACHE_MAKERQ(c, c->cm_ssdwq, NULL, ln->cn_ssd_blk);
			c->cm_stage_count++;
		}

	}
	if(!is_read){
		CACHE_MAKERQ(c, c->cm_ssdwq, NULL, ln->cn_ssd_blk);
		ln->cn_dirty = 1;
	}else{
		if(!dram_hit)
			CACHE_MAKERQ(c, c->cm_ssdrq, NULL, ln->cn_ssd_blk);
	}

	return;
}
int bgclock_main(){	
	struct cache_manager *bglru_manager;	
	unsigned int blkno = RND(700000);
	int i;


	bgclock_init(&bglru_manager,"BGCLOCK", 50000, 50000, 1024, 0);

	for(i =0;i < 1000000;i++){				
		blkno = RND(700000);

		bgclock_make_req(bglru_manager, blkno, READ, 0, 1);
	}	

	CACHE_CLOSE(bglru_manager);

	return 0;
}