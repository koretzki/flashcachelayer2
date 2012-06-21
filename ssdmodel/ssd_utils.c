// DiskSim SSD support
// 2008 Microsoft Corporation. All Rights Reserved

#include "ssd_utils.h"

//////////////////////////////////////////////////////////////////////////////
//                 code for bit manipulation routines
//////////////////////////////////////////////////////////////////////////////


//clears a particular bit.
//pos 0 corresponds to msb
void ssd_clear_bit(unsigned char *c, int pos)
{
    int byte = pos / 8;
    int bit_pos = 7 - (pos % 8);

    c[byte] = (c[byte] & (~(0x1 << bit_pos)));

    return;
}


//sets a particular bit.
//pos 0 corresponds to msb and pos 7 corresponds
//to lsb.
void ssd_set_bit(unsigned char *c, int pos)
{
    int byte = pos / 8;
    int bit_pos = 7 - (pos % 8);

    c[byte] = (c[byte] | (0x1 << bit_pos));

    return;
}

//returns true if a bit is set
int ssd_bit_on(unsigned char *c, int pos)
{
    int byte = pos / 8;
    int bit_pos = 7 - (pos % 8);

    return (c[byte] & (0x1 << bit_pos));
}

//finds the position of the first zero-th
//bit in an unsigned char array. the positions are
//numbered starting from 0 from msb to lsb. returns -1
//if all the bits are already set. 'total' specifies the
//number of bits to consider in the array. 'start' gives
//the location from which to start the search.
int ssd_find_zero_bit(unsigned char *c, int total, int start)
{
    int bit = start;
    int temp = total;

    while (temp) {
        int byte = bit / 8;
        if ((c[byte] >> (7 - (bit % 8))) & 0x1) {
            bit = (bit + 1) % total;
            temp --;
        } else {
            return bit;
        }
    }

    return -1;
}

//////////////////////////////////////////////////////////////////////////////
//             adding some code for a linked list module
//////////////////////////////////////////////////////////////////////////////

void ll_create(listnode **start)
{
    *start = (listnode *)malloc(sizeof(listnode));
    memset(*start, 0, sizeof(listnode));

    (*start)->data = (header_data *)malloc(sizeof(header_data));
    ((header_data *)(*start)->data)->size = 0;
}

// Free all the nodes in the list.
void ll_release(listnode *start)
{
    free(start->data);

    while (start) {
        listnode *next = start->next;

        if (next) {
            next->prev = NULL;

            if (next == start) {
                next = NULL;
            }
        }

        if (start->prev) {
            start->prev->next = NULL;
        }

        /* we just release the node. we don't release the
         * data that is contained in the node. that is the
         * responsibility of the function that is using
         * this linked list */
        free(start);

        start = next;
    }

    return;
}

listnode *_ll_insert_at_tail(listnode *start, listnode *toinsert)
{
    if ((!start) || (!toinsert)) {
        fprintf(stderr, "Error: invalid value to _ll_insert_at_tail\n");
        exit(-1);
    } else {
        if (start->prev) {
            listnode *prevnode = start->prev;

            toinsert->next = prevnode->next;
            prevnode->next->prev = toinsert;
            prevnode->next = toinsert;
            toinsert->prev = prevnode;
        } else {
            start->prev = toinsert;
            start->next = toinsert;
            toinsert->prev = start;
            toinsert->next = start;
        }

        return toinsert;
    }
}



listnode *_ll_insert_at_head(listnode *start, listnode *toinsert)
{
    if ((!start) || (!toinsert)) {
        fprintf(stderr, "Error: invalid value to _ll_insert_at_head\n");
        exit(-1);
    } else {
        if (start->next) {
            listnode *nextnode = start->next;

            toinsert->prev = nextnode->prev;
            nextnode->prev->next = toinsert;
            nextnode->prev = toinsert;
            toinsert->next = nextnode;
        } else {
            start->next = toinsert;
            start->prev = toinsert;
            toinsert->next = start;
            toinsert->prev = start;
        }

        return toinsert;
    }
}

/*
 * Insert the data at the tail.
 */
listnode *ll_insert_at_tail(listnode *start, void *data)
{
    /* allocate a new node */
    listnode *newnode = malloc(sizeof(listnode));
    newnode->data = data;

    /* increment the number of entries */
    ((header_data *)(start->data))->size ++;

    return _ll_insert_at_tail(start, newnode);
}




/*
 * Insert the data at the head.
 */
listnode *ll_insert_at_head(listnode *start, void *data)
{
    /* allocate a new node */
    listnode *newnode = malloc(sizeof(listnode));
    newnode->data = data;

    /* increment the number of entries */
    ((header_data *)(start->data))->size ++;

    return _ll_insert_at_head(start, newnode);
}

/*
 * Release a node from the linked list.
 */
void ll_release_node(listnode *start, listnode *node)
{
    if ((!node) || (!start)) {
        fprintf(stderr, "Error: invalid items on the list\n");
        exit(-1);
    } else {
        ((header_data *)(start->data))->size --;

		//printf(" size %d --\n", ((header_data *)(start->data))->size);

        if (((header_data *)(start->data))->size > 0) {
            listnode *nodesprev = node->prev;

            nodesprev->next = node->next;
            node->next->prev = nodesprev;
        } else {
            /* do some sanity checking */

            /* there is just one element left in the list
             * and we want to release it. so, check if it
             * matches the prev and next of the start node. */
            if ((start->next != node) || (start->prev != node)) {
                fprintf(stderr, "Error: sanity check failed\n");
                exit(-1);
            }

            /* this is the last element - just release it */
            start->next = NULL;
            start->prev = NULL;
        }

        /* we just release the node. we don't release the
         * data that is contained in the node. that is the
         * responsibility of the function that is using
         * this linked list */
        free(node);
    }
}

/*
 * Release the tail node from the linked list.
 */
void ll_release_tail(listnode *start)
{
    listnode *tail = start->prev;

    if ((!tail) || (tail == start)) {
        fprintf(stderr, "Warning: the list is empty. no element to release\n");
        return;
    } else {
        ll_release_node(start, tail);
    }
}

listnode *ll_get_tail(listnode *start)
{
    return (start->prev->data);
}

listnode *ll_get_head(listnode *start)
{
	return (start->next->data);
}

/*
 * Return the total number of nodes in the list.
 */
int ll_get_size(listnode *start)
{
    return ((header_data *)(start->data))->size;
}

/*
 * For debugging purposes.
 * We can make some improvements on performance,
 * for e.g., by searching from the tail if n is
 * greater than size/2.
 */
listnode *ll_get_nth_node(listnode *start, int n)
{
    int i;
    int reverse;
    listnode *node;

    if (n >= ll_get_size(start)) {
        fprintf(stderr, "Error: n (%d) is greater than size %d\n",
            n, ll_get_size(start));
        return NULL;
    }

    if (n > ll_get_size(start)/2) {
        i = ll_get_size(start) - 1;
        reverse = 1;
        node = start->prev;
    } else {
        i = 0;
        reverse = 0;
        node = start->next;
    }

    /* go over each node and print it */
    while ((node) && (node != start)) {
        if (i == n) {
            return node;
        }

        if (reverse) {
            node = node->prev;
            i --;
        } else {
            node = node->next;
            i ++;
        }
    }

    fprintf(stderr, "Error: cannot find the %d node in list\n", n);
    return NULL;
}


listnode *ll_find_data(listnode *start,void *data){
	int size = ((header_data *)(start->data))->size;
	listnode *p = start->next;

	if(p == NULL)
		return 0;

	while(start!=p){

		if(p->data == data)
			return p;

		p = p->next;
	}

	return NULL;
}






/* A C-program for MT19937: Integer version (1999/10/28)          */
/*  genrand() generates one pseudorandom unsigned integer (32bit) */
/* which is uniformly distributed among 0 to 2^32-1  for each     */
/* call. sgenrand(seed) sets initial values to the working area   */
/* of 624 words. Before genrand(), sgenrand(seed) must be         */
/* called once. (seed is any 32-bit integer.)                     */
/*   Coded by Takuji Nishimura, considering the suggestions by    */
/* Topher Cooper and Marc Rieffel in July-Aug. 1997.              */

/* This library is free software; you can redistribute it and/or   */
/* modify it under the terms of the GNU Library General Public     */
/* License as published by the Free Software Foundation; either    */
/* version 2 of the License, or (at your option) any later         */
/* version.                                                        */
/* This library is distributed in the hope that it will be useful, */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of  */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.            */
/* See the GNU Library General Public License for more details.    */
/* You should have received a copy of the GNU Library General      */
/* Public License along with this library; if not, write to the    */
/* Free Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA   */ 
/* 02111-1307  USA                                                 */

/* Copyright (C) 1997, 1999 Makoto Matsumoto and Takuji Nishimura. */
/* Any feedback is very welcome. For any question, comments,       */
/* see http://www.math.keio.ac.jp/matumoto/emt.html or email       */
/* matumoto@math.keio.ac.jp                                        */

/* REFERENCE                                                       */
/* M. Matsumoto and T. Nishimura,                                  */
/* "Mersenne Twister: A 623-Dimensionally Equidistributed Uniform  */
/* Pseudo-Random Number Generator",                                */
/* ACM Transactions on Modeling and Computer Simulation,           */
/* Vol. 8, No. 1, January 1998, pp 3--30.                          */

/* Period parameters */  
#define N 624
#define M 397
#define MATRIX_A 0x9908b0df   /* constant vector a */
#define UPPER_MASK 0x80000000 /* most significant w-r bits */
#define LOWER_MASK 0x7fffffff /* least significant r bits */

/* Tempering parameters */   
#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000
#define TEMPERING_SHIFT_U(y)  (y >> 11)
#define TEMPERING_SHIFT_S(y)  (y << 7)
#define TEMPERING_SHIFT_T(y)  (y << 15)
#define TEMPERING_SHIFT_L(y)  (y >> 18)

static unsigned long mt[N]; /* the array for the state vector  */
static int mti=N+1; /* mti==N+1 means mt[N] is not initialized */

/* Initializing the array with a seed */
void
sgenrand(seed)
unsigned long seed;	
{
	int i;

	for (i=0;i<N;i++) {
		mt[i] = seed & 0xffff0000;
		seed = 69069 * seed + 1;
		mt[i] |= (seed & 0xffff0000) >> 16;
		seed = 69069 * seed + 1;
	}
	mti = N;
}

/* Initialization by "sgenrand()" is an example. Theoretically,      */
/* there are 2^19937-1 possible states as an intial state.           */
/* This function allows to choose any of 2^19937-1 ones.             */
/* Essential bits in "seed_array[]" is following 19937 bits:         */
/*  (seed_array[0]&UPPER_MASK), seed_array[1], ..., seed_array[N-1]. */
/* (seed_array[0]&LOWER_MASK) is discarded.                          */ 
/* Theoretically,                                                    */
/*  (seed_array[0]&UPPER_MASK), seed_array[1], ..., seed_array[N-1]  */
/* can take any values except all zeros.                             */
void
lsgenrand(seed_array)
unsigned long seed_array[]; 
/* the length of seed_array[] must be at least N */
{
	int i;

	for (i=0;i<N;i++) 
		mt[i] = seed_array[i];
	mti=N;
}

unsigned long 
genrand()
{
	unsigned long y;
	static unsigned long mag01[2]={0x0, MATRIX_A};
	/* mag01[x] = x * MATRIX_A  for x=0,1 */

	if (mti >= N) { /* generate N words at one time */
		int kk;

		if (mti == N+1)   /* if sgenrand() has not been called, */
			sgenrand(4357); /* a default initial seed is used   */

		for (kk=0;kk<N-M;kk++) {
			y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
			mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[y & 0x1];
		}
		for (;kk<N-1;kk++) {
			y = (mt[kk]&UPPER_MASK)|(mt[kk+1]&LOWER_MASK);
			mt[kk] = mt[kk+(M-N)] ^ (y >> 1) ^ mag01[y & 0x1];
		}
		y = (mt[N-1]&UPPER_MASK)|(mt[0]&LOWER_MASK);
		mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[y & 0x1];

		mti = 0;
	}

	y = mt[mti++];
	y ^= TEMPERING_SHIFT_U(y);
	y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
	y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
	y ^= TEMPERING_SHIFT_L(y);

	return y; 
}


#define DEVIDE 10

#define SCALE_DOWN 5
#define SIZE 100
#define SIZE_DOWN (SIZE/SCALE_DOWN)
#define MKINDEX(k,i,j) (k*SIZE_DOWN*SIZE_DOWN + i*SIZE_DOWN+j)
#define MKINDEX2(k,i,j) (k*SIZE*SIZE + i*SIZE+j)





int wow_calc_belady(int csize1,int csize2, float hit1, float hit2,float *cp, float *kp){
	int i, j, k;
	float hit_ratio;
	int *array;
	float min_error = 1.0;
	int min_i = 0,min_j = 0;


	array = (int *)malloc(sizeof(int) * 2 * (SIZE/SCALE_DOWN) * SIZE/SCALE_DOWN);
	if(array == NULL){
		printf(" malloc error \n");
		exit(1);
	}

	for(k = 0;k < 2;k++){
		for(i = 1;i < SIZE_DOWN;i++){
			for(j  = 1;j  < SIZE_DOWN; j++){
				array[MKINDEX(k, i, j)] = 0;				
			}
		}

	}


	for(i = 1;i < SIZE;i++){
		for(j  = 1;j  < SIZE; j++){

			hit_ratio = BELADY(((float)i/DEVIDE), ((float)j/DEVIDE),csize1);

			if((int)(hit_ratio*100) == (int)(hit1*100)){				
				array[MKINDEX(0, i/SCALE_DOWN, j/SCALE_DOWN)] = 1;
			}
		}
	}


	for(i = 1;i < SIZE;i++){
		for(j  = 1;j  < SIZE; j++){

			hit_ratio = BELADY(((float)i/DEVIDE), ((float)j/DEVIDE),csize2);

			if((int)(hit_ratio*100) == (int)(hit2*100)){				
				array[MKINDEX(1, i/SCALE_DOWN, j/SCALE_DOWN)] = 1;
			}
		}
	}


	for(i = 1;i < SIZE_DOWN;i++){
		for(j  = 1;j  < SIZE_DOWN; j++){	
			if(array[MKINDEX(0, i, j)] && array[MKINDEX(1, i,j)]){				
				float err1, err2;
				//fprintf(stdout, "Found Control Values: c = %.2f k = %.2f %.2f %.2f\n", (float)i*SCALE_DOWN/DEVIDE, (float)j*SCALE_DOWN/DEVIDE, BELADY((float)i*SCALE_DOWN/DEVIDE, (float)j*SCALE_DOWN/DEVIDE, csize1), BELADY((float)i*SCALE_DOWN/DEVIDE, (float)j*SCALE_DOWN/DEVIDE, csize2));
				//fprintf(outputfile, "Found Control Values: c = %.2f k = %.2f %.2f %.2f\n", (float)i*SCALE_DOWN/DEVIDE, (float)j*SCALE_DOWN/DEVIDE, BELADY((float)i*SCALE_DOWN/DEVIDE, (float)j*SCALE_DOWN/DEVIDE, csize1), BELADY((float)i*SCALE_DOWN/DEVIDE, (float)j*SCALE_DOWN/DEVIDE, csize2));				

				err1 = BELADY((float)i*SCALE_DOWN/DEVIDE, (float)j*SCALE_DOWN/DEVIDE, csize1) - hit1;
				err1 = sqrt(err1*err1);
				err2 = BELADY((float)i*SCALE_DOWN/DEVIDE, (float)j*SCALE_DOWN/DEVIDE, csize2) - hit2;
				err2 = sqrt(err2*err2);

				if(min_error > err1+err2){
					min_error = err1 + err2;
					min_i = i;
					min_j = j;
				}
			}			
		}
	}

	if(min_i && min_j) {
		//fprintf(stdout, "Found Control Values: c = %.2f k = %.2f %.2f(%.2f) %.2f(%.2f)\n", (float)min_i*SCALE_DOWN/DEVIDE, (float)min_j*SCALE_DOWN/DEVIDE, BELADY((float)min_i*SCALE_DOWN/DEVIDE, (float)min_j*SCALE_DOWN/DEVIDE, csize1), hit1, BELADY((float)min_i*SCALE_DOWN/DEVIDE, (float)min_j*SCALE_DOWN/DEVIDE, csize2), hit2);
	}else{
		//fprintf(stdout, "It cannot find Control Value ...\n");
	}


	if(cp){
		*cp = (float)min_i*SCALE_DOWN/DEVIDE;
	}
	if(kp){
		*kp = (float)min_j*SCALE_DOWN/DEVIDE;
	}

	fflush(stdout);

	free(array);

	return (min_i && min_j);
}



listnode *ll_find_node(listnode *start,void *data, int (*CompFunc) (const void*,const void*)){
	int size = ((header_data *)(start->data))->size;
	listnode *p = start->next;
	//group_t *g;

	if(p == NULL)
		return 0;

	while(start!=p){

		if(CompFunc(p->data, data))//(int)g->groupno == (int)data
			return p;

		p = p->next;
	}

	return NULL;
}



listnode *_ll_insert_at_sort(listnode *start, listnode *toinsert, int (*CompFunc) (const void*,const void*)){
	listnode *g_new, *g_prev;
	listnode *temp;
	g_new = toinsert->data;

	if ((!start) || (!toinsert)) {
		fprintf(stderr, "Error: invalid value to _ll_insert_at_tail\n");
		exit(-1);
	} else {
		if (start->prev) {
			listnode *prevnode = start->prev;
			g_prev = prevnode->data;

			while(CompFunc(g_new, g_prev) > 0 && prevnode != start){
				prevnode = prevnode->prev;				
				g_prev= prevnode->data;	
			}

			toinsert->next = prevnode->next;
			prevnode->next->prev = toinsert;
			prevnode->next = toinsert;
			toinsert->prev = prevnode;
		} else {
			start->prev = toinsert;
			start->next = toinsert;
			toinsert->prev = start;
			toinsert->next = start;
		}

		return toinsert;
	}
}
listnode *ll_insert_at_sort(listnode *start, void *data, int (*comp_func) (const void*,const void*))
{
	/* allocate a new node */
	listnode *newnode = malloc(sizeof(listnode));
	newnode->data = data;

	/* increment the number of entries */
	((header_data *)(start->data))->size ++;

	return _ll_insert_at_sort(start, newnode, comp_func);
}

//
//listnode *_ll_insert_at_prev(listnode *start, listnode *toinsert){
//	listnode *g_new, *g_prev;
//	listnode *temp;
//	g_new = toinsert->data;
//
//	if ((!start) || (!toinsert)) {
//		fprintf(stderr, "Error: invalid value to _ll_insert_at_tail\n");
//		exit(-1);
//	} else {
//		if (start->prev) {
//			listnode *prevnode = start->prev;
//			g_prev = prevnode->data;
//			
//			toinsert->next = prevnode->next;
//			prevnode->next->prev = toinsert;
//			prevnode->next = toinsert;
//			toinsert->prev = prevnode;
//		} else {
//			start->prev = toinsert;
//			start->next = toinsert;
//			toinsert->prev = start;
//			toinsert->next = start;
//		}
//
//		return toinsert;
//	}
//}
//listnode *ll_insert_at_prev(listnode *start, void *data)
//{
//	/* allocate a new node */
//	listnode *newnode = malloc(sizeof(listnode));
//	newnode->data = data;
//
//	/* increment the number of entries */
//	((header_data *)(start->data))->size ++;
//
//	return _ll_insert_at_prev(start, newnode);
//}
//


listnode *_ll_insert_at_next(listnode *start, listnode *ptr, listnode *toinsert){
	listnode *g_new, *g_prev;
	listnode *temp;
	g_new = toinsert->data;

	if ((!start) || (!toinsert)) {
		fprintf(stderr, "Error: invalid value to _ll_insert_at_tail\n");
		exit(-1);
	} else {
		if (start->next) {
			//listnode *nextnode = start->next;			
			listnode *nextnode = ptr;

			toinsert->next = nextnode->next;
			nextnode->next->prev = toinsert;
			nextnode->next = toinsert;
			toinsert->prev = nextnode;
		} else {
			start->prev = toinsert;
			start->next = toinsert;
			toinsert->prev = start;
			toinsert->next = start;
		}

		return toinsert;
	}
}
listnode *ll_insert_at_next(listnode *start, listnode *ptr,void *data)
{
	/* allocate a new node */
	listnode *newnode = malloc(sizeof(listnode));
	newnode->data = data;

	/* increment the number of entries */
	((header_data *)(start->data))->size ++;
	//printf(" size %d --\n", ((header_data *)(start->data))->size);

	return _ll_insert_at_next(start, ptr, newnode);
}