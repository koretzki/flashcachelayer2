    
/*
* Flash Cache Layer (FCL) (Version 1.0) 
*
* Author		: Yongseok Oh (ysoh@uos.ac.kr)
* Date			: 18/06/2012  
* Description	: The header file 
* File Name		: disksim_fcl_cache.h
*/

#include "../ssdmodel/ssd_utils.h"
#include "list.h"

#ifndef _FCL_CACHE_H
#define _FCL_CACHE_H  


#define HASH_NUM (1024*1024)
//#define BG_HASH_NUM (1024)
//#define GRP2BLK 1024

#define FCL_CACHE_FLAG_FILLING 1 
#define FCL_CACHE_FLAG_SEALED 2  

struct lru_node{
	
	struct list_head cn_list;
	struct list_head cn_clean_list;
	struct list_head cn_dirty_list;
	struct hlist_node cn_hash;

	unsigned int cn_blkno;
	int cn_ssd_blk;
//	int cn_ssd_sector;

	int cn_dirty;
	int cn_read;
	int cn_flag;
	unsigned int cn_recency;
	unsigned int cn_frequency;

	void *cn_temp1;
	void *cn_temp2;

	double cn_time;

};

struct cache_manager{

 //listnode *cm_head;
 //listnode **cm_hash;

 struct list_head cm_head;
 struct list_head cm_dirty_head;
 struct list_head cm_clean_head;
 struct hlist_head *cm_hash;

 //listnode *cm_destage_ptr;
 struct list_head *cm_destage_ptr;
 unsigned int cm_hit;
 unsigned int cm_miss;
 unsigned int cm_ref;
 unsigned int cm_read_hit;
 unsigned int cm_read_ref;

 int cm_size; 
 int cm_free;
 int cm_count;
 int cm_max;
 int cm_min;
 char *cm_name;

 int cm_dirty_size;
 int cm_clean_size;

 int cm_dirty_free;
 int cm_clean_free;

 int cm_dirty_count;
 int cm_clean_count;

 int cm_destage_count;
 int cm_stage_count;

 int cm_groupcount;
 int cm_lowwater;
 int cm_highwater;
 int cm_policy;

 void (*cache_open)(struct cache_manager *cache,int cache_size, int cache_max);
 void (*cache_close)(struct cache_manager *cache);
 struct lru_node *(*cache_presearch)(struct cache_manager *cache, unsigned int blkno);
 struct lru_node *(*cache_search)(struct cache_manager *cache,unsigned int blkno);
 void *(*cache_replace)(struct cache_manager *cache,int w, int dirty); 
 void *(*cache_remove)(struct cache_manager *cache, struct lru_node *ln); 
 void (*cache_move_mru)(struct cache_manager *cache, struct lru_node *ln); 
 void (*cache_insert)(struct cache_manager *cache, struct lru_node *node);
 void *(*cache_alloc)(struct lru_node *node, unsigned int blkno);
 int (*cache_inc)(struct cache_manager *cache, int i);
 int (*cache_dec)(struct cache_manager *cache, int i);
 void (*cache_print)(struct cache_manager *cache, FILE *fp);
};



#if 0 
struct ghost_node{
	listnode *gn_node;
	listnode *gn_hash;
	unsigned int gn_blkno;	
	int gn_ssd_blk;
	int gn_dirty;
	unsigned int gn_recency;
	unsigned int gn_frequency;
};

struct clock_node{
	listnode *cn_node;
	listnode *cn_hash;
	unsigned int cn_blkno;
	int cn_ssd_blk;		
	int cn_dirty;
	int cn_read;
	unsigned int cn_recency;
	unsigned int cn_frequency;
};
#endif 


#if 0
struct bglru_node{
	listnode *bg_node;
	listnode *bg_hash;
	unsigned int bg_no;	/* block group number */
	int block_count;
	int bg_recency;
	listnode *bg_block_list;
	listnode *bg_block_hash[BG_HASH_NUM];
};

struct seq_node{
	listnode *sq_node;
	listnode *sq_hash;
	unsigned int sq_no;	/* block group number */
	int sq_block_count;
	int sq_dirty_count;
	int sq_ref;
	int sq_hit;
	double sq_start_time;
	listnode *sq_block_list;
	listnode *sq_block_hash[BG_HASH_NUM];
};
#endif 

#define CACHE_OPEN(c, sz, m) c->cache_open((struct cache_manager *)c, sz, m)
#define CACHE_CLOSE(c ) c->cache_close((struct cache_manager *)c)
#define CACHE_PRESEARCH(c, p) c->cache_presearch((struct cache_manager *)c, p)
#define CACHE_SEARCH(c, p) c->cache_search((struct cache_manager *)c, p)
#define CACHE_REPLACE(c, w, d) c->cache_replace((struct cache_manager *)c, w, d)
#define CACHE_MOVEMRU(c, w) c->cache_move_mru((struct cache_manager *)c, w)
#define CACHE_REMOVE(c, p) c->cache_remove((struct cache_manager *)c, p)
#define CACHE_INSERT(c, p) c->cache_insert((struct cache_manager *)c, p)
#define CACHE_ALLOC(c, n, p) c->cache_alloc(n, p)
#define CACHE_PRINT(c, p) c->cache_print((struct cache_manager *)c, p)

//#define CACHE_INC(c, i) c->cache_inc((struct cache_manager *)c, i)
//#define CACHE_DEC(c, i) c->cache_dec((struct cache_manager *)c, i)
//#define CACHE_HIT(c) ((float)c->cm_hit/c->cm_ref)
//#define CACHE_MAKERQ(c, p, r, i) c->cache_makerq((struct cache_manager *)c, p, r, i)
//#define CACHE_RELEASERQ(c, p) c->cache_releaserq((struct cache_manager *)c, p)
//#define CACHE_FLUSHRQ(c, d, r, l) c->cache_flushrq((struct cache_manager *)c, d, r, l)



void lru_open(struct cache_manager *c,int cache_size, int cache_max);
void lru_close(struct cache_manager *c);
struct lru_node *lru_presearch(struct cache_manager *c, unsigned int blkno);
struct lru_node *lru_search(struct cache_manager *c,unsigned int blkno);
void *lru_remove(struct cache_manager *c, struct lru_node *ln);
void *lru_alloc(struct lru_node *ln, unsigned int blkno);
void lru_insert(struct cache_manager *c,struct lru_node *ln);
void *lru_replace(struct cache_manager *c, int watermark, int dirty);	
//int lru_inc(struct cache_manager *c, int inc_val);
//int lru_dec(struct cache_manager *c, int dec_val);
void lru_init(struct cache_manager **c,char *name, int size,int max_sz,int high,int low);
void lru_move_clean_list ( struct cache_manager *c, struct lru_node *ln );

void lru_print ( struct cache_manager *c, FILE *fp) ;
struct lru_node *mlru_search(struct cache_manager **lru_manager,int lru_num, int blkno, int insert,int hit, int *hit_position);
void mlru_remove(struct cache_manager **lru_manager,int lru_num, int blkno);
struct cache_manager **mlru_init(char *name,int lru_num, int total_size);
void mlru_exit(struct cache_manager **lru_manager,int lru_num);
void lru_set_dirty_size ( struct cache_manager *c, int dirty_size, int clean_size ) ;

#endif 
