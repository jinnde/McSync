#include "definitions.h"

void resetscanprogress(scan_progress *progress) //does not alter progress->updateinteval
{
    (*progress)->directories =
    (*progress)->regularfiles =
    (*progress)->links =
    (*progress)->other =
    (*progress)->total = 0;
} // initscanprogress

char* strdupcat(const char* first, ...)
{
    va_list args;
    char* buf;
    const char* arg;
    size_t len = strlen(first);

    // calculate the length of the output string
    va_start(args, first);
    while ((arg = va_arg(args, const char*)) != NULL)
        len += strlen(arg);
    va_end(args);

    // allocate memory for the output string
    buf = malloc(len + 1);
    if (buf == NULL)
        return NULL;

    // copy strings into the output buffer
    strcpy(buf, first);
    va_start(args, first);
    while ((arg = va_arg(args, const char*)) != NULL)
        strcat(buf, arg);
    va_end(args);
    return buf;
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

void freehistory(history skunk)
{
    if (!skunk)
        return;

    if (skunk->next)
        freehistory(skunk->next);
    free(skunk);

} // freehistory

void freeextendedattributes(extendedattributes skunk)
{
    if (skunk->next)
        freeextendedattributes(skunk);

    if (skunk->name)
        free(skunk->name);

    if (skunk->contents)
        free(skunk->contents);

    freehistory(skunk->hist_attr);

    free(skunk);

} // freeextendedattributes

void freefileinfo(fileinfo* skunk)
{
    if (!skunk)
        return;

    freefileinfo(skunk->next);
    freefileinfo(skunk->down);

    if (skunk->user)
        free(skunk->user);

    if (skunk->group)
        free(skunk->group);

    if (skunk->filename)
        free(skunk->filename);

    freehistory(skunk->hist_modtime);
    freehistory(skunk->hist_contents);
    freehistory(skunk->hist_perms);
    freehistory(skunk->hist_name);
    freehistory(skunk->hist_loc);

    free(skunk);

} // freefileinfo

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

// get inode info for filename (which includes path) and for any subdirectories
fileinfo* formimage(char* filename, stringlist *prunepoints, connection worker_plug,
                    hashtable *h, scan_progress progress)
{  // IMPORTANT: h has to be NULL on top level call!
    fileinfo *image, *twin;
    struct stat status;
    int32 toplevel = 0;

    // is the filename for real?
    if (    !strcmp(filename + strlen(filename) - 2, "/.")
         || !strcmp(filename + strlen(filename) - 3, "/..")) // inefficient
        return NULL;
    // is the file on the prunelist?
    if (stringlistcontains(prunepoints, filename) != NULL)
        return NULL;
    if (lstat(filename, &status)) {
        printerr("Scan Warning: Could not stat %s (%s)\n", filename, strerror(errno));
        return NULL;
    }

    if (h == NULL) {
        toplevel = 1;
        h = inithashtable(1024, &hash_int32, &int32_equals);
    }

    // prepare some space
    image = (fileinfo*) malloc(sizeof(fileinfo));
    image->next = image->down = image->up = image->nexthardlink = NULL;

    // jot down a bunch of stuff about the file, see man page for stat
    image->inode = status.st_ino;

    twin = hashtablesearch(h, &image->inode);
    (void)hashtableinsert(h, &image->inode, image);

    switch (status.st_mode & S_IFMT) {
        case S_IFDIR:   image->filetype = 1;   progress->directories++;  break;
        case S_IFREG:   image->filetype = 2;   progress->regularfiles++; break;
        case S_IFLNK:   image->filetype = 3;   progress->links++;        break;
        default:        image->filetype = 4;   progress->other++;        break;
    }
    progress->total++;

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

    image->hist_modtime = NULL;
    image->hist_contents = NULL;
    image->hist_name = NULL;
    image->hist_perms = NULL;
    image->hist_loc = NULL;

    // if it is a directory, dig out the filenames and recurse
    if (image->filetype == 1) {
        DIR* thisdir;
        struct dirent *entry;

        thisdir = opendir(filename);
        if (!thisdir) {
            printerr("Warning: directory %s could not be read (%s)\n",
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
                    printerr("Warning: error reading directory %s (%s)\n",
                            filename, strerror(errno));
                // if no error, entry==NULL means we have read it all
                break;
            }
            if (index(entry->d_name, '/')) {
                printerr("Warning: skipping file \"%s\" because it"
                        " contains a '/' (in directory %s)\n",
                        entry->d_name, filename);
                continue;
            }
            childname = strdupcat(filename, "/", entry->d_name, NULL);

            child = formimage(childname, prunepoints, worker_plug, h, progress);
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
            printerr("Warning: could not close directory %s (%s)\n",
                    filename, strerror(errno));
        }
    }

    donewithdir:

    if (progress->total % progress->updateinterval == 0) {
            printerr("\015Scanning directories... "
             "(found %d directories, %d files, %d links, %d other)\n",
             progress->directories, progress->regularfiles, progress->links, progress->other);
    }

    if (toplevel) {
        freehashtable(h, 0, 0); // don't free images and don't free inodes
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
        //printerr("Initializing input string buffer\n");
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
            //printerr("Expanding input string buffer from %d to %d\n",
            //        buflen, 2 * buflen);
            buflen *= 2;
            //printerr("last character stored: '%c'  original buffer: %.*s\n",
            //        newchar, buflen/2, buf);
            newbuf = (char*) realloc(buf, buflen);
            i += newbuf - buf;
            buf = newbuf;
            //printerr("last character stored: '%c'  new buffer: %.*s\n",
            //        newchar, buflen, buf);
        }
    }
    copy = (char*) malloc(i - buf);
    strncpy(copy, buf, i - buf);
    copy[i - buf - 1] = 0;
    //printerr("read string '%s'\n", copy);
    return copy;
} // getstring

void puthistory(FILE *output, history h)
{ // prepends number of stored history objects
    history temp;
    int32 count = 0;

    for (temp = h; temp != NULL; temp = temp->next)
        count++;
    put32(output, count);
    for (temp = h; temp != NULL; temp = temp->next) {
        put64(output, temp->trackingnumber);
        put32(output, temp->devicetime);
    }
} // puthistory

void writesubimage(FILE *output, fileinfo* subimage, scan_progress progress)
{
    if (subimage == NULL)
        return;

    switch (subimage->filetype) {
        case 1:     progress->directories++;    break;
        case 2:     progress->regularfiles++;   break;
        case 3:     progress->links++;          break;
        case 4:     progress->other++;          break;
    }
    progress->total++;

    if (progress->total % progress->updateinterval == 0) {
            printerr("\015Writing image file to disk... "
             "(found %d directories, %d files, %d links, %d other)\n",
             progress->directories, progress->regularfiles, progress->links, progress->other);
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
    // from here down related to history
    put32(output, subimage->existence);
    puthistory(output, subimage->hist_modtime);
    puthistory(output, subimage->hist_contents);
    puthistory(output, subimage->hist_perms);
    puthistory(output, subimage->hist_name);
    puthistory(output, subimage->hist_loc);
    put64(output, subimage->trackingnumber);
    put32(output, subimage->devicetime);

    {
        fileinfo *child;
        for (child = subimage->down; child != NULL; child = child->next) {
            writesubimage(output, child, progress);
        }
    }
} // writesubimage

void writeimage(fileinfo *image, char *filename, scan_progress progress)
{
    FILE *output;

    if (image == NULL) // not clear this should ever happen
        return;

    printerr("Writing image file %s\n", filename);

    output = fopen(filename, "w");
    if (!output) {
        printerr("Error opening output file %s (%s)\n",
                filename, strerror(errno));
        return;
    }

    put32(output, magiccookie);
    put32(output, logfileversionnumber);
    put32(output, image->subtreesize);

    writesubimage(output, image, progress);
    fclose(output);
 } // writeimage

history gethistory(FILE *input) // allocates history, free when done
{
    history head, tail, temp;
    int32 count = get32(input);

    head = tail = NULL;
    while (count--) {
        temp = (history) malloc (sizeof(struct history_struct));
        temp->trackingnumber = get64(input);
        temp->devicetime = get32(input);
        temp->next = NULL;
        if (!head)
            head = tail = temp;
        else {
            tail->next = temp;
            tail = temp;
        }
    }
    return head;
} // gethistory

fileinfo *readsubimage(FILE *input, scan_progress progress)
{
    fileinfo *subimage;

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
    // from here down related to history
    subimage->existence = get32(input);
    subimage->hist_modtime = gethistory(input);
    subimage->hist_contents = gethistory(input);
    subimage->hist_perms = gethistory(input);
    subimage->hist_name = gethistory(input);
    subimage->hist_loc = gethistory(input);
    subimage->trackingnumber = get64(input);
    subimage->devicetime = get32(input);

    subimage->subtreesize = 1;
    subimage->subtreebytes = subimage->filelength;

    switch (subimage->filetype) {
        case 1:     progress->directories++;    break;
        case 2:     progress->regularfiles++;   break;
        case 3:     progress->links++;          break;
        case 4:     progress->other++;          break;
    }

    printerr("\015Reading image file... "
             "(read %d directories, %d files, %d links, %d other)",
             progress->directories, progress->regularfiles, progress->links, progress->other);
   {
        int childnum;
        fileinfo *child, **womb;
        womb = &(subimage->down);
        for (childnum = subimage->numchildren; childnum > 0; childnum--) {
            child = readsubimage(input, progress);
            *womb = child;
            womb = &(child->next);
            child->up = subimage;
            subimage->subtreesize += child->subtreesize;
            subimage->subtreebytes += child->subtreebytes;
        }
    }

    return subimage;
} // readsubimage

fileinfo *readimage(char *filename, scan_progress progress)
{
    fileinfo *image;
    FILE *input;
    int32 fileversion;
    int32 numberofrecords;

    printerr("Reading image file %s\n", filename);

    input = fopen(filename, "r");
    if (!input) {
        printerr("Error opening input file %s (%s)\n",
                filename, strerror(errno));
        return NULL;
    }

    if (get32(input) != magiccookie) {
        printerr("Error: input file %s does not appear to be an image file\n",
                filename);
        return NULL;
    }

    fileversion = get32(input);
    if (fileversion > logfileversionnumber) {
        printerr("Warning: I can read file formats up to version %d,"
                " but file %s has format version %d\n",
                logfileversionnumber, filename, fileversion);
    }

    numberofrecords = get32(input);

    progress->directories = progress->regularfiles = progress->links = progress->other = 0;

    image = readsubimage(input, progress);

    printerr("\015Reading image file... "
            "(read %d directories, %d files, %d links, %d other)\n",
            progress->directories, progress->regularfiles, progress->links, progress->other);

    if (numberofrecords != image->subtreesize) {
        printerr("Warning: image file %s claimed to have %d entries,"
                " but it appears to contain %d entries instead.\n",
                filename, numberofrecords, image->subtreesize);
    }

    fclose(input);

    {
        char *g1, *g2;
        printerr("Read image file %s containing %s files (%s bytes).\n",
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
