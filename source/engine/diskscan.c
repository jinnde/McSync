#include "definitions.h"


char* strdupcat(char* a, char* b, ...) // allocs new string, last arg must be NULL
{
    char **i, *j, *cc;
    int len;

    len = 0;
    for (i = &a; *i != NULL; i += &b - &a)
        for (j = *i; *j != 0; j++)
            len++;

    cc = (char*)malloc(len + 1);

    len = 0;
    for (i = &a; *i != NULL; i += &b - &a)
        for (j = *i; *j != 0; j++)
            cc[len++] = *j;
    cc[len] = 0;

    return cc;
} // strdupcat

char *most(char *s) // allocs new string, s must contain '/' and be writable
{
    char c, *cp, *ret;
    cp = rindex(s, '/');
    c = *cp;
    *cp = 0;
    ret = strdup(s);
    *cp = c;
    return ret;
} // most



ssize_t sys_getxattr(const char *path, const char *name, void *value, size_t size)
{
#if defined(HAVE_GETXATTR)
#if defined(DARWINOS)           // MAC
    return getxattr(path, name, value, size, 0, XATTR_NOFOLLOW);
#else                           // LINUX
    return lgetxattr(path, name, value, size);
#endif
#elif defined(HAVE_GETEA)       // AIX
    return lgetea(path, name, value, size);
#else
    errno = ENOTSUP; // either not supported by filesystem, or not by mcsync!
    return -1;
#endif
} // sys_getxattr

ssize_t sys_listxattr(const char *path, char *list, size_t size)
{
#if defined(HAVE_LISTXATTR)
#if defined(DARWINOS)           // MAC
    return listxattr(path, list, size, XATTR_NOFOLLOW);
#else                           // LINUX
    return llistxattr(path, list, size);
#endif
#elif defined(HAVE_LISTEA)      // AIX
    return llistea(path, list, size);
#else
    errno = ENOTSUP; // either not supported by filesystem, or not by mcsync!
    return -1;
#endif
} // sys_listxattr

int sys_removexattr(const char *path, const char *name)
{
#if defined(HAVE_REMOVEXATTR)
#if defined(DARWINOS)           // MAC
    return removexattr(path, name, XATTR_NOFOLLOW);
#else                           // LINUX
    return lremovexattr(path, name);
#endif
#elif defined(HAVE_REMOVEEA)    // AIX
    return lremoveea(path, name);
#else
    errno = ENOTSUP; // either not supported by filesystem, or not by mcsync!
    return -1;
#endif
} // sys_removexattr

int sys_setxattr(const char *path, const char *name, const void *value,
                    size_t size, int flags)
{
#if defined(HAVE_SETXATTR)
#if defined(DARWINOS)           // MAC
    return setxattr(path, name, value, size, 0, flags | XATTR_NOFOLLOW);
#else                           // LINUX
    return lsetxattr(path, name, value, size, flags);
#endif
#elif defined(HAVE_SETEA)       // AIX
    return lsetea(path, name, value, size, flags);
#else
    errno = ENOTSUP; // either not supported by filesystem, or not by mcsync!
    return -1;
#endif
} // sys_setxattr


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of disk scan //////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

// from here down to sortchildren is for sorting

typedef struct sorter_struct {
    fileinfo *d2, *d4, *d6; // data, NULL if absent
    struct sorter_struct *b1, *b3, *b5, *b7; // the stuff below, NULL if absent
} sorter;

sorter *newsorter(void)
{
    sorter *newguy = (sorter *) malloc(sizeof(sorter));
    newguy->d2 = newguy->d4 = newguy->d6 = NULL;
    newguy->b1 = newguy->b3 = newguy->b5 = newguy->b7 = NULL;
    return newguy;
} // newsorter

void freesortertree(sorter *skunk)
{
    if (skunk == NULL)
        return;
    freesortertree(skunk->b5);
    freesortertree(skunk->b3);
    freesortertree(skunk->b1);
    free(skunk);
} // freesortertree

void insertchild(sorter *holder, fileinfo *child)
{
    // the 2-3 algorithm is to go down to the lowest level (NULL b1) & insert it.
    // Recursively speaking, if the result of an insert is a #4 (d6 != NULL)
    // then we split it up, growing ourself by 1.

    if (holder->b1 == NULL) {
        // at bottom
        if (holder->d2 == NULL || strcmp(rindex(holder->d2->filename, '/'),
                                         rindex(child->filename, '/')) > 0) {
            holder->d6 = holder->d4;
            holder->d4 = holder->d2;
            holder->d2 = child;
        } else if (holder->d4 == NULL || strcmp(rindex(holder->d4->filename, '/'),
                                            rindex(child->filename, '/')) > 0) {
            holder->d6 = holder->d4;
            holder->d4 = child;
        } else {
            holder->d6 = child;
        }
    } else {
        // not at bottom
        if (strcmp(rindex(holder->d2->filename, '/'),
                   rindex(child->filename, '/')) > 0) {
            insertchild(holder->b1, child);
            if (holder->b1->d6 != NULL) {
                holder->b7 = holder->b5;
                holder->d6 = holder->d4;
                holder->b5 = holder->b3;
                holder->d4 = holder->d2;
                holder->b3 = newsorter();
                holder->b3->b1 = holder->b1->b5;
                holder->b3->d2 = holder->b1->d6;
                holder->b3->b3 = holder->b1->b7;
                holder->d2 = holder->b1->d4;
                holder->b1->d4 = NULL;
                holder->b1->b5 = NULL;
                holder->b1->d6 = NULL;
                holder->b1->b7 = NULL;
            }
        } else if (holder->d4 == NULL || strcmp(rindex(holder->d4->filename, '/'),
                                              rindex(child->filename, '/')) > 0) {
            insertchild(holder->b3, child);
            if (holder->b3->d6 != NULL) {
                holder->b7 = holder->b5;
                holder->d6 = holder->d4;
                holder->b5 = newsorter();
                holder->b5->b1 = holder->b3->b5;
                holder->b5->d2 = holder->b3->d6;
                holder->b5->b3 = holder->b3->b7;
                holder->d4 = holder->b3->d4;
                holder->b3->d4 = NULL;
                holder->b3->b5 = NULL;
                holder->b3->d6 = NULL;
                holder->b3->b7 = NULL;
            }
        } else {
            insertchild(holder->b5, child);
            if (holder->b5->d6 != NULL) {
                holder->b7 = newsorter();
                holder->b7->b1 = holder->b5->b5;
                holder->b7->d2 = holder->b5->d6;
                holder->b7->b3 = holder->b5->b7;
                holder->d6 = holder->b5->d4;
                holder->b5->d4 = NULL;
                holder->b5->b5 = NULL;
                holder->b5->d6 = NULL;
                holder->b5->b7 = NULL;
            }
        }
    }
} // insertchild

fileinfo **linkchildren(sorter *subset, fileinfo **tail) // returns new tail
{
    *tail = NULL;

    if (subset != NULL) {
        if (subset->b1 != NULL)
            tail = linkchildren(subset->b1, tail);
        if (subset->d2 != NULL)
            tail = &((*tail = subset->d2)->next);
        if (subset->b3 != NULL)
            tail = linkchildren(subset->b3, tail);
        if (subset->d4 != NULL)
            tail = &((*tail = subset->d4)->next);
        if (subset->b5 != NULL)
            tail = linkchildren(subset->b5, tail);
    }
    *tail = NULL;

    return tail;
} // linkchildren

void sortchildren(fileinfo *image) // sorts so "A" comes before "Z"
{
    // we want to sort down, down->next, down->next->next, etc. by filename
    // strategy: eat the ->next linked list, forming a tree
    // we want to have reasonable running time even if the order is not random
    // the point of sorting is that contrastimages can run much faster
    // without sorting contrastimages is quite slow compared to everything else,
    // and it is due to big directories (mail messages, movie frames, etc.)

    sorter *root = newsorter();
    fileinfo *child;

    // build 2-3 tree of children
    for (child = image->down; child != NULL; child = child->next) {
        insertchild(root, child);
        if (root->d6 != NULL) {
            // the tree is growing one level deeper
            sorter *newtoplevel, *newroot;
            newtoplevel = newsorter();
            newtoplevel->b1 = root->b5;
            newtoplevel->d2 = root->d6;
            newtoplevel->b3 = root->b7;
            root->b5 = NULL;
            root->d6 = NULL;
            root->b7 = NULL;
            newroot = newsorter();
            newroot->b1 = root;
            newroot->d2 = root->d4;
            newroot->b3 = newtoplevel;
            root->d4 = NULL;
            root = newroot;
        }
    }

    // rebuild linked list of children
    linkchildren(root, &(image->down));

    freesortertree(root);
} // sortchildren

fileinfo** hashtab;
int hashtabsize = 0, hashtabmask, hashtabentries = 0;
// stores files according to inode hash, to find inode repeats

uint32 inthash(int32 inode) // hash function due to Thomas Wang, 2007
{
    uint32 key = inode;
    key = ~key + (key << 15); // key = (key << 15) - key - 1;
    key = key ^ (key >> 12);
    key = key + (key << 2);
    key = key ^ (key >> 4);
    key = key * 2057; // key = (key + (key << 3)) + (key << 11);
    key = key ^ (key >> 16);
    return key & hashtabmask;
} // inthash

fileinfo* storehash(fileinfo* file) // returns file with same inode or stores new
{
    int32 hash; // don't compute hash yet, because mask changes when enlarging
    int pos;
    // do we need to enlarge the hash table?
    if (3 * hashtabentries >= hashtabsize) { // we need to make it bigger
        if (hashtabsize == 0) {
            hashtabsize = 64; // a power of two
            hashtabmask = hashtabsize - 1;
            hashtab = (fileinfo**) calloc(hashtabsize, sizeof(fileinfo*));
        } else {
            int oldsize = hashtabsize, i, j;
            fileinfo** oldtab = hashtab;
            // first double the size
            hashtabsize *= 2;
            hashtabmask = hashtabsize - 1;
            hashtab = (fileinfo**) calloc(hashtabsize, sizeof(fileinfo*));
            // now move all the old entries into the new table
            for (i = 0; i < oldsize; i++)
                if (oldtab[i] != NULL) {
                    for (j = inthash(oldtab[i]->inode); hashtab[j] != NULL;
                                                    j = (j + 1) & hashtabmask) ;
                    hashtab[j] = oldtab[i];
                }
            // now free the old table
            free(oldtab);
        }
    }
    hash = inthash(file->inode);
    for (pos = hash; hashtab[pos] != NULL; pos = (pos + 1) & hashtabmask)
        if (inthash(hashtab[pos]->inode) == hash)
            return hashtab[pos];
    hashtab[pos] = file;
    hashtabentries++;
    return NULL;
} // storehash

void emptyhashtable(void)
{
    if (hashtabsize == 0)
        return;
    free(hashtab);
    hashtabsize = 0;
    hashtabentries = 0;
} // emptyhashtable

// get inode info for filename (which includes path) and for any subdirectories
fileinfo* formimage(char* filename)
{
    fileinfo *image, *twin;
    struct stat status;
    int toplevel;

    // is the filename for real?
    if (    !strcmp(filename + strlen(filename) - 2, "/.")
         || !strcmp(filename + strlen(filename) - 3, "/..")) // inefficient
        return NULL;
    if (lstat(filename, &status)) {
        printf("Warning: Could not stat %s (%s)\n", filename, strerror(errno));
        return NULL;
    }

    toplevel = ! amticking;
    if (toplevel) {
        progress1 = progress2 = progress3 = progress4 = 0;
        startticking();
        emptyhashtable();
    }

    // prepare some space
    image = (fileinfo*) malloc(sizeof(fileinfo));
    image->next = image->down = image->up = NULL;

    // jot down a bunch of stuff about the file, see man page for stat
    image->inode = status.st_ino;
    twin = storehash(image);
    switch (status.st_mode & S_IFMT) {
        case S_IFDIR:   image->filetype = 1;   progress1++;    break;
        case S_IFREG:   image->filetype = 2;   progress2++;    break;
        case S_IFLNK:   image->filetype = 3;   progress3++;    break;
        default:        image->filetype = 4;   progress4++;    break;
    }
    // Mac man page says the only attributes returned from an
    // lstat() that refer to the symbolic link itself are the
    // file type (S_IFLNK), size, blocks, and link count (always 1).
    // But mac (no) and linux (yes) differ on whether links have times...
    image->numchildren = 0;
    image->subtreesize = 1;
    image->subtreebytes = status.st_size;
    image->hardlinks = status.st_nlink;
    if (twin == NULL)
        image->nexthardlink = image; // trivial circular linked list
    else {
        image->nexthardlink = twin->nexthardlink; // insert right after twin
        twin->nexthardlink = image;
    }
    image->accesstime = status.st_atime;
    image->modificationtime = status.st_mtime;
    image->metamodtime = status.st_ctime;
    image->metamodtime = status.st_ctime;
    image->filelength = status.st_size;
    image->contentsignature[0] = 0; // time-consuming,
    image->contentsignature[1] = 0; // so postpone this calculation
    image->contentsignature[2] = 0;
    image->contentsignature[3] = 0;
    image->permissions = status.st_mode & perm_mask; // perm_mask flags portable?
    image->numericuser = status.st_uid;
    image->numericgroup = status.st_gid;
    image->user = strdup(getpwuid(status.st_uid)->pw_name);
    image->group = strdup(getgrgid(status.st_gid)->gr_name);
    image->filename = strdup(filename);

    // if it is a directory, dig out the filenames and recurse
    if (image->filetype == 1) {
        DIR* thisdir;
        struct dirent *entry;

        thisdir = opendir(filename);
        if (!thisdir) {
            printf("Warning: directory %s could not be read (%s)\n",
                    filename, strerror(errno));
            goto donewithdir;
        }

        // loop over entries in the directory
        while (1) {
            fileinfo *child;
            char *childname;
            errno = 0;
            entry = readdir(thisdir); // this assumes errno stays 0 on success
            // (I see no other way to detect an error than this way.
            //  Another reason interfaces should be formalized further.)
            if (entry == NULL) {
                if (errno != 0)
                    printf("Warning: error reading directory %s (%s)\n",
                            filename, strerror(errno));
                // if no error, entry==NULL means we have read it all
                break;
            }
            if (index(entry->d_name, '/')) {
                printf("Warning: skipping file \"%s\" because it"
                        " contains a '/' (in directory %s)\n",
                        entry->d_name, filename);
                continue;
            }
            childname = strdupcat(filename, "/", entry->d_name, NULL);
            child = formimage(childname);
            if (child == NULL) {
                // this is not abnormal, it can mean the entry should be ignored
                // for example, . and .. will result in NULL
                goto freechild;
            }
            child->next = image->down;
            child->up = image;
            image->down = child;
            image->numchildren += 1;
            image->subtreesize += child->subtreesize;
            image->subtreebytes += child->subtreebytes;

            freechild:
            free(childname);
        }

        if (closedir(thisdir) != 0) {
            printf("Warning: could not close directory %s (%s)\n",
                    filename, strerror(errno));
        }

        sortchildren(image);
    }
    donewithdir:

    if (didtick || toplevel) {
        didtick = 0;
        printf("\015Scanning directories... "
                "(found %d directories, %d files, %d links, %d other)",
                progress1, progress2, progress3, progress4);
        fflush(stdout);
    }
    if (toplevel) {
        printf("\n");
        stopticking();
    }

    return image;
} // formimage

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of disk scan ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of scan IO ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////


/*
image file format:
int32 mcsync magic cookie
int32 version
int32 number of files
repeat:   (they must be in depth-first order, using "number of subfiles")
int32 inode
int32 file type (1 = directory, 2 = file, 3 = symlink, 4 = other)
int32 number of subfiles (0 if not a directory or an empty directory)
int32 number of hard links
int64 access time
int64 modification time
int64 meta-info modification time
int64 file length
int128 content signature
int32 permissions (suid, sgid, sticky, ur, uw, ux, gr, gw, gx, or, ow=2, ox=1)
int32 user
int32 group
char user, ending in 0
char group, ending in 0
char filename ending in 0
*/

char* getstring(FILE* input, char delimiter) // returns new string; free when done
{
    static char* buf = NULL;
    static int buflen;
    char *i;
    char *copy;

    if (buf == NULL) {
        //printf("Initializing input string buffer\n");
        buflen = 2048;
        buf = (char*) malloc(buflen);
    }

    i = buf;
    while (1) {
        int newchar = getc(input);
        if (newchar == EOF) {
            printf("Error: file ended prematurely\n");
            cleanexit(__LINE__);
        }
        *i = (char) newchar;
        i++;
        if (newchar == delimiter)
            break;
        if (i - buf >= buflen) {
            char *newbuf;
            //printf("Expanding input string buffer from %d to %d\n",
            //        buflen, 2 * buflen);
            buflen *= 2;
            //printf("last character stored: '%c'  original buffer: %.*s\n",
            //        newchar, buflen/2, buf);
            newbuf = (char*) realloc(buf, buflen);
            i += newbuf - buf;
            buf = newbuf;
            //printf("last character stored: '%c'  new buffer: %.*s\n",
            //        newchar, buflen, buf);
        }
    }
    copy = (char*) malloc(i - buf);
    strncpy(copy, buf, i - buf);
    copy[i - buf - 1] = 0;
    //printf("read string '%s'\n", copy);
    return copy;
} // getstring

void writesubimage(FILE* output, fileinfo* subimage)
{
    if (subimage == NULL)
        return;

    if (didtick) {
        didtick = 0;
        printf("\015Writing image file to disk... "
                "(wrote %d directories, %d files, %d links, %d other)",
                progress1, progress2, progress3, progress4);
        fflush(stdout);
    }
    switch (subimage->filetype) {
        case 1:     progress1++;    break;
        case 2:     progress2++;    break;
        case 3:     progress3++;    break;
        case 4:     progress4++;    break;
    }

    if (0) {
        time_t c = subimage->metamodtime;
        time_t m = subimage->modificationtime;
        time_t a = subimage->accesstime;
        char *cs = strdup(ctime(&c));
        char *ms = strdup(ctime(&m)); // ctime overwrites its buffer
        char *as = strdup(ctime(&a));
        printf("%s\n", subimage->filename);
        free(cs);
        free(ms);
        free(as);
    }

    put32(output, subimage->inode);
    put32(output, subimage->filetype);
    put32(output, subimage->numchildren);
    put32(output, subimage->hardlinks);
    put64(output, subimage->accesstime);
    put64(output, subimage->modificationtime);
    put64(output, subimage->metamodtime);
    put64(output, subimage->filelength);
    put32(output, subimage->contentsignature[0]);
    put32(output, subimage->contentsignature[1]);
    put32(output, subimage->contentsignature[2]);
    put32(output, subimage->contentsignature[3]);
    put32(output, subimage->permissions);
    put32(output, subimage->numericuser);
    put32(output, subimage->numericgroup);
    putstring(output, subimage->user);
    putstring(output, subimage->group);
    putstring(output, subimage->filename);

    {
        fileinfo *child;
        for (child = subimage->down; child != NULL; child = child->next) {
            writesubimage(output, child);
        }
    }
} // writesubimage

void writeimage(fileinfo* image, char* filename)
{
    FILE *output;

    if (image == NULL) // not clear this should ever happen
        return;

    printf("Writing image file %s\n", filename);

    output = fopen(filename, "w");
    if (!output) {
        printf("Error opening output file %s (%s)\n",
                filename, strerror(errno));
        return;
    }

    progress1 = progress2 = progress3 = progress4 = 0;
    startticking();

    put32(output, magiccookie);
    put32(output, logfileversionnumber);
    put32(output, image->subtreesize);

    writesubimage(output, image);

    fclose(output);

    stopticking();
    printf("\015Writing image file to disk... "
            "(wrote %d directories, %d files, %d links, %d other)\n",
            progress1, progress2, progress3, progress4);

    {
        char *g1, *g2;
        printf("Wrote image file %s containing %s items "
                "(which contain %s bytes on disk).\n",
                filename,
                g1 = strdup(commanumber(image->subtreesize)),
                g2 = strdup(commanumber(image->subtreebytes)));
        free(g1);
        free(g2);
    }
} // writeimage

fileinfo* readsubimage(FILE* input)
{
    fileinfo* subimage;

    subimage = (fileinfo*) malloc(sizeof(fileinfo));
    subimage->next = subimage->down = subimage->up = NULL;
    subimage->inode = get32(input);
    subimage->filetype = get32(input);
    subimage->numchildren = get32(input);
    subimage->hardlinks = get32(input);
    subimage->nexthardlink = subimage; // trivial circular linked list
    subimage->accesstime = get64(input);
    subimage->modificationtime = get64(input);
    subimage->metamodtime = get64(input);
    subimage->filelength = get64(input);
    subimage->contentsignature[0] = get32(input);
    subimage->contentsignature[1] = get32(input);
    subimage->contentsignature[2] = get32(input);
    subimage->contentsignature[3] = get32(input);
    subimage->permissions = get32(input);
    subimage->numericuser = get32(input);
    subimage->numericgroup = get32(input);
    subimage->user = getstring(input, 0);
    subimage->group = getstring(input, 0);
    subimage->filename = getstring(input, 0);

    subimage->subtreesize = 1;
    subimage->subtreebytes = subimage->filelength;

    if (0) {
        printf("%s\n", subimage->filename);
    }

    switch (subimage->filetype) {
        case 1:     progress1++;    break;
        case 2:     progress2++;    break;
        case 3:     progress3++;    break;
        case 4:     progress4++;    break;
    }
    if (didtick) {
        didtick = 0;
        printf("\015Reading image file... "
                "(read %d directories, %d files, %d links, %d other)",
                progress1, progress2, progress3, progress4);
        fflush(stdout);
    }

    {
        int childnum;
        fileinfo *child, **womb;
        womb = &(subimage->down);
        for (childnum = subimage->numchildren; childnum > 0; childnum--) {
            child = readsubimage(input);
            *womb = child;
            womb = &(child->next);
            child->up = subimage;
            subimage->subtreesize += child->subtreesize;
            subimage->subtreebytes += child->subtreebytes;
        }
    }

    return subimage;
} // readsubimage

fileinfo* readimage(char* filename)
{
    fileinfo *image;
    FILE *input;
    int32 fileversion;
    int32 numberofrecords;

    printf("Reading image file %s\n", filename);

    input = fopen(filename, "r");
    if (!input) {
        printf("Error opening input file %s (%s)\n",
                filename, strerror(errno));
        return NULL;
    }

    if (get32(input) != magiccookie) {
        printf("Error: input file %s does not appear to be an image file\n",
                filename);
        cleanexit(__LINE__);
    }

    fileversion = get32(input);
    if (fileversion > logfileversionnumber) {
        printf("Warning: I can read file formats up to version %d,"
                " but file %s has format version %d\n",
                logfileversionnumber, filename, fileversion);
    }

    numberofrecords = get32(input);

    progress1 = progress2 = progress3 = progress4 = 0;
    startticking();

    image = readsubimage(input);

    stopticking();
    printf("\015Reading image file... "
            "(read %d directories, %d files, %d links, %d other)\n",
            progress1, progress2, progress3, progress4);

    if (numberofrecords != image->subtreesize) {
        printf("Warning: image file %s claimed to have %d entries,"
                " but it appears to contain %d entries instead.\n",
                filename, numberofrecords, image->subtreesize);
    }

    fclose(input);

    {
        char *g1, *g2;
        printf("Read image file %s containing %s files (%s bytes).\n",
                filename,
                g1 = strdup(commanumber(image->subtreesize)),
                g2 = strdup(commanumber(image->subtreebytes)));
        free(g1);
        free(g2);
    }

    return image;
} // readimage

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of scan IO //////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of scan management ////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

typedef struct threadlist_struct {
    struct threadlist_struct *next;
    int newinfo; // 0 = report missing or already read, 1 = new report ready
    int report[4]; // meaning and usage will depend on what thread is doing
    // but usually it is { [0]:files [1]:dirs [2]:links [3]:other } processed
    int answerready; // 0 = not ready, 1 = ready
    void *answer; // where thread can put its answer when done
} threadlist;

fileinfo *updatehistory(device *m, char *filepath, stringlist *ignore,
                                                                    int *report)
// this may be done in another thread or in another process on a remote machine
{
   return NULL;
} // updatehistory

void doremotescan(device *m, char *filepath, stringlist *ignore, threadlist *rep)
// this occurs in another thread, so *rep holds the report
{
    stringlist *where;
    int local = 0; // whether the device is local

    // see if this is a local job
    for (where = m->reachplan.ipaddrs; where != NULL; where = where->next) {
        if (! strcmp(where->string, hostname())) {
            local = 1;
            break;
        }
    }

    if (local) {
        rep->answer = updatehistory(m, filepath, ignore, rep->report);
        rep->answerready = 1;
    } else {
        if (m->reachplan.ipaddrs != NULL)
            printf("Error: Information from %s not available on %s.\n",
                    m->reachplan.ipaddrs->string, hostname());
    }
} // doremotescan

void spinoffscan(device *m, char *filepath, stringlist *ignore)
{
    // split off a separate thread to (possibly remotely) perform the scan
    threadlist *newguy = (threadlist*) malloc(sizeof(newguy));

} // spinoffscan

char *physicalpath(graft *g, char *virtualpath) // free when done
{
    // change leading part of virtualpath from g->virtualpath to g->hostpath
    if (strncmp(g->virtualpath, virtualpath, strlen(g->virtualpath))) {
        // routine should not have been called under this condition
        printf("Error: physicalpath called to convert"
                " %s from %s (which it doesn't match) to %s.\n",
                virtualpath, g->virtualpath, g->hostpath);
        return strdupcat(g->hostpath, NULL);
    }
    return strdupcat(g->hostpath, virtualpath + strlen(g->virtualpath), NULL);
} // physicalpath

void startscans(virtualnode *subtree)
{
    // get a scan going for each graft that is present at this subtree
    graft *g;
    stringlist *pr;
    char *subtreepath;
    int doit;

    { // set subtreepath
        virtualnode *climber = subtree;
        subtreepath = strdupcat(climber->name, NULL);
        while ((climber = climber->up) != NULL) {
            char *morepath = strdupcat(climber->name, "/", subtreepath, NULL);
            free(subtreepath);
            subtreepath = morepath;
        }
    }

    for (g = graftlist; g != NULL; g = g->next) {
        doit = 0;
        if (! strncmp(g->virtualpath, subtreepath, strlen(g->virtualpath))) {
            // we're inside the graft... check if we're pruned
            doit = 1;
            for (pr = g->prunepoints; pr != NULL; pr = pr->next) {
                if (! strncmp(pr->string, subtreepath, strlen(pr->string))) {
                    // we're pruned back off the graft
                    doit = 0;
                    break;
                }
            }
        }
        if (doit) {
            // get the scan started
            char *phroot = physicalpath(g, subtreepath);
            // first build up stringlist of places to ignore (backwards is ok)
            stringlist *pignore = NULL;
            stringlist *vignore = g->prunepoints;
            while (vignore != NULL) {
                stringlist *newguy = (stringlist*) malloc(sizeof(stringlist));
                newguy->string = physicalpath(g, vignore->string);
                newguy->next = pignore;
                pignore = newguy;
                vignore = vignore->next;
            }
            spinoffscan(g->host, phroot, pignore);
            // now break everything back down
            free(phroot);
            while (pignore != NULL) {
                stringlist *next = pignore->next;
                free(pignore->string);
                free(pignore); // not thread-safe to assume pignore->next is good
                pignore = next;
            }
        }
    }
} // startscans

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of scan management //////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

