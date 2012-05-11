#include "definitions.h"

virtualnode virtualroot; // has no siblings and no name
                         // only to be used by the HQ thread


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
    pos = strrchr(dir, '/');
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

void setstatus(device **target, status_t newstatus)
{
    device *d = *target;
    status_t oldstatus;
    char buf[90];

    // set the new status
    oldstatus = d->status;
    d->status = newstatus;
    // take any status-change-based actions
    switch (newstatus) {
        case status_inactive:
                sendmessage(hq_plug, cmd_int, msgtype_disconnected, d->deviceid);
            break;
        case status_reaching:
            break;
        case status_connected:
                snprintf(buf, 90, "%s", d->deviceid); // unnecessary to use buf
                sendmessage(hq_plug, cmd_int, msgtype_connected, buf);
            break;
    }
    printerr("Changed status of %s from %d (%s) to %d (%s).\n",
                d->nickname,
                oldstatus, statusword[oldstatus],
                newstatus, statusword[newstatus]);
} // setstatus

void hq_reachfor(device *d)
{
    if (!d->reachplan.whichtouse) {
        printerr("Error: \"which to use\" address not set on device! %s", d->deviceid);
        return;
    }
    sendrecruitcommand(hq_plug, d->reachplan.routeraddr, d->reachplan.whichtouse);
    setstatus(&d, status_reaching);
} // hq_reachfor

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
            char *physicalpath = strdupcat(g->hostpath, scanpathcharacter, NULL);

            // get rid of the virtual graft path in the prunes list..
            stringlist *graftprunes, *deviceprunes, *tmp;
            int32 virtualpathlen = strlen(g->virtualpath);

            deviceprunes = tmp = NULL;
            graftprunes = g->prunepoints;
            while (graftprunes != NULL) {
                tmp = deviceprunes;
                deviceprunes = (stringlist*) malloc(sizeof(struct stringlist_struct));
                deviceprunes->next = tmp;
                if (! strncmp(g->virtualpath, graftprunes->string, virtualpathlen))
                    deviceprunes->string = strdupcat(g->hostpath, graftprunes->string + virtualpathlen, NULL);
                else
                    deviceprunes->string = strdup(graftprunes->string);
                graftprunes = graftprunes->next;
            }

            // send scan command to workers using only host paths
            sendscancommand(hq_plug, g->host->reachplan.routeraddr, physicalpath, deviceprunes);
            freestringlist(deviceprunes);
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

device* addunknowndevice(char *deviceid, char *address, int32 routeraddr)
{
    device **d;
    int i = 0;
    for (d = &devicelist; *d != NULL; d = &((*d)->next))
        i++;
    *d = (device*) malloc(sizeof(device));
    (*d)->next = NULL;
    (*d)->nickname = strdup("Unknown Device");
    (*d)->deviceid = strdup(deviceid);
    (*d)->status = status_connected;
    (*d)->linked = 1;
    (*d)->reachplan.ipaddrs = (stringlist*) malloc(sizeof(stringlist));
    (*d)->reachplan.ipaddrs->next = NULL;
    (*d)->reachplan.ipaddrs->string = strdup(address);
    (*d)->reachplan.routeraddr = routeraddr;
    return *d;
} // addunknowndevice

device* getdevicebyid(char *deviceid) {
    device *d;

    for (d = devicelist; d != NULL; d = d->next)
        if (!strcmp(d->deviceid, deviceid))
            break;
    return d;
} // getdevice

device* getdevicebyplugnum(int32 plugnumber) {
    device *d;

    for (d = devicelist; d != NULL; d = d->next)
        if (d->reachplan.routeraddr == plugnumber)
            break;
    return d;
} // getdevice

void identifydevice(char *localid, char *remoteid)
{

    device *targetdevice;
    device *reacheddevice; // those two may refer to the same device

    targetdevice = getdevicebyid(localid);
    reacheddevice = getdevicebyid(remoteid);

    if (!targetdevice) { // we should always be able to find it, because it was
                         // used to connect to reached device
        printerr("Error: Can't find device with id [%s]\n", localid);
        return;
    }

    if (reacheddevice != NULL && reacheddevice->linked) {
        // we reached some known device ...
        if (reacheddevice->status == status_connected) {
            // ... which is already connected
            printerr("Error: Device with id [%s] was connected to twice!\n", remoteid);
            // connected through another device?
            if (targetdevice != reacheddevice) {
                sendplugnumber(hq_plug, recruiter_int, msgtype_removeplugplease,
                               targetdevice->reachplan.routeraddr);
                setstatus(&targetdevice, status_reaching);
            } // else here would mean we reached a connected device using the same device.
              // this should have been caught by headquarters or command. we can't do
              // anything about it here, because it means that the old plug was already
              // memory leaked during the connection set up
        } else {
            // ... which we will set to be connected now -> transfer the plug if needed
            if (targetdevice != reacheddevice) {
                printerr("Warning: Transferring plug from [%s] to [%s]\n", localid, remoteid);
                reacheddevice->reachplan.routeraddr = targetdevice->reachplan.routeraddr;
                targetdevice->reachplan.routeraddr = -1;
                setstatus(&targetdevice, status_inactive);
            }
            setstatus(&reacheddevice, status_connected);
        }
    } else {
        // we reached unknown device -> add it and set its status to connected
        if (targetdevice->linked) {
            reacheddevice = addunknowndevice(remoteid, targetdevice->reachplan.whichtouse,
                                                       targetdevice->reachplan.routeraddr);
            // leave it up to the user if he/she wants to save the unknown device to specs
            targetdevice->reachplan.routeraddr = -1;
            setstatus(&targetdevice, status_inactive);
            setstatus(&reacheddevice, status_connected);

        } else {
            // we had a temporary, unverified id -> believe workers id and quickly add to specs!
            if (strcmp(localid, remoteid) != 0) {
                free(targetdevice->deviceid);
                targetdevice->deviceid = strdup(remoteid);
            }
            targetdevice->linked = 1;
            if (specstatevalid())
                writespecsfile(specs_file_path);
            setstatus(&targetdevice, status_connected);
        }
    }
} // identifydevice

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
                    if (! (d = getdevicebyplugnum(msg_src))) {
                        printerr("Error: Received plug number which does not belong "
                                 "to any device (%d)\n", msg_src);
                        break;
                    }
                    // send a device id suggestion to worker
                    sendmessage(hq_plug, msg_src, msgtype_identifydevice, d->deviceid);
                    break;
            case msgtype_deviceid:
            {
                    char *localid = msg_data;
                    char *remoteid = secondstring(msg_data);

                    printerr("Heard that device we call [%s] it calls itself [%s]\n",
                             localid, remoteid);

                    identifydevice(localid, remoteid);
                    break;
            }
            case msgtype_connectdevice: // msg_data is device id
                   if (! (d = getdevicebyid(msg_data))) {
                        printerr("Error: Received unknown device id [%s]\n", msg_data);
                        break;
                    }
                    if (d->status == status_connected) {
                        printerr("Error: Got connect request for already connected "
                                 "device [%s]\n", msg_data);
                        break;
                    }
                    if (d->reachplan.routeraddr == -1) {
                        sendmessage(hq_plug, recruiter_int, msgtype_newplugplease, msg_data); // as reference for us
                        break;
                    }
                    hq_reachfor(d);
                    break;
            case msgtype_disconnectdevice: // msg_data is device id
                if (! (d = getdevicebyid(msg_data))) {
                     printerr("Error: Received unknown device id [%s]\n", msg_data);
                     break;
                 }

                if (d->status != status_connected) {
                    printerr("Error: Got connect request for already connected "
                             "device [%s]\n", msg_data);
                    break;
                }

                sendplugnumber(hq_plug, recruiter_int, msgtype_removeplugplease,
                                                        d->reachplan.routeraddr);

                setstatus(&d, status_reaching);
                break;
            case msgtype_newplugplease: // answer from recruit to our newplugplease message,
            {
                char *deviceid; // we sent the device id as our reference to recruiter
                int32 plugnumber;
                receivenewplugresponse(msg_data, &deviceid, &plugnumber);
                if (! (d = getdevicebyid(deviceid))) {
                    printerr("Error: Received unknown device id [%s]\n", msg_data);
                    free(deviceid);
                    break;
                }
                free(deviceid);
                d->reachplan.routeraddr = plugnumber;
                hq_reachfor(d);
                break;
            }
            case msgtype_removeplugplease: // answer from recruiter to our removeplugplease message,
                                           // msgdata is plugnumber of the removed device
            {
                int32 plugnumber;
                receiveplugnumber(msg_data, &plugnumber);

                if (! (d = getdevicebyplugnum(plugnumber))) {
                    printerr("Error: Received plug number which does not belong "
                             "to any device (%d)\n", plugnumber);
                    break;
                }
                d->reachplan.routeraddr = -1;
                setstatus(&d, status_inactive);
                break;
            }
            case msgtype_failedrecruit: // possible answer to our recruitworker message sent to the recruiter
                                        // if the recruit was successful, we will hear form worker directly (workerisup message)
            {                           // msg_data is plugnumber
                    int32 plugnumber;
                    receiveplugnumber(msg_data, &plugnumber);

                    if (! (d = getdevicebyplugnum(plugnumber))) {
                        printerr("Error: Received plug number which does not belong "
                                 "to any device (%d)\n", plugnumber);
                        break;
                    }
                    sendplugnumber(hq_plug, recruiter_int, msgtype_removeplugplease, plugnumber);
                    d->reachplan.routeraddr = -1;
                    setstatus(&d, status_inactive); // msg_data is deviceid
                    break;
            }
            case msgtype_listvirtualdir:
                    // msg_data is the virtual path of the virtual directory to list
                    sendvirtualnodelisting(msg_data);
                    break;
            case msgtype_scanvirtualdir:
                    // msg_data is the virtual path of the node to scan
                    hq_scan(msg_data);
                    break;
            case msgtype_scanupdate: // msg_data is the path of the remote scan file
            {
              char buf[512], *devicepath, *scanpath;
              int  ret;
              connection plug = findconnectionbyplugnumber(msg_src);

              if (! (d = getdevicebyplugnum(msg_src))) {
                  printerr("Error: Received plug number which does not belong "
                           "to any device (%d)\n", msg_src);
                  break;
              }

              devicepath = strdupcat(".", scan_files_path, "/", d->deviceid, "/", NULL);
              scanpath = strdupcat(devicepath, (strrchr(msg_data, '/') + 1), NULL);
              mkdir(devicepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
              free(devicepath);

              if (plug->session.path == NULL) { // the connection is local
                    sprintf(buf, "/bin/cp %s %s 2> /dev/null", msg_data, scanpath);
              } else {
                    sprintf(buf, "/usr/bin/scp -C -o 'ControlPath %s' %s@%s:%s %s",
                            plug->session.path,
                            plug->session.uname,
                            plug->session.mname,
                            msg_data,
                            scanpath);
              }

              ret = system(buf);
              free(scanpath);

              if (ret) {
                printerr("Error: System call on Headquarters was not successful\n");
                break;
              }

              break;
            }
            case msgtype_exit:
                    cleanexit(__LINE__);
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

