#include <stdio.h>
#include "cache.h"
#include "ssd_utils.h"
#include "wow_driver.h"


extern int SEQ_COUNT;


void seq_detect_evict_blocks(struct cache_manager *c, struct seq_node *bn);


void seq_detect_open(struct cache_manager *c,int cache_size, int cache_max){
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

void seq_detect_close(struct cache_manager *c){
	listnode *node = c->cm_head->next;
	struct seq_node *bn;
	int i;

	//fprintf(stdout, " %s hit ratio = %f\n",c->cm_name, (float)c->cm_hit/c->cm_ref);

	while(node != c->cm_head && node){		
		bn = (struct seq_node *)node->data;

		seq_detect_evict_blocks(c, (struct seq_node *)bn);

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


static int seq_detect_compare_gno(const void *a,const void *b){
	if((unsigned int)(((struct seq_node *)a)->sq_no) == (unsigned int)b)
		return 1;	
	return 0;
}




listnode *seq_detect_presearch(struct cache_manager *c, unsigned int blkno){
	struct seq_node *bn = NULL;
	struct lru_node *ln = NULL;	
	listnode *gnode, *node;
	unsigned int groupno = blkno/SEQ_COUNT;
	int group_alloc = 0, node_alloc = 0;

	//c->cm_ref++;

	gnode = CACHE_SEARCH(c, blkno);
	if(gnode){ /* hit  in group*/
		bn = (struct seq_node *)gnode->data;

		/* Move to MRU position*/
		//bn = CACHE_REMOVE(c, gnode);
		//CACHE_INSERT(c, bn);

		node = ll_find_node(bn->sq_block_hash[blkno%BG_HASH_NUM], (void *)blkno, compare_hdd2);
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

listnode *seq_detect_search(struct cache_manager *c, unsigned int blkno){
	listnode *gnode;
	listnode *head;
	int groupno = blkno/SEQ_COUNT;

	head = c->cm_hash[groupno%HASH_NUM];
	gnode = ll_find_node(head,(void *)groupno, seq_detect_compare_gno);	
	if(gnode){ /* hit  in group*/
		return gnode;
	}

	return NULL;
}



void *seq_detect_remove(struct cache_manager *c, listnode *remove_ptr){
	struct seq_node *bn;
	listnode *victim;

	bn = (struct seq_node *)remove_ptr->data;

	// Release Node 
	ll_release_node(c->cm_head, bn->sq_node);

	// Release Hash Node		
	ll_release_node(c->cm_hash[bn->sq_no%HASH_NUM], bn->sq_hash);

	//victim  = (listnode *)ln->cn_blkno;

	//	free(ln);
	//c->cm_free++;
	//c->cm_count--;
	c->cm_groupcount--;

	bn->sq_node = NULL;
	bn->sq_hash = NULL;

	return (void *)bn;

}


void *seq_detect_alloc(struct seq_node *bn, unsigned int groupno){
	int i;

	if(bn == NULL){
		bn = (struct seq_node *)malloc(sizeof(struct seq_node));
		if(bn == NULL){
			fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
			exit(1);
		}
		memset(bn, 0x00, sizeof(struct seq_node));
	}

	bn->sq_no = groupno;


	ll_create(&bn->sq_block_list);

	for(i = 0;i <BG_HASH_NUM;i++){
		ll_create(&bn->sq_block_hash[i]);
	}

	return bn;
}



struct lru_node *seq_detect_insert_block_to_group(struct cache_manager *c, struct seq_node *bn,int blk, struct lru_node *ln){
	//struct lru_node *ln;

	if(ln == NULL){
		ln = (struct lru_node *)lru_alloc(NULL, blk);
//		ln->cn_ssd_blk = reverse_map_alloc_blk(blk);
	}

	ln->cn_node = ll_insert_at_sort(bn->sq_block_list, ln, compare_hdd);	
	bn->sq_block_count++;
	ln->cn_hash = ll_insert_at_tail(bn->sq_block_hash[blk%BG_HASH_NUM], ln);

	c->cm_free--;
	c->cm_count++;

	return ln;
}

void seq_detect_insert(struct cache_manager *c,struct seq_node *bn){

	// insert node to next of destage ptr 
	bn->sq_node = (listnode *)ll_insert_at_next(c->cm_head, c->cm_destage_ptr,(void *)bn);
	//c->cm_free--;
	c->cm_groupcount++;

	// insert node to hash
	bn->sq_hash = (listnode *)ll_insert_at_head(c->cm_hash[(bn->sq_no)%HASH_NUM],(void *)bn);

}

//extern int destage_count;

void seq_detect_evict_blocks(struct cache_manager *c, struct seq_node *bn){
	struct lru_node *ln;
	listnode *node;	
	int i;

	node = bn->sq_block_list->prev;

	while(bn->sq_block_count){
		ln = (struct lru_node *)node->data;

		if(ln->cn_dirty){ 
			//CACHE_MAKERQ(c, c->cm_ssdrq, NULL, ln->cn_ssd_blk);
			//CACHE_MAKERQ(c, c->cm_hddwq, NULL, reverse_get_blk(ln->cn_ssd_blk));
			c->cm_destage_count++;
		}	
		//reverse_map_release_blk(ln->cn_ssd_blk);

		free(ln);

		bn->sq_block_count--;
		c->cm_free++;
		c->cm_count--;
		NODE_PREV(node);
	}


	ll_release(bn->sq_block_list);

	for(i = 0;i < BG_HASH_NUM;i++){
		ll_release(bn->sq_block_hash[i]);
	}

}

void seq_detect_make_seq_req(struct cache_manager *c, struct cache_manager *r_clock, struct cache_manager *w_clock, struct seq_node *bn){
	struct lru_node *ln;
	struct lru_node *rm_node;
	listnode *node;	
	listnode *lru_list;
	int i;

	node = bn->sq_block_list->prev;
	if(bn->sq_dirty_count!= 0 && bn->sq_dirty_count != SEQ_COUNT)
		bn = bn;
	while(bn->sq_block_count){
		int blkno;
		ln = (struct lru_node *)node->data;

		blkno = ln->cn_blkno;
		
#if 0 
		if(!bn->sq_dirty_count){

			/* Read */
			lru_list = CACHE_PRESEARCH(r_clock, blkno);
			if(lru_list){
				rm_node = CACHE_REMOVE(r_clock, lru_list);
				reverse_map_release_blk(rm_node->cn_ssd_blk);
				ssd_trim_command(SSD, rm_node->cn_ssd_blk * BLOCK2SECTOR);
				free(rm_node);
			}else{
				CACHE_MAKERQ(r_clock, r_clock->cm_hddrq, NULL, blkno);
			}
		}else{

			/* Write */
			lru_list = CACHE_PRESEARCH(w_clock, blkno);
			if(lru_list){		

				rm_node = CACHE_REMOVE(w_clock, lru_list);			
				
				CACHE_MAKERQ(w_clock, w_clock->cm_ssdrq, NULL, rm_node->cn_ssd_blk);
				CACHE_MAKERQ(w_clock, w_clock->cm_hddwq, NULL, reverse_get_blk(rm_node->cn_ssd_blk));
				
				reverse_map_release_blk(rm_node->cn_ssd_blk);
				free(rm_node);

			}else{			
				CACHE_MAKERQ(w_clock, w_clock->cm_hddwq, NULL, blkno);
			}		

		}
#endif 

		free(ln);

		bn->sq_block_count--;
		c->cm_free++;
		c->cm_count--;
		NODE_PREV(node);
	}

	


	ll_release(bn->sq_block_list);

	for(i = 0;i < BG_HASH_NUM;i++){
		ll_release(bn->sq_block_hash[i]);
	}

}



listnode *seq_detect_replace(struct cache_manager *c, int watermark){	
	listnode *destage_ptr;
	listnode *remove_ptr;
	struct seq_node *victim = NULL;

	if(c->cm_free < watermark + 1){
		remove_ptr = c->cm_head->prev;
		c->cm_destage_ptr = c->cm_head;
		victim = CACHE_REMOVE(c, remove_ptr);
		seq_detect_evict_blocks(c, (struct seq_node *)victim);

		free(victim);
		victim = NULL;
	}

	return (listnode *)victim;
}


int seq_detect_inc(struct cache_manager *c, int inc_val){

	if((c->cm_size+inc_val) < (c->cm_max)){
		c->cm_size+=inc_val;
		c->cm_free+=inc_val;
		return inc_val;
	}else{
		return 0;
	}
}

int seq_detect_dec(struct cache_manager *c, int dec_val){
	if((c->cm_size-dec_val) > 0){
		c->cm_size-=dec_val;
		c->cm_free-=dec_val;
		return dec_val;
	}else{
		return 0;
	}



}
void seq_detect_make_reqlist(struct cache_manager *c, listnode *req_list, Rb_node *rbtree, int blk){	
	ll_insert_at_tail(req_list, (void *)blk);	
}
void seq_detect_release_reqlist(struct cache_manager *c, listnode *req_list){

	while(ll_get_size(req_list)){
		ll_release_tail(req_list);
	}
}


void seq_detect_init(struct cache_manager **c,char *name, int size,int max_sz, int high, int low){
	*c = (struct cache_manager *)malloc(sizeof(struct cache_manager));
	if(*c == NULL){
		fprintf(stderr, " Malloc Error %s %d \n",__FUNCTION__,__LINE__);
		exit(1);
	}
	memset(*c, 0x00, sizeof(struct cache_manager));

	(*c)->cache_open = seq_detect_open;
	(*c)->cache_close = seq_detect_close;
	(*c)->cache_presearch = seq_detect_presearch;
	(*c)->cache_search = seq_detect_search;
	(*c)->cache_replace = seq_detect_replace;
	(*c)->cache_remove = seq_detect_remove;
	(*c)->cache_insert = seq_detect_insert;
	(*c)->cache_inc = seq_detect_inc;
	(*c)->cache_dec = seq_detect_dec;
	(*c)->cache_alloc = seq_detect_alloc;
	(*c)->cache_makerq = seq_detect_make_reqlist;
	(*c)->cache_releaserq = seq_detect_release_reqlist;

	CACHE_OPEN((*c), size, max_sz);

	(*c)->cm_name = (char *)malloc(strlen(name)+1);
	strcpy((*c)->cm_name, name);


	(*c)->cm_highwater = high;
	(*c)->cm_lowwater = low;
}



int seq_detect_make_req(struct cache_manager *c, struct cache_manager *r_clock, struct cache_manager *w_clock, int blkno, int is_read, double req_time){
	struct seq_node *bn = NULL;
	struct lru_node *ln = NULL;	
	listnode *gnode, *node;
	unsigned int groupno = blkno/SEQ_COUNT;
	int group_alloc = 0, node_alloc = 0;

	c->cm_ref++;

	gnode = CACHE_SEARCH(c, blkno);
	if(gnode){ /* hit  in group*/
		bn = (struct seq_node *)gnode->data;

		bn->sq_ref++;
		/* Move to MRU position*/
		bn = CACHE_REMOVE(c, gnode);
		CACHE_INSERT(c, bn);

		node = ll_find_node(bn->sq_block_hash[blkno%BG_HASH_NUM], (void *)blkno, compare_hdd2);
		if(node){ 
			/* Hit in group node*/				
			c->cm_hit++;				
			node_alloc = 0;
			group_alloc = 0;

			bn->sq_hit++;

			ln = (struct lru_node *)node->data;
			if(!is_read){		
				if(!ln->cn_dirty){
					bn->sq_dirty_count++;
					ln->cn_dirty = 1;
				}
				
				
			}
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
			bn->sq_ref++;
			bn->sq_start_time = req_time;
			CACHE_INSERT(c, bn);			
		}
		ln = seq_detect_insert_block_to_group(c, bn, blkno, NULL);	
		if(!is_read){		
			ln->cn_dirty = 1;
			bn->sq_dirty_count++;
		}
		if(bn->sq_block_count == SEQ_COUNT && (bn->sq_dirty_count == SEQ_COUNT || bn->sq_dirty_count == 0)){
			
			//if(bn->sq_hit*100/bn->sq_ref > 1 || req_time - bn->sq_start_time > (double)5000)
			//	return 0;
			if((ln->cn_blkno+1) % (SEQ_COUNT) != 0)
				return 0;

			// sequential은 짧은 시간에 연속적으로 페이지들이 채워져야 한다.
			// 그러나 위와 같은 방식은 장시간에 따라서 블록에 페이지들이 채워질 수 있다.
			// 이것은 시퀀셜이라고 하기는 문제가 있다. 
			//if(bn->sq_dirty_count == SEQ_COUNT)
			//	fprintf(stdout, " Write Sequential pattern is detected %d...\n", bn->sq_no);
			//else
			//	fprintf(stdout, " Read Sequential pattern is detected %d...\n", bn->sq_no);


			//printf(" q size = %d, %d, %d", ll_get_size(w_clock->cm_ssdrq), ll_get_size(w_clock->cm_ssdwq), ll_get_size(w_clock->cm_hddwq));

#ifndef __linux
			printf(" Sequential Detected block(%.3f) hit ratio = %d, hit %d, ref %d, max = %d\n", req_time - bn->sq_start_time, bn->sq_hit*100/bn->sq_ref, bn->sq_hit, bn->sq_ref, SEQ_COUNT);
#endif 
#if 1 
			if(is_read){
				/* Staging */			
				lru_stage(r_clock);
				/* SSD Read */
				//lru_flush_reqlist(r_clock, SSD, READ, r_clock->cm_ssdrq, 0);
				lru_ssd_read(r_clock, SSD, READ, r_clock->cm_ssdrq, 0);
			}else{
				/* Destaging */			
				lru_destage(w_clock);
				/* SSD Write */
				//lru_flush_reqlist(w_clock, SSD, WRITE, w_clock->cm_ssdwq, 0);
				lru_ssd_write(w_clock, SSD, WRITE, w_clock->cm_ssdwq, 0);
			}
#endif 


			bn = CACHE_REMOVE(c, bn->sq_node);
			
			if(req_time - bn->sq_start_time < 5000)
				req_time = req_time;

			//printf(" Delayed time = %.3f\n", req_time - bn->sq_start_time);


			seq_detect_make_seq_req(c, r_clock, w_clock, (struct seq_node *)bn);
			free(bn);

			return 1;
		}
	}


	return 0;
}
int seq_detect_main(){	
	struct cache_manager *seq_detector;	
	unsigned int blkno = RND(700000);
	int i;


	seq_detect_init(&seq_detector,"SEQ_DETECTOR", 50000, 50000, 1024, 0);

	for(i =0;i < 1000000;i++){				
		blkno = RND(700000);

//		seq_detect_make_req(seq_detector, blkno, READ);		
	}	

	CACHE_CLOSE(seq_detector);

	return 0;
}