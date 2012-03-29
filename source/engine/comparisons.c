#include "definitions.h"

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of comparison /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

/*
    Our version 1 algorithm.
*/

char *nicknameA = "imageA", *nicknameB = "imageB";

FILE *contrastlog = NULL;
#define printl(...) fprintf(contrastlog, __VA_ARGS__)

typedef struct filecontrast_struct {
    struct filecontrast_struct * next;
    struct filecontrast_struct * down;
    struct filecontrast_struct * up;
    fileinfo *A, *B;
    char *  filename;       // not including the path
    int64   filediff;       // how this file compares between A and B
    int64   underdiff;      // all differences under this file
    int64   fileaction;     // what we're going to do about it
    int64   underaction;    // all actions taken below here
    int64   fileuseraction; // the overriding action given to us by the user
    int64   underuseraction;// user actions under us
    int64   fileneedsuser;  // things we are awaiting a verdict from the user on
    int64   underneedsuser; // user actions under us
} filecontrast;

#define difftype        0000000000001LL
#define diffextraA      0000000000002LL
#define diffextraB      0000000000004LL
#define diffAnewer      0000000000010LL
#define diffBnewer      0000000000020LL
#define diffAlonger     0000000000040LL
#define diffBlonger     0000000000100LL
#define diffsig         0000000000200LL
#define diffApur        0000000100000LL
#define diffApuw        0000000200000LL
#define diffApux        0000000400000LL
#define diffApgr        0000000010000LL
#define diffApgw        0000000020000LL
#define diffApgx        0000000040000LL
#define diffApor        0000000001000LL
#define diffApow        0000000002000LL
#define diffApox        0000000004000LL
#define diffBpur        0000100000000LL
#define diffBpuw        0000200000000LL
#define diffBpux        0000400000000LL
#define diffBpgr        0000010000000LL
#define diffBpgw        0000020000000LL
#define diffBpgx        0000040000000LL
#define diffBpor        0000001000000LL
#define diffBpow        0000002000000LL
#define diffBpox        0000004000000LL
#define diffAsuid       0001000000000LL
#define diffAsgid       0002000000000LL
#define diffAsticky     0004000000000LL
#define diffBsuid       0010000000000LL
#define diffBsgid       0020000000000LL
#define diffBsticky     0040000000000LL
#define diffuser        0100000000000LL
#define diffgroup       0200000000000LL

// the p stands for propagate, so 2="propagate B's contents", etc.
#define pAaddition   0010000000000LL // these are the only ones where A is not
#define pBaddition   0020000000000LL // the inverse of B -- you also have to
#define pAdeletion   0040000000000LL // swap "addition" with "deletion"
#define pBdeletion   0100000000000LL
#define pAcontents   0000000000001LL
#define pBcontents   0000000000002LL
#define pAuser       0000000000004LL
#define pBuser       0000000000010LL
#define pAgroup      0000000000020LL
#define pBgroup      0000000000040LL
#define pApur        0000000010000LL
#define pApuw        0000000020000LL
#define pApux        0000000040000LL
#define pApgr        0000000001000LL
#define pApgw        0000000002000LL
#define pApgx        0000000004000LL
#define pApor        0000000000100LL
#define pApow        0000000000200LL
#define pApox        0000000000400LL
#define pBpur        0000010000000LL
#define pBpuw        0000020000000LL
#define pBpux        0000040000000LL
#define pBpgr        0000001000000LL
#define pBpgw        0000002000000LL
#define pBpgx        0000004000000LL
#define pBpor        0000000100000LL
#define pBpow        0000000200000LL
#define pBpox        0000000400000LL
#define pAsuid       0000100000000LL
#define pAsgid       0000200000000LL
#define pAsticky     0000400000000LL
#define pBsuid       0001000000000LL
#define pBsgid       0002000000000LL
#define pBsticky     0004000000000LL

filecontrast *newfilecontrast(char *filepath, fileinfo *A, fileinfo *B) {
    filecontrast *this;
    this = (filecontrast*) malloc(sizeof(filecontrast));
    this->next = this->down = this->up = NULL;
    this->A = A; this->B = B;
    this->filediff = this->underdiff = 0;
    this->fileaction = this->underaction = 0;
    this->fileuseraction = this->underuseraction = 0;
    this->fileneedsuser = this->underneedsuser = 0;
    this->filename = strdup(1 + rindex(filepath, '/'));
    return this;
} // newfilecontrast

// if you do this after bottom is all prepared, then it handles flags for you
filecontrast **putcontrastunder(filecontrast *bottom, filecontrast *top,
                                filecontrast **tail)
{
    *tail = bottom;
    bottom->up = top;
    top->underdiff |= bottom->filediff | bottom->underdiff;
    top->underaction |= bottom->fileaction | bottom->underaction;
    top->underuseraction |= bottom->fileuseraction | bottom->underuseraction;
    bottom->fileneedsuser = bottom->fileaction; // initially all changes need user
    bottom->underneedsuser = bottom->underaction;
    return &(bottom->next);
} // putcontrastunder

// this compares the properties of two files
// it does not check their children
// it is separated out from contrastimages simply because its length
// would obscure the simplicity of contrastimages -- it could be inline
filecontrast* contrastfiles(fileinfo* imageA, fileinfo* imageB)
{
    char *ais, *bis, *atype, *btype, buf[77]; // two 38-char halves
    int i;
    filecontrast *contrast;
    #define diffprop(thediff,action) if (1) {                       \
        contrast->filediff |= thediff;                              \
        if (imageA->modificationtime < imageB->modificationtime) {  \
            /* B is newer */                                        \
            contrast->fileaction |= pB ## action;                   \
        } else {                                                    \
            /* A is newer */                                        \
            contrast->fileaction |= pA ## action;                   \
        }                                                           \
    } else /* allows final semicolon without messing up if/else */

    contrast = newfilecontrast(imageA->filename, imageA, imageB);
    // imageA->filename is the same as imageB->filename

    // we fill in file* fields, not under* fields

    if (imageA->filetype == imageB->filetype
        && (imageA->filetype == 3
            || imageA->modificationtime == imageB->modificationtime)
        && (imageA->filetype == 1
            || imageA->filelength == imageB->filelength)
        && imageA->contentsignature[0] == imageB->contentsignature[0]
        && imageA->contentsignature[1] == imageB->contentsignature[1]
        && imageA->contentsignature[2] == imageB->contentsignature[2]
        && imageA->contentsignature[3] == imageB->contentsignature[3]
        && (imageA->filetype == 3
            || imageA->permissions == imageB->permissions)
        && (imageA->filetype == 3
            || !strcmp(imageA->user, imageB->user))) {
        // assume they're the same
        return contrast;
    }

    for (i = 0; i < 77; i++)
        buf[i] = '-';
    buf[76] = 0;
    //printl("%s\n", buf);

    if (imageA->modificationtime < imageB->modificationtime) {
        ais = "older";
        bis = "newer";
        diffprop(diffBnewer, contents);
    } else if (imageA->modificationtime == imageB->modificationtime) {
        ais = "equally old";
        bis = "equally old";
    } else {
        ais = "newer";
        bis = "older";
        diffprop(diffAnewer, contents);
    }
    atype = (imageA->filetype == 1 ? "directory" :
            imageA->filetype == 2 ? "file" :
            imageA->filetype == 3 ? "symbolic link" : "unknown object");
    btype = (imageB->filetype == 1 ? "directory" :
            imageB->filetype == 2 ? "file" :
            imageB->filetype == 3 ? "symbolic link" : "unknown object");
    if (imageA->filetype != imageB->filetype) {
        diffprop(difftype, contents);
    }

    //printl("%s\n", imageA->filename);

    for (i = 0; i < 77; i++)
        buf[i] = ' ';
    i = snprintf(buf, 38, "--- %s ---", nicknameA);
    buf[i] = ' ';
    snprintf(buf + 38, 38, "--- %s ---", nicknameB);
    //printl("%s\n", buf);

    for (i = 0; i < 77; i++)
        buf[i] = ' ';
    i = snprintf(buf, 38, "This %s is %s.", atype, ais);
    buf[i] = ' ';
    snprintf(buf + 38, 38, "This %s is %s.", btype, bis);
    //printl("%s\n", buf);

    {
        time_t at = imageA->modificationtime, bt = imageB->modificationtime;
        for (i = 0; i < 77; i++)
            buf[i] = ' ';
        i = snprintf(buf, 38, "modified: %s", ctime(&at));
        buf[i-1] = buf[i] = ' ';
        snprintf(buf + 38, 38, "modified: %s", ctime(&bt));
        //printl("%s", buf);
    }

    if (imageA->filetype != 1 && imageB->filetype != 1
            && imageA->filelength != imageB->filelength) {
        for (i = 0; i < 77; i++)
            buf[i] = ' ';
        i = snprintf(buf, 38, "Size: %s bytes (%s)",
            commanumber(imageA->filelength),
            imageA->filelength > imageB->filelength ? "larger" : "smaller");
        buf[i] = ' ';
        snprintf(buf + 38, 38, "Size: %s bytes (%s)",
            commanumber(imageB->filelength),
            imageB->filelength > imageA->filelength ? "larger" : "smaller");
        //printl("%s\n", buf);
        if (imageA->filelength > imageB->filelength)
            diffprop(diffAlonger, contents);
        else
            diffprop(diffBlonger, contents);
    }

    if (strcmp(imageA->user, imageB->user)) {
        for (i = 0; i < 77; i++)
            buf[i] = ' ';
        i = snprintf(buf, 38, "Owned by: %s", imageA->user);
        buf[i] = ' ';
        snprintf(buf + 38, 38, "Owned by: %s", imageB->user);
        //printl("%s\n", buf);
        diffprop(diffuser, user);
    }

    if (imageA->permissions != imageB->permissions) {
        for (i = 0; i < 77; i++)
            buf[i] = ' ';
        i = snprintf(buf, 38, "permissions: %c%c%c%c%c%c%c%c%c%c%c%c%c",
                                (imageA->permissions & S_ISUID) ? 'u' : '.',
                                (imageA->permissions & S_ISGID) ? 'g' : '.',
                                (imageA->permissions & S_ISVTX) ? 's' : '.',
                                "d-l?"[imageA->filetype - 1],
                                (imageA->permissions & S_IRUSR) ? 'r' : '-',
                                (imageA->permissions & S_IWUSR) ? 'w' : '-',
                                (imageA->permissions & S_IXUSR) ? 'x' : '-',
                                (imageA->permissions & S_IRGRP) ? 'r' : '-',
                                (imageA->permissions & S_IWGRP) ? 'w' : '-',
                                (imageA->permissions & S_IXGRP) ? 'x' : '-',
                                (imageA->permissions & S_IROTH) ? 'r' : '-',
                                (imageA->permissions & S_IWOTH) ? 'w' : '-',
                                (imageA->permissions & S_IXOTH) ? 'x' : '-');
        buf[i] = ' ';
        snprintf(buf + 38, 38, "permissions: %c%c%c%c%c%c%c%c%c%c%c%c%c",
                                (imageB->permissions & S_ISUID) ? 'u' : '.',
                                (imageB->permissions & S_ISGID) ? 'g' : '.',
                                (imageB->permissions & S_ISVTX) ? 's' : '.',
                                "d-l?"[imageB->filetype - 1],
                                (imageB->permissions & S_IRUSR) ? 'r' : '-',
                                (imageB->permissions & S_IWUSR) ? 'w' : '-',
                                (imageB->permissions & S_IXUSR) ? 'x' : '-',
                                (imageB->permissions & S_IRGRP) ? 'r' : '-',
                                (imageB->permissions & S_IWGRP) ? 'w' : '-',
                                (imageB->permissions & S_IXGRP) ? 'x' : '-',
                                (imageB->permissions & S_IROTH) ? 'r' : '-',
                                (imageB->permissions & S_IWOTH) ? 'w' : '-',
                                (imageB->permissions & S_IXOTH) ? 'x' : '-');
        //printl("%s\n", buf);
    {
        time_t at = imageA->metamodtime, bt = imageB->metamodtime;
        for (i = 0; i < 77; i++)
            buf[i] = ' ';
        i = snprintf(buf, 38, "  (since %s", ctime(&at));
        if (i > 38)
            i = 38;
        buf[i-1] = ')';
        buf[i] = ' ';
        i = 38 + snprintf(buf + 38, 38, "  (since %s", ctime(&bt));
        if (i > 76)
            i = 76;
        buf[i-1] = ')';
        buf[i] = 0;
        //printl("%s\n", buf);
    }
    }

    for (i = 0; i < 77; i++)
        buf[i] = '-';
    buf[76] = 0;
    //printl("%s\n", buf);

    return contrast;
} // contrastfiles

// recursively compares the contents of two directories
// imageA and imageB are eldest siblings, contrast is for the parent
// this calls contrastfiles to compare properties of same-named files
void contrastimages(fileinfo* imageA, fileinfo* imageB, filecontrast *contrast)
{
    fileinfo *ia, *ib;
    filecontrast *subcontrast, **tail = &(contrast->down);

    // the lists are sorted, yay, so we can work quickly
    // first look for things in A that aren't in B
    for (ia = imageA, ib = imageB; ia != NULL; ia = ia->next) {
        int q = 0; // to silence compiler
        while (ib != NULL && (q = strcmp(rindex(ia->filename, '/'),
                                         rindex(ib->filename, '/'))) > 0)
            ib = ib->next;
        if (ib != NULL && q == 0)
            goto nextaonly; // found a match
        subcontrast = newfilecontrast(ia->filename, ia, NULL);
        subcontrast->filediff = diffextraA;
        subcontrast->fileaction = (ia->modificationtime > 0)
                                  ? pAaddition : pBdeletion;
        tail = putcontrastunder(subcontrast, contrast, tail);
        //printl("File %s only exists in %s\n", ia->filename, nicknameA);
        nextaonly:;
    }
    // now look for things in B that aren't in A
    for (ib = imageB, ia = imageA; ib != NULL; ib = ib->next) {
        int q = 0; // to silence compiler
        while (ia != NULL && (q = strcmp(rindex(ib->filename, '/'),
                                         rindex(ia->filename, '/'))) > 0)
            ia = ia->next;
        if (ia != NULL && q == 0)
            goto nextbonly; // found a match
        subcontrast = newfilecontrast(ib->filename, NULL, ib);
        subcontrast->filediff = diffextraB;
        subcontrast->fileaction = (ib->modificationtime > 0)
                                  ? pBaddition : pAdeletion;
        tail = putcontrastunder(subcontrast, contrast, tail);
        //printl("File %s only exists in %s\n", ib->filename, nicknameB);
        nextbonly:;
    }
    // now compare those things whose names match
    for (ia = imageA, ib = imageB; ia != NULL && ib != NULL; ) {
        int q = strcmp(rindex(ia->filename, '/'), rindex(ib->filename, '/'));
        if (q < 0)
            ia = ia->next;
        else if (q > 0)
            ib = ib->next;
        else {
            // hey, they match!
            filecontrast *subcontrast;
            subcontrast = contrastfiles(ia, ib);
            if (ia->down != NULL || ib->down != NULL) {
                contrastimages(ia->down, ib->down, subcontrast);
            }
            tail = putcontrastunder(subcontrast, contrast, tail);
            ia = ia->next;
            ib = ib->next;
        }
    }
} // contrastimages

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of comparison ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

