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
    FILE *f = fopen("./data/temp.txt", "w");
    fileinfo *info = formimage(scanroot);
    writesubimage(f, info);
    fclose(f);
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

int32 getlockfile(char *path, int32 maxtime)
{// blocks for maxtime milliseconds until the lock for file at path can be acquired
 // returns 1 if sucessful, 0 otherwise
    FILE *lockfile;
    char *lockfilepath = strdupcat(path, ".locked", NULL);

    lockfile = fopen(lockfilepath, "r");
    // we need microseconds for usleep
    maxtime *= 1000;
    while (lockfile && (maxtime > 0)) {
        lockfile = fopen(lockfilepath, "r");
        usleep(1000);
        maxtime -= 1000;
    }
    // waiting time was up before we got the lock
    if (lockfile)
        return 0;
    if (!(lockfile = fopen(lockfilepath, "w"))) {
        printerr("Error: Could not write lock file to disk (%s) Path: %s\n",
                 strerror(errno), lockfilepath);
        return 0;
    }
    fclose(lockfile);
    free(lockfilepath);
    return 1;
} // getlockfile

int32 releaselockfile(char *path) //returns 1 if sucessful, 0 otherwise
{
    char *lockfilepath = strdupcat(path, ".locked", NULL);

    if (remove(lockfilepath) == -1) {
        if (errno != ENOENT) {
            printerr("Error: Could not release lock file (%s) Path: %s\n",
                     strerror(errno), lockfilepath);
            free(lockfilepath);
            return 0;
        }
        printerr("Warning: re-releasing lock file! Path: %s\n", lockfilepath);
    }
    free(lockfilepath);
    return 1;
} // releaselockfile

/*
Device file format:
int32   McSync magic cookie
int32   version
int32   inode of device file (st_ino)
int32   id of OS device on which the device file is stored (st_dev)
string  128 bit unique device identifier
*/
int32 createdevicefile(char *path)
{// creates a new device file, with a unqiue device id. Returns 1 if sucessful, 0 otherwise
    FILE *devicefile, *devrandom;
    char randombuf[device_id_size];
    char *deviceid;
    struct stat devicefileinfo;
    int32 i, j;

    // unique device id generation
    devrandom = fopen("/dev/random", "rb");
    if (!devrandom) {
        printerr("Error: Worker can't open /dev/random for unique device id creation (%s)\n",
                 strerror(errno));
        return 0;
    }

    if (fread(randombuf, 1, device_id_size, devrandom) != device_id_size) {
        printerr("Error: Worker can't read from /dev/random for unique device id creation (%s)\n",
                 strerror(errno));
        fclose(devrandom);
        return 0;
    }
    fclose(devrandom);

    // store the device id in hex for human readability
    deviceid = (char*) malloc(device_id_size * 2);
    for (i = 0, j = 0; i < device_id_size; i++, j = i * 2) {
        tohex(randombuf[i], &deviceid[j], &deviceid[j + 1]);
    }

    // collection of meta data on the device file created
    devicefile = fopen(path, "w");
    if (!devicefile) {
        printerr("Error: Worker can't open file for device file creation (%s) "
                 "Path: %s \n",
                 strerror(errno), path);
        free(deviceid);
        return 0;
    }
    if (lstat(path, &devicefileinfo) == -1) {
        printerr("Error: Worker can't access stat info of device file (%s) "
                 "Path: %s\n",
                 strerror(errno), path);
        fclose(devicefile);
        free(deviceid);
        if (remove(path) == -1)
            printerr("Error: Worker can't remove incomplete device file (%s) "
                     "remove it manually! Path: %s\n",
                     strerror(errno), path);
        return 0;
    }
    put32(devicefile, magiccookie);
    put32(devicefile, devicefileversionnumber);
    put32(devicefile, devicefileinfo.st_ino);
    put32(devicefile, devicefileinfo.st_dev);
    putstring(devicefile, deviceid);
    free(deviceid);
    fclose(devicefile);
    return 1;
} // createdevicefile

char* deviceidondisk(void)
{// If unsuccessful returns "unknown". Allocates string, free when done
    FILE *devicefile = NULL;
    int32 storedinode, storedosdeviceid;
    struct stat devicefilestat;
    char *deviceid = NULL;

    // try to get the device file lock, block for a minute before giving up
    if (!getlockfile(device_file_path, 60000)) {
        printerr("Error: Worker can't get lock file for device file. "
                 "Sending : \"unknown\" as id\n");
        goto release_and_return;
    }

    devicefile = fopen(device_file_path, "r");

    if (!devicefile) {
        if (errno != ENOENT) {
            // there is some other problem other than the lack of a device file
            printerr("Error: Worker can't open device file for reading (%s)"
                     "Path: %s\n Sending : \"unknown\" as id\n",
                     strerror(errno), device_file_path);
            goto release_and_return;
        }
        // create a new device file for this device
        if (!createdevicefile(device_file_path)) {
            printerr("Error: Device file creation has failed. "
                     "Sending : \"unknown\" as id\n");
            goto release_and_return;
        }
        // try to open the new device file
        devicefile = fopen(device_file_path, "r");
        // rare chance of happening, but somehow the permissions might have
        // changed or what not, so let's just be safe and check for a NULL
        // file descriptor again
        if (!devicefile) {
            printerr("Error: Worker can't open created device file for reading (%s) "
                     "Path: %s\n Sending : \"unknown\" as id\n",
                     strerror(errno), device_file_path);
            goto release_and_return;
        }
    }

    // make sure we have opened a proper device file
    if (get32(devicefile) != magiccookie) {
        printerr("Error: Worker read invalid magic cookie in device file: %s "
                 "Sending : \"unknown\" as id\n", device_file_path);
        goto release_and_return;
    }

    if (get32(devicefile) != devicefileversionnumber) {
        printerr("Error: Worker can't read device file because of a wrong device "
                 "file version. Sending : \"unknown\" as id\n");
        goto release_and_return;
    }

    // check if the device file is a left over from another McSync device archive
    if (lstat(device_file_path, &devicefilestat) == -1) {
        printerr("Error: Worker can't access stat info of device file: %s "
                 " Sending : \"unknown\" as id\n", device_file_path);
        goto release_and_return;
    }

    // we use the inode of the device file and its devices id to guess whether we are on the same device
    storedinode = get32(devicefile);
    storedosdeviceid = get32(devicefile);

    if (storedinode != devicefilestat.st_ino || storedosdeviceid != devicefilestat.st_dev) {
        printerr("Warning: Worker detected foreign device file, "
                 "starts creation of new one!\n");
        // create a backup of the old file device
        char *backup = strdupcat(device_file_path, ".backup", NULL);
        // does not matter if removing an old backup file fails, it does not have
        // to exist other problems are probably caught by rename further down any way
        (void)remove(backup);
        fclose(devicefile);
        if (rename(device_file_path, backup) == -1) {
            printerr("Error: Backup of foreign device file failed (%s) "
                     "Sending : \"unknown\" as id\n", strerror(errno));
            free(backup);
            goto release_and_return;
        }
        free(backup);
        // Trying to release the lock twice is a little problem compared to an orphaned lock file
        if(!releaselockfile(device_file_path))
            goto release_and_return;

        return deviceidondisk(); // will create the new device file for us
    }

    deviceid = getstring(devicefile, 0);

release_and_return:

    if(!releaselockfile(device_file_path))
        printerr("Error: Worker could not realease lock file! Path: %s\n",
                 device_file_path);

    if (devicefile)
        fclose(devicefile);

    // if problems occured, the function should return "unknown"
    if (!deviceid)
        deviceid = strdup("unknown");

    return deviceid;
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
            {
                char *deviceid = deviceidondisk();
                sendmessage(worker_plug, hq_int, msgtype_deviceid, deviceid);
                free(deviceid);
            }
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

