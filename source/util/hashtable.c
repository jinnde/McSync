#include "definitions.h"

#define freekey(X) free(X)

// Copyright (C) 2004 Christopher Clark <firstname.lastname@cl.cam.ac.uk>

// Credit for primes table: Aaron Krowne
// http://br.endernet.org/~akrowne/
// http://planetmath.org/encyclopedia/GoodHashTablePrimes.html
static const uint32 primes[] = {
    53, 97, 193, 389,
    769, 1543, 3079, 6151,
    12289, 24593, 49157, 98317,
    196613, 393241, 786433, 1572869,
    3145739, 6291469, 12582917, 25165843,
    50331653, 100663319, 201326611, 402653189,
    805306457, 1610612741
};

const uint32 prime_table_length = sizeof(primes)/sizeof(primes[0]);
const float max_load_factor = 0.65;

uint32 hash(hashtable *h, void *k);

static inline uint32 indexfor(unsigned int tablelength, unsigned int hashvalue) {
    return (hashvalue % tablelength);
}

uint32 hash_int32(void *k) // Function due to Thomas Wang, 2007
{
    uint32 key = *((uint32*) k);
    key = ~key + (key << 15); // key = (key << 15) - key - 1;
    key = key ^ (key >> 12);
    key = key + (key << 2);
    key = key ^ (key >> 4);
    key = key * 2057; // key = (key + (key << 3)) + (key << 11);
    key = key ^ (key >> 16);
    return key;
} // hash_int32

int32 int32_equals(void *key1, void *key2)
{
    int32 *a = (int32*) key1;
    int32 *b = (int32*) key2;
    return *a == *b;
} // int_equals

hashtable *inithashtable(uint32 minsize, uint32 (*hashf) (void*),
                 int32 (*eqf) (void*, void*))
{
    hashtable *h;
    uint32 pindex, size = primes[0];
    /* Check requested hashtable isn't too large */
    if (minsize > (1u << 30))
        return NULL;
    /* Enforce size as prime */
    for (pindex=0; pindex < prime_table_length; pindex++) {
        if (primes[pindex] > minsize) {
            size = primes[pindex];
            break;
        }
    }
    h = (hashtable*) malloc(sizeof(struct hashtable_struct));
    if (NULL == h)
        return NULL; /*oom*/
    h->table = (entry **) malloc(sizeof(struct entry_struct*) * size);
    if (NULL == h->table) {
        free(h);
        return NULL;
    } /*oom*/
    memset(h->table, 0, size * sizeof(struct entry_struct*));
    h->tablelength = size;
    h->primeindex = pindex;
    h->entrycount = 0;
    h->hashfn = hashf;
    h->eqfn = eqf;
    h->loadlimit = (uint32) ceil(size * max_load_factor);
    return h;
} // inithashtable

uint32 hash(hashtable *h, void *k)
{
    // Aim to protect against poor hash functions by adding logic here
    // - logic taken from java 1.4 hashtable source
    uint32 i = h->hashfn(k);
    i += ~(i << 9);
    i ^=  ((i >> 14) | (i << 18)); /* >>> */
    i +=  (i << 4);
    i ^=  ((i >> 10) | (i << 22)); /* >>> */
    return i;
} // hash

int32 hashtableexpand(hashtable *h)
{
    /* Double the size of the table to accomodate more entries */
    entry **newtable;
    entry *e;
    entry **pE;
    uint32 newsize, i, index;
    /* Check we're not hitting max capacity */
    if (h->primeindex == (prime_table_length - 1))
        return 0;
    newsize = primes[++(h->primeindex)];

    newtable = (entry**) malloc(sizeof(struct entry_struct*) * newsize);
    if (NULL != newtable) {
        memset(newtable, 0, newsize * sizeof(struct entry *));
        /* This algorithm is not 'stable'. ie. it reverses the list
         * when it transfers entries between the tables */
        for (i = 0; i < h->tablelength; i++) {
            while (NULL != (e = h->table[i])) {
                h->table[i] = e->next;
                index = indexfor(newsize,e->h);
                e->next = newtable[index];
                newtable[index] = e;
            }
        }
        free(h->table);
        h->table = newtable;
    } else { // Plan B: realloc instead
        newtable = (entry**) realloc(h->table, newsize * sizeof(struct entry_struct*));
        if (NULL == newtable) {
            (h->primeindex)--;
            return 0;
        }
        h->table = newtable;
        memset(newtable[h->tablelength], 0, newsize - h->tablelength);
        for (i = 0; i < h->tablelength; i++) {
            for (pE = &(newtable[i]), e = *pE; e != NULL; e = *pE) {
                index = indexfor(newsize,e->h);
                if (index == i) {
                    pE = &(e->next);
                } else {
                    *pE = e->next;
                    e->next = newtable[index];
                    newtable[index] = e;
                }
            }
        }
    }
    h->tablelength = newsize;
    h->loadlimit = (uint32) ceil(newsize * max_load_factor);
    return -1;
} // hashtableexpand

uint32 hashtablecount(hashtable *h)
{
    return h->entrycount;
} // hashtablecount

int32 hashtableinsert(hashtable *h, void *k, void *v) // non-zero on success
{
    // This method allows duplicate keys - but they shouldn't be used
    uint32 index;
    entry *e;
    if (++(h->entrycount) > h->loadlimit) {
         // Ignore the return value. If expand fails, we should
         // still try cramming just this value into the existing table
         // -- we may not have memory for a larger table, but one more
         // element may be ok. Next time we insert, we'll try expanding again.
        hashtableexpand(h);
    }
    e = (entry*) malloc(sizeof(struct entry_struct));
    if (NULL == e) {
        --(h->entrycount);
        return 0;
    } /*oom*/
    e->h = hash(h,k);
    index = indexfor(h->tablelength,e->h);
    e->k = k;
    e->v = v;
    e->next = h->table[index];
    h->table[index] = e;
    return -1;
} // hashtablesinsert

void *hashtablesearch(hashtable *h, void *k) // returns value associated with key
{
    entry *e;
    uint32 hashvalue, index;
    hashvalue = hash(h,k);
    index = indexfor(h->tablelength,hashvalue);
    e = h->table[index];
    while (NULL != e) {
        /* Check hash value to short circuit heavier comparison */
        if ((hashvalue == e->h) && (h->eqfn(k, e->k))) return e->v;
        e = e->next;
    }
    return NULL;
} // hashtablesearch

void *hashtableremove(hashtable *h, void *k)
{
    entry *e;
    entry **pE;
    void *v;
    uint32 hashvalue, index;

    hashvalue = hash(h,k);
    index = indexfor(h->tablelength,hash(h,k));
    pE = &(h->table[index]);
    e = *pE;
    while (NULL != e) {
        // Check hash value to short circuit heavier comparison
        if ((hashvalue == e->h) && (h->eqfn(k, e->k))) {
            *pE = e->next;
            h->entrycount--;
            v = e->v;
            freekey(e->k);
            free(e);
            return v;
        }
        pE = &(e->next);
        e = e->next;
    }
    return NULL;
} // hashtableremove

void freehashtable(hashtable *h, int32 free_values, int32 free_keys)
{
    uint32 i;
    entry *e, *f;
    entry **table = h->table;
    if (free_values) {
        for (i = 0; i < h->tablelength; i++) {
            e = table[i];
            while (NULL != e) {
                f = e;
                e = e->next;
                if (free_keys)
                    freekey(f->k);
                free(f->v);
                free(f);
            }
        }
    } else {
        for (i = 0; i < h->tablelength; i++) {
            e = table[i];
            while (NULL != e) {
                f = e;
                e = e->next;
                if (free_keys)
                    freekey(f->k);
                free(f);
            }
        }
    }
    free(h->table);
    free(h);
} // freehashtable

/*
 * Copyright (c) 2002, Christopher Clark
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of the original author; nor the names of any contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/