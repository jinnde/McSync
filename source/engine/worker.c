#include "definitions.h"

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of minor slave activities /////////////////////
//////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////   D  E  L  E  T  E

int removefile(char *skunk) // full path name of file
{
    struct stat status;
    struct utimbuf times;
    char *i = rindex(skunk, '/');

    // first store modtime of parent
    if (i == NULL) {
        printf("Error: filename has no path: %s\n", skunk);
        return -1;
    }
    *i = 0; // hack skunk down to its parent directory (not thread-safe!)
    if (lstat(skunk, &status)) {
        printf("Error: Could not stat %s (%s)\n", skunk, strerror(errno));
        *i = '/';
        return -1;
    }
    times.actime = status.st_atime;
    times.modtime = status.st_mtime;
    *i = '/'; // undo the parent directory hack

    // now figure out whether skunk is a directory
    if (lstat(skunk, &status)) {
        printf("Error: Could not stat %s (%s)\n", skunk, strerror(errno));
        return -1;
    }
    if ((status.st_mode & S_IFMT) == S_IFDIR) {
        // remove contents of directory first
        DIR* thisdir;
        struct dirent *entry;

        thisdir = opendir(skunk);
        if (!thisdir) {
            printf("Error: Directory %s could not be read (%s)\n",
                    skunk, strerror(errno));
            return -1;
        }

        // loop over entries in the directory
        while (1) {
            char *childname;
            errno = 0;
            entry = readdir(thisdir); // this assumes errno stays 0 on success
            // (I see no other way to detect an error than this way.
            //  Another reason interfaces should be formalized further.)
            if (entry == NULL) {
                if (errno != 0)
                    printf("Warning: error reading entry in directory %s (%s)\n",
                            skunk, strerror(errno));
                // if no error, entry==NULL means we have read it all
                break;
            }
            // very important: do not follow "parent directory" links!
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
                continue;
            // delete the child
            childname = strdupcat(skunk, "/", entry->d_name, NULL);
            removefile(childname);
            free(childname);
        }

        if (closedir(thisdir) != 0) {
            printf("Warning: Could not close directory %s (%s)\n",
                    skunk, strerror(errno));
        }

        // now that it is empty, we can finally remove the directory
        if (rmdir(skunk) != 0) {
            printf("Error: Could not remove directory %s (%s)\n",
                    skunk, strerror(errno));
            return -1;
        }
    } else {
        // it is a file, symlink, or something else but not a directory
        if (unlink(skunk)) {
            printf("Error: Could not remove %s (%s)\n",
                    skunk, strerror(errno));
            return -1;
        }
    }

    // restore mod time of parent directory to what it was
    *i = 0;
    if (utime(skunk, &times) != 0) {
        printf("Warning: Could not maintain directory mod time for %s\n", skunk);
    }
    *i = '/';

    printf("Removed %s\n", skunk);
    return 0;
} // removefile

void deletefiles(char *filelist)
{
    FILE *list;
    list = fopen(filelist, "r");
    if (!list) {
        printf("Error: Couldn't open file-removal list %s (%s)\n",
                filelist, strerror(errno));
        return;
    }
    while (1) { // (!feof(list)) is not working!
        char *skunkname;
        // first test for end of file
        int c = getc(list);
        if (c == EOF) // even here, feof doesn't return true
            break; // no way to ask politely if there's more data???  grrr.
        ungetc(c, list);
        // now go ahead as planned
        skunkname = getstring(list, '\n');
        removefile(skunkname);
    }
    fclose(list);
} // deletefiles

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of minor slave activities ///////////////////////
//////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of device worker //////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

void slavesetup(void)
{
    // input: scan numbers and history numbers that master has, who master is
    // output: any change in history, and current history number
} // slavesetup

void slavedelete(void)
{
    // input: file or directory to delete
    // output: success
} // slavedelete

void slavescan(char *scanroot, stringlist *prunepoints)
{
    FILE *f;
    fileinfo *info = formimage(scanroot);

    f = fopen("./data/temp.txt", "w");
    writesubimage(f, info);

    // output: scan number, changes since previous on master
} // slavescan

void slaveread(void)
{
    // input: file, aspect
    // output: length of contents, file contents (for that aspect)
} // slaveread

void slavewrite(void)
{
    // input: virtual tree filename, aspect, length/contents
    // output: success
} // slavewrite

void slavesetinfo(void)
{
} // slavesetinfo

void slaveping(void)
{
} // slaveping

char* deviceidondisk(void)
{
    // we don't cache it, so if someone swaps a drive and asks us to notice, we can
    return "local1";
} // deviceidondisk

void channel_launch(connection* known_plug, char* deviceid, int plugnumber);

void workermain(void)
{
    char buf[90];
    int32 msg_src;
    int64 msg_type;
    char* msg_data;
    device* m;

    // report existence
    snprintf(buf, 90, "%d", worker_plug->thisway->values[0]);
    sendmessage(worker_plug, hq_int, msgtype_workerisup, buf);

    while (1) {
        while (! receivemessage(worker_plug, &msg_src, &msg_type, &msg_data)) {
            usleep(1000);
        }
        // we got a message
        switch (msg_type) {
            case msgtype_info:
                    printerr("worker got info message: \"%s\" from %d\n",
                                    msg_data, msg_src);
                    break;
            case msgtype_newplugplease:
                    // get device from msg_data (m->deviceid)
                    for (m = devicelist; m != NULL; m = m->next) {
                        if (! strcmp(msg_data, m->deviceid))
                            break;
                    }
                    if (m == NULL) {
                        printerr("Error: Received unknown device id \"%s\"\n",
                                msg_data);
                        goto nextmessage;
                    }
                    // get plugnum from msg_data (not m->...!)
                    int plugnum = atoi(secondstring(msg_data));
                    printerr("hey, data = (%s,%s), plugnum = %d\n",
                        msg_data, secondstring(msg_data), plugnum);
                    // BUGGY  if no ssh is needed, a local plug should be made
                    channel_launch(NULL, m->deviceid, plugnum);
                    // m->reachplan.routeraddr might not be set on this device
                    // if it works, it will report its existence on its own...
                    break;
            case msgtype_identifydevice:
                    sendmessage(worker_plug, hq_int, msgtype_deviceid,
                                deviceidondisk());
                    break;
            case msgtype_scan:
                    {
                        char *scanroot;
                        stringlist *prunepoints;

                        receivescancommand(msg_data, &scanroot, &prunepoints);
                        slavescan(scanroot, prunepoints);
                        free(scanroot);
                        freestringlist(prunepoints);
                    }
                    break;
            default:
                    printerr("worker got unexpected message"
                                    " of type %lld from %d: \"%s\"\n",
                                    msg_type, msg_src, msg_data);
        }
        nextmessage:
        free(msg_data);
    }
} // workermain

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of device worker ////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

