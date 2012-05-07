#include "definitions.h"

// a hashtable which can be used to find file infos using inode values
hashtable inithashtable(void) // allocs hashtable, free when done
{
    hashtable h = (hashtable) malloc(sizeof(struct hashtable_struct));
    h->table = NULL;
    h->size = h->mask = h->entries = 0;
    return h;
} // inithashtable

void freehashtable(hashtable h)
{
    if (h->table);
        free(h->table);
    free(h);
} // freehashtable

uint32 inthash(hashtable h, int32 inode) // hash function due to Thomas Wang, 2007
{
    uint32 key = inode;
    key = ~key + (key << 15); // key = (key << 15) - key - 1;
    key = key ^ (key >> 12);
    key = key + (key << 2);
    key = key ^ (key >> 4);
    key = key * 2057; // key = (key + (key << 3)) + (key << 11);
    key = key ^ (key >> 16);
    return key & h->mask;
} // inthash

fileinfo* storehash(hashtable h, fileinfo* file) // returns file with same inode or stores new
{
    int32 hash; // don't compute hash yet, because mask changes when enlarging
    int pos;
    // do we need to enlarge the hash table?
    if (3 * h->entries >= h->size) { // we need to make it bigger
        if (h->size == 0) {
            h->size = 64; // a power of two
            h->mask = h->size - 1;
            h->table = (fileinfo**) calloc(h->size, sizeof(fileinfo*));
        } else {
            int oldsize = h->size, i, j;
            fileinfo** oldtab = h->table;
            // first double the size
            h->size *= 2;
            h->mask = h->size - 1;
            h->table = (fileinfo**) calloc(h->size, sizeof(fileinfo*));
            // now move all the old entries into the new table
            for (i = 0; i < oldsize; i++)
                if (oldtab[i] != NULL) {
                    for (j = inthash(h, oldtab[i]->inode); h->table[j] != NULL;
                                                    j = (j + 1) & h->mask) ;
                    h->table[j] = oldtab[i];
                }
            // now free the old table
            free(oldtab);
        }
    }
    hash = inthash(h, file->inode);
    for (pos = hash; h->table[pos] != NULL; pos = (pos + 1) & h->mask)
        if (inthash(h, h->table[pos]->inode) == hash)
            return h->table[pos];
    h->table[pos] = file;
    h->entries++;
    return NULL;
} // storehash
