#include "definitions.h"

virtualnode virtualroot; // has no siblings and no name
                            // only to be used by the hq thread


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of algo main //////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

char* statusword[] = {
    "inactive",
    "reaching",
    "connected",
};

virtualnode *conjuredirectory(virtualnode* root, char *dir) // chars in dir string must be writable
// dir must not end with / or contain //
// this routine creates all virtual files except for the root (in initvirtualroot)
{
    virtualnode *parent, *ans;
    char *pos;
    if (dir[0] != '/') // in general true only for "" (otherwise dir was bad)
        return root;
    pos = rindex(dir, '/');
    *pos = 0; // truncate string
    parent = conjuredirectory(root, dir);
    *pos = '/';
    for (ans = parent->down; ans != NULL; ans = ans->next)
        if (ans->filetype <= 1 && !strcmp(ans->name, pos + 1))
            return ans;
    // it doesn't exist
    ans = (virtualnode*) malloc(sizeof(virtualnode));
    ans->next = parent->down;
    parent->down = ans;
    if (ans->next != NULL)
        ans->next->prev = ans;
    ans->prev = NULL;
    ans->down = NULL;
    ans->up = parent;
    ans->grafteelist = NULL;
    ans->bootedlist = NULL;
    ans->graftroots = NULL;
    ans->graftends = NULL;
    ans->name = strdup(pos + 1);
    ans->filetype = 0; // 0=stub
    // that was the structural stuff, now some adorning interface info
    ans->redyellow = 0;
    ans->redgreen = 0;
    ans->numchildren = 0;
    ans->subtreesize = 1;
    ans->subtreebytes = 0;
    ans->cols = 6;
    ans->firstvisiblenum = -1; // -1 means needs recompute
    ans->firstvisible = NULL;
    ans->selectionnum = -1; // -1 means needs recompute
    ans->selection = NULL;
    parent->numchildren++;
    parent->firstvisiblenum = -1;
    parent->selectionnum = -1;
    while (parent != NULL) {
        parent->subtreesize++;
        parent = parent->up;
    }
    return ans;
} // conjuredirectory

void removedirectory(virtualnode *skunk) // skunk should be empty
{
    virtualnode *parent = skunk->up;
    if (skunk->prev != NULL)
        skunk->prev->next = skunk->next;
    else
        skunk->up->down = skunk->next;
    if (skunk->next != NULL)
        skunk->next->prev = skunk->prev;
    free(skunk->name);
    free(skunk);
    parent->numchildren--;
    parent->firstvisiblenum = -1;
    parent->selectionnum = -1;
    while (parent != NULL) {
        parent->subtreesize--;
        parent = parent->up;
    }
} // removedirectory

void mapgraftpoint(graft *source, char *where, int pruneq, int deleteq)
// adds (or deletes, if deleteq) the graft "source" to the virtual directory
// "where" as a graftroot (or graftend, if pruneq)
{
    virtualnode *v;
    graftee *gee;

    v = conjuredirectory(&virtualroot, where);
    if (pruneq) // it is a prune point
        gee = &(v->graftends);
    else // it is a graft root
        gee = &(v->graftroots);
    if (deleteq) { // we should remove it
        while (*gee != NULL && (*gee)->source != source)
            gee = &((*gee)->next);
        if (*gee == NULL) {
            // we're supposed to delete it, but it's not there
            // this should never happen
        } else {
            graftee skunk = *gee;
            *gee = skunk->next;
            free(skunk);
            // now check to see if we should remove stub as well
            while (v->graftroots == NULL && v->graftends == NULL
                    && v->grafteelist == NULL && v->bootedlist == NULL
                    && v->down == NULL && v->filetype == 0
                    && v->up != NULL) {
                // remove v
                virtualnode *parent = v->up;
                if (v->prev != NULL)
                    v->prev->next = v->next;
                else
                    v->up->down = v->next;
                if (v->next != NULL)
                    v->next->prev = v->prev;
                free(v->name);
                free(v);
                v = parent;
            }
        }
    } else { // we should add it
        while (*gee != NULL)
            gee = &((*gee)->next);
        *gee = (graftee) malloc(sizeof(struct graftee_struct));
        (*gee)->next = NULL;
        (*gee)->source = source;
        (*gee)->realfile = NULL; // not used
    }
} // mapgraftpoint

void conjuregraftpoints(void) // make graft roots and prune points exist in v. dir
{
    graft *g;
    stringlist *s;

    for (g = graftlist; g != NULL; g = g->next) {
        mapgraftpoint(g, g->virtualpath, 0, 0);
        for (s = g->prunepoints; s != NULL; s = s->next) {
            mapgraftpoint(g, s->string, 1, 0);
        }
    }
} // conjuregraftpoints

// Used by CMD and HQ to initialize their virtual trees
void initvirtualroot(virtualnode *root)
{
    // set up root directory
    root->next = NULL;
    root->prev = NULL;
    root->up = NULL;
    root->down = NULL; // will change when children are added
    root->grafteelist = NULL;
    root->bootedlist = NULL;
    root->graftroots = NULL;
    root->graftends = NULL;
    root->name = "";
    root->filetype = 1; // 1 = directory
    // now for the interface stuff
    root->redyellow = 0;
    root->redgreen = 0;
    root->numchildren = 0;
    root->subtreesize = 1;
    root->subtreebytes = 0;
    root->cols = 6;
    root->firstvisiblenum = -1;
    root->firstvisible = NULL;
    root->selectionnum = -1;
    root->selection = NULL;
    root->touched = 1;
} // initvirtualroot

void virtualtreeinit(void)
{
    // specs file is read in routermain
    initvirtualroot(&virtualroot); // initialize virtual tree
    conjuregraftpoints(); // make the tree include spots indicated by the grafts
} // virtualtreeinit

void freegrafteelist(graftee gee)
{
    graftee skunk;
    while (gee != NULL) {
        skunk = gee;
        gee = skunk->next;
        free(skunk);
    }
} // freegraftee

void freevirtualnode(virtualnode *node)
{
    free(node->name);
    free(node->user);
    free(node->group);
    freegrafteelist(node->grafteelist);
    freegrafteelist(node->bootedlist);
    freegrafteelist(node->graftroots);
    freegrafteelist(node->graftends);
    free(node);
} // freevirtualnode

void virtualnoderemovenode(virtualnode **node) // removes and frees node and all its children
{
    virtualnode *skunk = *node;
    virtualnode *child;

    if (!skunk)
        return;
    if (!skunk->prev) // the head of the list, reconnect siblings with parent
        skunk->up->down = skunk->next;
    else {
        skunk->prev->next = skunk->next;
        if (skunk->next)
            skunk->next->prev = skunk->prev;
    }
    for (child = skunk->down; child != NULL; child = child->next)
        virtualnoderemovenode(&child);

    freevirtualnode(skunk);
} // virtualnoderemovechild

void virtualnodeaddchild(virtualnode **parent, virtualnode **child)
{
    if ((*parent)->down) {
        (*child)->next = (*parent)->down;
        (*child)->next->prev = *child;
        (*child)->prev = NULL;
    } else
        (*child)->next = NULL;

    (*parent)->down = *child;
    (*child)->up = *parent;
} // virtualnodeaddchild

void overwritevirtualnode(virtualnode **oldnode, virtualnode **newnode) // frees oldnode
{
    virtualnode *new = *newnode;
    virtualnode *old = *oldnode;

    if (old->up) {
        new->up = old->up;
        old->up->down = new;
    }

    if (old->down) {
        new->down = old->down;
        old->down->up = new;
    }

    if (old->next) {
        new->next = old->next;
        old->next->prev = new;
    }

    if (old->prev) {
        new->prev = old->prev;
        old->prev->next = new;
    }

    freevirtualnode(old);
} // overwritevirtualnode

void getvirtualnodepath(bytestream b, virtualnode *root, virtualnode *node)
{
    if (node == root) {
        bytestreaminsertchar(b, '/');
        return;
    }
    getvirtualnodepath(b, root, node->up);
    if (node->up != root)
        bytestreaminsertchar(b, '/');
    bytestreaminsert(b, node->name, strlen(node->name));
} // getvirtualnodepath

void setstatus(int32 who, status_t newstatus)
{
    device* mach;
    status_t oldstatus;
    char buf[90];

    // find device for "who"
    for (mach = devicelist; mach != NULL; mach = mach->next) {
        if (mach->reachplan.routeraddr == who)
            break;
    }
    if (mach == NULL) {
        printerr("Error: Machine %d not in machine list.\n", who);
        return;
    }
    // set the new status
    oldstatus = mach->status;
    mach->status = newstatus;
    // take any status-change-based actions
    switch (newstatus) {
        case status_inactive:
                sendmessage(hq_plug, cmd_int, msgtype_disconnect,
                            mach->deviceid);
            break;
        case status_reaching:
            break;
        case status_connected:
                snprintf(buf, 90, "%s", mach->deviceid); // unnecessary to use buf
                sendmessage(hq_plug, cmd_int, msgtype_connected, buf);
            break;
    }
    printerr("Changed status of %d (%s) from %d (%s) to %d (%s).\n",
                who, mach->nickname,
                oldstatus, statusword[oldstatus],
                newstatus, statusword[newstatus]);
} // setstatus

static int NextFreeAddress = firstfree_int;

void hq_reachfor(char* deviceid, char* reachfrom_deviceid) // RFID can be NULL
{
    device* target_m = NULL;
    device* m;
    int reachfrom_addr = -1;
    char buf[90];

    for (m = devicelist; m != NULL; m = m->next) {
        if (!strcmp(m->deviceid, deviceid))
            target_m = m;
        if (reachfrom_deviceid
            && !strcmp(m->deviceid, reachfrom_deviceid)) {
            if (m->status == status_connected) {
                reachfrom_addr = m->reachplan.routeraddr;
            } else {
                printerr("Error: Hop-from machine (id \"%s\") not connected.\n",
                        reachfrom_deviceid);
                return;
            }
        }
    }
    if (target_m == NULL) { // can this happen?
        printerr("Error: Target machine id \"%s\" not found.\n", deviceid);
        return;
    }
    if (reachfrom_addr == -1) {
        if (reachfrom_deviceid == NULL) { // didn't find it cause we didn't look
            reachfrom_addr = topworker_int;
        } else {
            printerr("Error: Hop-from machine id \"%s\" not found.\n",
                    reachfrom_deviceid);
            return;
        }
    }

    // start a remote mcsync for this device
    target_m->reachplan.routeraddr = NextFreeAddress; // awaiting status_connected
    setstatus(NextFreeAddress, status_reaching);
    snprintf(buf, 90, "%s%c%d", deviceid, 0, NextFreeAddress);
    sendmessage2(hq_plug, reachfrom_addr, msgtype_newplugplease, buf);
    // Notice we don't send the device target_m!
    // It will be recovered from the deviceid by topworker in channel_launch.
    // This lets us use a worker besides topworker (multi-hop case), because
    // the deviceid for a device is the same everywhere.
    NextFreeAddress++;
} // hq_reachfor

int waitmode = 0; // changed to 1 on startup if "-wait" flag is provided

void hq_init(void) // called as soon as local worker is ready to do work
{
    if (waitmode)
        return;
    // here we do whatever the configuration file tells us to do on startup

} // hq_init

virtualnode *findnode(virtualnode *root, char *path) // threads '/' as delimiter,
// e.g. "home/tmp" is equal to "[/]*home[/]+tmp[/]*)" use "validfullpath" for validation.
{
    virtualnode *node = root;
    char delimiter[] = "/";
    char *rest = NULL;
    char *pathdup = strdup(path);
    char *name = strtok_r(pathdup, delimiter, &rest);

    // node is only null if we could not find a node with name
    while (name != NULL && node != NULL) {
        for (node = node->down; node != NULL; node = node->next) {
            if (!strcmp(node->name, name))
                break;
        }
        // get next node name
        name = strtok_r(NULL, delimiter, &rest);
    }
    free(pathdup);
    return node;
} // findnode

int validfullpath(char *path) // returns 1 if path is a valid full virtual path
// checks file name length and tree depth. Only accepts paths which start at
// root '/' and do not end in '/'. File names can't start with a space.
{
    uint32 namelen = 0;
    uint32 depth = 1;

    // path has to start at root
    if (*path != '/')
        return 0;
     // root itself is always valid, also we start at depth 1
    if (*(path++) == '\0')
        return 1;

    while (*path != '\0') {
        // a new file name starts and we are one level deeper
        if (*path == '/') {
            // no empty file names allowed
            if (!namelen)
                return 0;
            // check for maximum depth
            depth++;
            if (virtual_path_depth_max < depth)
                return 0;
            // a new file begins
            namelen = 0;
        } else if (*path == ' ' && *(path-1) == '/') {
            // file names can't start with whitespaces
            return 0;
        } else {
            namelen++;
            if (virtual_file_name_max < namelen)
                return 0;
        }
        path++;
    }
    // the root path would've returned already, so last character can't be '/'
    if (*(path--) == '/')
        return 0;

    return 1;
} // validfullpath

void hq_scan(char *scanrootpath)
{
    graft *g;
    char *scanpathcharacter, *graftpathcharacter;

    if (!validfullpath(scanrootpath)) {
        printerr("Error: Headquarters got invalid scan root path: %s\n", scanrootpath);
        return;
    }

    for (g = graftlist; g != NULL; g = g->next) {
        if (!(g->host->status == status_connected))
            continue;

        // if the requested path to scan and the virtualpath of the graft
        // are the same for the length of the virtual path of the graft,
        // we need to include said graft.

        // Consider the scan request for "/Home/test" and a graft with virtual
        // path "/Home". The first 5 charachters are the same and because this
        // is the length of the virtual path of the graft, it means the requested
        // scan path is a child of the graft virtual root.
        scanpathcharacter = scanrootpath;
        graftpathcharacter = g->virtualpath;

        while (*graftpathcharacter && *graftpathcharacter == *scanpathcharacter) {
            graftpathcharacter++;
            scanpathcharacter++;
        }

        if (*graftpathcharacter == '\0') { // we reached the end of the graft virtual path
            char *physicalpath = strdupcat(g->hostpath, scanpathcharacter);
            sendscancommand(hq_plug, g->host->reachplan.routeraddr, physicalpath, g->prunepoints);
            free(physicalpath);
        }
    }

} // hq_scan

void sendvirtualnodelisting(char* path) // sends back all children of node at path
{
    virtualnode *dir;

    if (!validfullpath(path)) {
        printerr("Error: Headquarters got invalid path to list: %s\n", path);
        return;
    }

    dir = findnode(&virtualroot, path);

    if (!dir) {
        printerr("Error: Headquarters could not find node with path: %s\n", path);
        return;
    }

    if (dir->filetype > 1) {
        printerr("Error: Headquarters got list request for node which is not a directory: %s\n", path);
        return;
    }

    sendvirtualdir(hq_plug, cmd_int, path, dir);
} // sendvirtualnodelisting

void hqmain(void)
{
    int32 msg_src;
    int64 msg_type;
    char* msg_data;
    device* d;

    virtualtreeinit();

    while (1) {
        while (! receivemessage(hq_plug, &msg_src, &msg_type, &msg_data)) {
            usleep(1000);
        }
        // we got a message
        switch (msg_type) {
            case msgtype_info:
                    printerr("Headquarters got info message: \"%s\" from %d\n",
                                    msg_data, msg_src);
                    break;
            case msgtype_workerisup:
                    // we need to find out (topworker) or verify who we are
                    sendmessage(hq_plug, msg_src, msgtype_identifydevice, "");
                    // we won't say we're connected till we know to whom!
                    break;
            case msgtype_deviceid:
                    // need to find the device with the id in msg_data
                    // (we already know it if we asked for connection,
                    // but not for top worker -- topworker tells us who we are)
                    printerr("Heard that plug %d is for device %s.\n",
                            msg_src, msg_data);
                    for (d = devicelist; d != NULL; d = d->next) {
                        if (! strcmp(d->deviceid, msg_data)) {
                            // d is the device with the deviceid
                            if (d->reachplan.routeraddr == msg_src) {
                                // already set to what we would expect
                            } else {
                                if (d->reachplan.routeraddr == -1
                                        && msg_src == topworker_int) {
                                    // it is from the topworker
                                    d->reachplan.routeraddr = topworker_int;
                                } else {
                                    printerr("Error: Plug confusion!");
                                    printerr(" (%d reported name \"%s\", already"
                                            " owned by %d)\n", msg_src, msg_data,
                                            d->reachplan.routeraddr);
                                }
                            }
                            break;
                        }
                    }
                    if (d == NULL) {
                        printerr("Error: Reported device id not known: %s\n",
                                    msg_data);
                    }
                    setstatus(msg_src, status_connected);
                    if (msg_src == topworker_int)
                        hq_init();
                    break;
            case msgtype_newplugplease1:
                    hq_reachfor(msg_data, NULL); // msg_data is d->deviceid
                    break;
            case msgtype_newplugplease2:
                    // msg_data is destid, sourceid
                    hq_reachfor(msg_data, secondstring(msg_data));
                    break;
            case msgtype_disconnect:
                    setstatus(atoi(msg_data), status_inactive);
                    break;
            case msgtype_listvirtualdir:
                    // msg_data is the virtual path of the virtual directory to list
                    sendvirtualnodelisting(msg_data);
                    break;
            case msgtype_scanvirtualdir:
                    // msg_data is the virtual path of the node to scan
                    hq_scan(msg_data);
                    break;
            default:
                    printerr("Headquarters got unexpected message"
                                    " of type %lld from %d: \"%s\"\n",
                                    msg_type, msg_src, msg_data);
        } // switch on message type
        free(msg_data);
    } // loop on messages
} // hqmain

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of algo main ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

