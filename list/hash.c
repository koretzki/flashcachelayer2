#include <stdio.h>
#include <stdlib.h>

#include "list.h"

struct task_t {
	struct list_head list;
	struct hlist_node hash;
	int num;
};

#define HASH 6
#define NUM 100

struct task_t *find (struct hlist_head *head, int num ) {
	struct hlist_node *node;
	struct task_t *t;

	hlist_for_each( node , head) {
		t = hlist_entry(node, struct task_t, hash);
		if ( t->num == num )
			return t;
	}

	return NULL;

}

int main () {
	struct hlist_head hash_table[HASH]; 
	LIST_HEAD(list);
	struct task_t task[NUM], *t;
	struct list_head *ptr;
	int i;

	for ( i = 0; i < HASH; i ++ ) {
		INIT_HLIST_HEAD ( &hash_table[i] );
	}
	
	for (i = 0; i < NUM; i ++ ) {
		task[i].num = i;
		list_add ( &task[i].list , &list);
		hlist_add_head (&task[i].hash, &hash_table[i%HASH] );
	}

	list_for_each( ptr, &list ) {
		t = list_entry ( ptr, struct task_t, list );
		printf( " blkno = %d \n", t->num ); 

	}


	for ( i = 0; i < NUM; i ++ ) {
		struct task_t *t = find ( &hash_table[i%HASH], i + 100) ;
		if ( t ) 
			printf (" found = %d \n", t->num);
		else 
			printf (" not found = %d \n", NULL );
	}
	
	return 0;
}
