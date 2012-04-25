
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <sys/dir.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <curses.h>
#include <signal.h>
#include <sys/time.h>
#include <utime.h>
#include <sys/uio.h>
#include <pthread.h>
#ifndef pthread_attr_default
#define pthread_attr_default NULL
#endif

extern FILE* ourerr; // we use this instead of stderr

#define printerr(...) do { fprintf(ourerr, __VA_ARGS__); fflush(ourerr); } while (0)

#define assert(test)                                                    \
        if (!(test)) {                                                  \
            printerr("Assertion failed: (%s), "                         \
                "function %s, file %s, line %d.\n",                     \
                #test, __FUNCTION__, __FILE__, __LINE__);               \
            __builtin_trap(); /* make THIS thread crash */              \
        } else // (standard assert.h just throws signal at whole process)

#define READ_END 0  // for pipes
#define WRITE_END 1

#define perm_mask (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)

typedef int int32;
typedef unsigned int uint32;
typedef long long int int64;
typedef unsigned long long int uint64;

#define programversionnumber    ((int32) 1)
#define logfileversionnumber    ((int32) 1)
#define specsfileversionnumber  ((int32) 1)
#define devicefileversionnumber ((int32) 1)
#define magiccookie             ((int32) 1331925123)
// cookie is from letters m=13, c=3, s=19, y=25, n=12, c=3.
// four bytes are: (high byte first) 79 99 144 131 (-112 -125) (hex: 4f 63 90 83)
// first four letters of file are OceE (e has a circumflex, E is acute)

#define device_file_path "./data/device"
#define specs_file_path "./config/specs"

#define device_id_size 16

// limit depth and file name length in virtual tree
#define virtual_path_depth_max 1024
#define virtual_file_name_max 256

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of data types /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

typedef int32 listint; // the type of int used in intlists, must match putintlist

typedef struct intlist_struct {
    int32 count;
    listint *values; // allocated memory belonging to this struct
} *intlist;

extern intlist OurChildren; // pids of all child processes, so we can kill them on exit

typedef struct stringlist_struct {
    struct stringlist_struct *next;
    char *string;
} stringlist;

typedef struct bytestream_struct {
    char *data;       // not necessarily usable as regular string! (allowed to contain '\0')
    char *head;       // the current writing position
    uint32 len;       // bytes written into stream
    uint32 streamlen; // total allocated memory of bytestream
}* bytestream;

typedef struct queuenode_struct {
    struct queuenode_struct *next;
    struct queuenode_struct *prev;
    void *data;
} *queuenode;

typedef struct queue_struct {
    queuenode head;
    queuenode tail;
    uint32 size;
} *queue;

// mcsync's notion of an aspect's "history" is captured in the history struct

typedef struct history_struct { // for every tracked file aspect, latest source
    struct history_struct *next;
    int64 trackingnumber; // specifies both device and file of aspect owner
    int32 devicetime; // device-specific increasing integer, negated if issame
} *history;

// the fileinfo struct is our platform independent representation of a unix file

typedef struct extendedattributes_struct { // a helper struct for fileinfo
    struct  extendedattributes_struct *next; // we keep them sorted by name
    char   *name;       // UTF-8
    int     size;
    int     literal;    // 1: contents = attr value, 0: contents = hash of value
    char   *contents;   //                          -1: contents = missing
    history hist_attr;
} *extendedattributes;

typedef struct fileinfo_struct { // everything to know about a file on disk
    struct fileinfo_struct *next; // directory listing is linked list
    struct fileinfo_struct *down; // if this is a directory, its contents
    struct fileinfo_struct *up;   // the parent directory
    int32   inode;
    int32   filetype;       // 1=directory, 2=plain file, 3=symlink, 4=other
    int32   numchildren;    // 0 if not a directory or if empty directory
    int32   subtreesize;    // number of files including self, children, grand...
    int64   subtreebytes;   // sum of lengths of all files counted in subtreesize
    int32   hardlinks;
    struct fileinfo_struct *nexthardlink; // circular linked list
    int64   accesstime;     // times are in seconds since 1970
    int64   modificationtime;
    int64   metamodtime;    // updated when inode (except atime) is altered
    int64   birthtime;      // of inode, on mac (through lstat64)
    int64   filelength;
    int32   contentsignature[4];
    int32   permissions;
    int32   numericuser;
    int32   numericgroup;
    char   *user;
    char   *group;
    char   *filename;       // includes path
    extendedattributes xattrs; // these each include their own history+missingness
    int32   existence; // two bits for each of the five aspects
    // the following apply when we know what tracked file this file on disk is for
    history hist_modtime;
    history hist_contents;
    history hist_perms; // includes group, owner: all security
    history hist_name; // filename, no path
    history hist_loc; // parent directory
    int64   trackingnumber; // together with devicetime,
    int32   devicetime; // this allows all the aspect histories to skip this entry
} fileinfo;

// devices

typedef enum { // if you change this, update status_word in headquarters.c
    status_inactive,
    status_reaching,
    status_connected,
} status_t;

typedef struct devicelocater_struct { // all you need for locating the device
    // before we're connected, we need the net address and file system address
    stringlist *ipaddrs;    // ways to reach that device
    char *whichtouse;       // which of the ipaddrs is the one
    // after we're connected, we need the router address
    int routeraddr;     // the current plug identifier for message routing (or -1)
} devicelocater;

typedef struct device_struct { // all you need to know about an arbitrary device
    struct device_struct *next;
    char *nickname;     // a user-friendly name
    char *deviceid;     // a long random unique id string, never changes on device
    int32 linked;       // boolean, whether this device had sucessfully agreed
                        // upon an id with a local or remote worker.
    status_t status;    // whether it is currently connected, what it's doing
    stringlist *networks;       // networks this device can usefully reach
    char *preferred_hq;         // if started on this device, use this hq (if set)
    devicelocater reachplan;    // how to reach the device
} device;

extern device *devicelist; // the list of devices we know about

// the virtual tree

typedef struct graft_struct { // all you need to know about an arbitrary graft
    struct graft_struct *next;
    device *host;            // which device is being grafted
    char *hostpath;          // directory or file on device
    char *virtualpath;       // virtual location
    stringlist *prunepoints; // virtual paths of descendants to ignore
} graft;

extern graft *graftlist; // the list of grafts we know about

typedef struct graftee_struct { // describes any real file that maps onto the tree
    struct graftee_struct *next;
    graft *source;
    fileinfo *realfile;
} *graftee;

typedef struct virtualnode_struct {
    struct virtualnode_struct *next; // next sibling
    struct virtualnode_struct *prev; // previous sibling
    struct virtualnode_struct *down; // first offspring
    struct virtualnode_struct *up;   // parent
    graftee grafteelist; // real files that should be synced to match this
    graftee bootedlist; // real files that will be changed to a different name
    graftee graftroots; // list of grafts that have this as their virtual root
    graftee graftends; // grafts having this as a prune point. <&^: no realfiles
    // virtual content (that synced stuff should match)
    char    *name;        // our name (not including the path) (free when done)
    int32   filetype;     // 1=directory, 2=plain file, 3=symlink, 4=other, 0=stub
                          // stub means we are on a path to a graft & have no data
    // info about the desired state of the file
    // (graftee list has info about existing states of the file)
    int64   accesstime;
    int64   modificationtime;
    graftee contents;    // specifies contents by pointing to an existing file
    int32   permissions;
    int32   numericuser;
    int32   numericgroup;
    char    *user; // free when done
    char    *group; // free when done
    // info about how the desired state is a change from the previous state(s)
    int32   redyellow; // red means conflict, yellow means user-specified
    int32   redgreen; // green means propagating change, no color means no change
    // some interface stuff
    int32   numchildren;  // 0 if not a directory or if empty directory
    int32   subtreesize;  // number of files including self, children, grand...
    int32   subtreebytes; // sum of lengths of all plain files in subtree
    int32   cols; // columns to show in interface (if this is a directory)
    int32   firstvisiblenum; // first visible child (-1 = pls recompute)
    struct virtualnode_struct *firstvisible; // first visible child
    int32   selectionnum; // which child is selected (-1 = pls recompute)
    struct virtualnode_struct *selection; // which child is selected
    int32   colwidth; // width of this column (for parent dir listing)
    int32   touched; // node has changed since last query to HQ, _only_ set on CMD
} virtualnode;


// messaging

// messages are sent between agents -- every agent has a postal address (an integer)
// there are 4 types of agents
// 1. tui = command = CMD = 2
// 2. headquarters = HQ = 1
// 3. worker = WKR = 3
// 4. parent = 0
// at a given post office, there is a connection_list of plugs, each listing the agents
// which lie in that direction.  the parent plug is used for all unknown agents.

typedef struct message_struct { // threads communicate by sending these
    int32 source; // return address
    intlist destinations; // allocated to this struct if not NULL
    int64 type;
    int64 len;
    char* data; // allocated to this struct if not NULL
    int nextisready; // really one nice atomic bit: 0 (no next) or 1 (next ready)
    struct message_struct *next;
} *message;

typedef struct connection_struct { // all a router needs to provide plug  huh? XXX
    int32           plugnumber;     // the plugnumber associated with this connection
    intlist         thisway; // what sites lie in this direction
    message         messages_tokid_tail, messages_fromkid_head; // for router
    message         messages_tokid_head, messages_fromkid_tail; // for kid r/w:h/t
    pthread_t       listener; // on local connections this also writes the output
    pthread_t       stream_shipper, stream_receiver, stderr_forwarder;  // for remote & parent
    FILE            *tokid, *fromkid, *errfromkid;      // for remote & parent
    struct connection_struct *next;
    // from here on down is only used during creation of the connection
    char            *address;       // the address we try to connect to
    pthread_t*      local_thread; // ptr to global thread var, NULL = spawn remote
    int             kidinpipe[2], kidoutpipe[2], kiderrpipe[2]; // filedescriptors
                    // r/w:  write to [WRITE_END=1], read from [READ_END=0]
    int             processpid; // the id of the process we spawn to reach remote
} *connection; // also known as a plug

extern connection cmd_plug, hq_plug, recruiter_plug, parent_plug; // for direct access

#define hq_int          1
#define cmd_int         2
#define recruiter_int   3
#define firstfree_int   4

#define msgtype_connectdevice       1
#define msgtype_newplugplease       2
#define msgtype_removeplugplease    3
#define msgtype_recruitworker       4
#define msgtype_failedrecruit       5
#define msgtype_info                6
#define msgtype_workerisup          7
#define msgtype_connected           8
#define msgtype_disconnect          9
#define msgtype_identifydevice      10
#define msgtype_deviceid            11
#define msgtype_listvirtualdir      12
#define msgtype_virtualdir          13
#define msgtype_touch               14
// ^ mark virtual node as touched (changed) on cmd
#define msgtype_scanvirtualdir      15
// ^ the message contains a virtual path (usually a request from cmd to hq)
#define msgtype_scan                16
// ^ the message contains a host path and prune points (usually a request from hq to wrks)
#define msgtype_exit                17
// ^ the destinatin is requested to cleanexit

// if you change these^, change msgtypelist in communication.c

#define slave_start_string "this is mcsync"
#define hi_slave_string "you are "

void (*cmd_thread_start_function)(); // this is the function called by the cmd thread.
                                     // depending on user choice this is currently either TUImain or climain

typedef void* (*channel_initializer) (void* arguments); // is the function provided to channel_launch
                                                        // which will turn the newly created thread into
                                                        // the corresponding agent

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of data types ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//////// some utility functions

char* hostname(void); // caches answer -- do not alter or free!

char* strdupcat(const char* first, ...); // allocates new string, last arg must be NULL
char *commanumber(int64 n); // returns human-readable integer in reused buffer

// error handling
void ourperror(char* whatdidnotwork); // writes to ourerr, not stderr
void cleanexit(int code); // kills children and exits

// progress monitoring
extern int didtick, amticking;
extern int progress1, progress2, progress3, progress4;
void startticking(void);
void stopticking(void);

// file io
void put32(FILE* output, int32 data);
void put32safe(FILE* output, int32 data);
void put64(FILE* output, int64 data);
void putstring(FILE* output, char* s);
int32 get32(FILE* input);
int32 get32safe(FILE* input);
int64 get64(FILE* input);
char* getstring(FILE* input, char delimiter); // returns new string; free when done
void tohex(unsigned char c, char* a, char* b);

// file locking
int32 getlockfile(char *path, int32 maxtime);
int32 releaselockfile(char *path); //returns 1 if sucessful, 0 otherwise

// communication control
void waitforstring(FILE* input, char* string);
void waitforsequence(FILE* input, char* sequence, int len, int echo);

// general message system
void sendmessage(connection plug, int recipient, int type, char* what);
void sendmessage2(connection plug, int recipient, int type, char* what);
void nsendmessage(connection plug, int recipient, int type, char* what, int len);

int receivemessage(connection plug, listint* src, int64* type, char** data);
char* secondstring(char* string); // read the second string from a sendmessage2 type message (contains two strings)

// virtual node comnunication between cmd and hq
void sendvirtualnoderquest(virtualnode *root, virtualnode *node); // sends a list virtual node request, only to be used from cmd
void sendvirtualdir(connection plug, int recipient, char *path, virtualnode *dir);
void receivevirtualdir(char *source, char **path, queue receivednodes);

// scan communication
void sendscanvirtualdirrequest(virtualnode *root, virtualnode *node);
void sendscancommand(connection plug, int recipient, char *scanroot, stringlist *prunepoints);
void receivescancommand(char *source, char **scanroot, stringlist **prunepoints);

// recruiter communication
void sendrecruitcommand(connection plug, int32 plugnum, char *address); // always sent to recruiter
void receiverecruitcommand(char *source, int32 *plugnum, char **address); // used by recruiter

void sendfailedrecruitmessage(int32 recipient, int32 plugnum); // used by recruiter
void receivefailedrecruitmessage(char *source, int32 *plugnum);

void sendnewplugresponse(int32 recipient, char *theirreference, int32 plugnum); // used by recruiter
void receivenewplugresponse(char *source, char **reference, int32 *plugnum);

void sendremoveplugpleasecommand(connection plug, int32 plugnum); // always sent to recruiter
void receiveremoveplugpleasecommand(char *source, int32 *plugnum); // used by recruiter

////////  general purpose data structures

// intlist
intlist emptyintlist(void);
void freeintlist(intlist skunk);
void addtointlist(intlist il, listint n); // keeps list sorted, works for multisets
void removefromintlist(intlist il, listint n);

// stringlist
void freestringlist(stringlist *skunk);

// bytestream - dynamically re-allocated string (doubling allocated memory when full)
bytestream initbytestream(uint32 len); // allocates new bytestream, free when done
void freebytestream(bytestream b);
void bytestreaminsert(bytestream b, void *data, int32 len);
void bytestreaminsertchar(bytestream b, char str);

// queue - implemented using a doubly linked list
queue initqueue(void); // allocates memory, free when done
void freequeue(queue q); // does not free the data!!

void queueinsertafter(queue q, queuenode qn, void *data);
void queueinsertbefore(queue q, queuenode qn, void *data);
void queueinserthead(queue q, void *data);
void queueinserttail(queue q, void *data);
void *queueremove(queue q, queuenode qn); // returns pointer to data, because it will not be freed!!

//////// actual interaction between program parts

// agent system
void TUImain(void);
void climain(void);
void hqmain(void);
void workermain(connection workerplug);
void reqruitermain(void);

void routermain(int master, int plug_id);

// post office (routermain) and recruiter interaction
void add_connection(connection *storeplughere, int plugnumber);
connection remove_connection(int32 plugnumber);

connection findconnectionbyplugnumber(int32 plugnumber);
void channel_launch(connection plug, channel_initializer initializer);

void* localworker_initializer(void *voidplug); // only used by routermain and recruiter
void* forward_raw_errors(void* voidplug);
void* stream_receiving(void* voidplug);
void* stream_shipping(void* voidplug);

void freemessage(message skunk);

// specification file handling
void readspecsfile(char *specsfile); // sets up devicelist and graftlist
int32 specstatevalid(void); // returns 1 if there exists a device with a reachplan and a graft;
int32 writespecsfile(char *specsfile); // writes devicelist and graftlist

// virtual tree
void initvirtualroot(virtualnode *root); // used by HQ and CMD (e.g. tui.c)
virtualnode *conjuredirectory(virtualnode* root, char *dir); // chars in dir string must be writable
virtualnode *findnode(virtualnode *root, char *path); // returns NULL if not found, path must be writable
void virtualnodeaddchild(virtualnode **parent, virtualnode **child);
void virtualnoderemovenode(virtualnode **node); // removes and frees node and all its children
void overwritevirtualnode(virtualnode **oldnode, virtualnode **newnode); // frees oldnode
void getvirtualnodepath(bytestream b, virtualnode *root, virtualnode *node); // writes the path of node into b
void freevirtualnode(virtualnode *node);

// disk scan for workers
fileinfo* formimage(char* filename); // get inode info for filename (which includes path) and for any subdirectories
void writesubimage(FILE* output, fileinfo* subimage);

// tui specific
void raw_io(void);

extern int doUI; // can be turned off by -batch option
extern int password_pause; // signals when input should be allowed to go to ssh
extern int waitmode; // changed to 1 on startup if "-wait" flag is provided

void TUIstart2D(void); // enter 2D mode
void TUIstop2D(void); // leave 2D mode, go back to scrolling terminal
