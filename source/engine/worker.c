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

char *replacetilde(char *address) // allocates string, free when done
{
    char *tildereplaced;

    if(*address == '~') {
        address++;
        tildereplaced = strdupcat(homedirectory(), address, NULL);
    } else
        tildereplaced = strdup(address);

    return tildereplaced;
} // replacetilde


void resetscanprogress(scan_progress *progress) // allocates scan_progress, free when done
{
    (*progress)->directories =
    (*progress)->regularfiles =
    (*progress)->links =
    (*progress)->other =
    (*progress)->total = 0;
} // initscanprogress

void workerscan(char *deviceroot, char *scanroot, char *deviceid, stringlist *prunepoints, connection worker_plug)
{
    char *devicescanfolder = strdupcat(deviceroot, scan_files_path, "/", deviceid, NULL);
    char *scanfilepath = strdupcat(devicescanfolder, "/scan", NULL);
    scan_progress progress = (scan_progress) malloc(sizeof(struct scan_progress_struct));


    mkdir(devicescanfolder, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    FILE *scanfile = fopen(scanfilepath, "w");

    if (!scanfile) {
        printerr("Error: Worker can't open file for scan file creation (%s) "
                 "Path: %s \n",
                 strerror(errno), scanfilepath);
        free(scanfilepath);
        return;
    }

    scanroot = replacetilde(scanroot);

    progress->updateinterval = 1000; // report to hq on every 1000 processed files

    resetscanprogress(&progress);
    fileinfo *info = formimage(scanroot, prunepoints, worker_plug, NULL, progress);

    resetscanprogress(&progress);
    writesubimage(scanfile, info, progress);
    fclose(scanfile);

    freefileinfo(info);

    sendmessage(worker_plug, hq_int, msgtype_scanupdate, scanfilepath);
    free(scanfilepath);
    free(devicescanfolder);
    free(scanroot);
    free(progress);

    // output: scan number, changes since previous on master
} // workerscan

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


/*
Device file format:
int32   McSync magic cookie
int32   version
int32   inode of device file (st_ino)
int32   id of OS device on which the device file is stored (st_dev)
string  unique device identifier (see device_id_size for byte size)
*/
int32 createdevicefile(char *path, char *deviceid)
{// creates a new device file and stores a given id in it. Returns 1 if sucessful, 0 otherwise
    FILE *devicefile;
    struct stat devicefileinfo;

    // collection of meta data on the device file created
    devicefile = fopen(path, "w");
    if (!devicefile) {
        printerr("Error: Worker can't open file for device file creation (%s) "
                 "Path: %s \n",
                 strerror(errno), path);
        return 0;
    }
    if (lstat(path, &devicefileinfo) == -1) {
        printerr("Error: Worker can't access stat info of device file (%s) "
                 "Path: %s\n",
                 strerror(errno), path);
        fclose(devicefile);
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
    fclose(devicefile);
    return 1;
} // createdevicefile

char* deviceidondisk(char *hqs_deviceid, char *devicefilepath) // returns NULL if there was a problem
{ // if hqs_deviceid is not NULL, will try to create a file with this id in case it does not exist or is foreign
    FILE *devicefile = NULL;
    int32 storedinode, storedosdeviceid;
    struct stat devicefilestat;
    char *deviceid = NULL;

    // try to get the device file lock, block for a minute before giving up
    if (!getlockfile(devicefilepath, 60000)) {
        printerr("Error: Worker can't get lock file for device file.\n");
        return NULL;
    }

    devicefile = fopen(devicefilepath, "r");

    if (!devicefile) {
        if (errno != ENOENT) {
            // there is some other problem other than the lack of a device file
            printerr("Error: Worker can't open device file for reading (%s)"
                     "Path: %s\n",
                     strerror(errno), devicefilepath);
            goto release_and_return;
        }
        // there is no device file, create a new device file for this device
        // with the id suggested by hq (if given, else return NULL)
        if (hqs_deviceid) {
            if (!createdevicefile(devicefilepath, hqs_deviceid)) {
                printerr("Error: Device file creation has failed.\n");
                goto release_and_return;
            }
            deviceid = strdup(hqs_deviceid);
        }
        goto release_and_return;
    }

    // make sure we have opened a proper device file
    if (get32(devicefile) != magiccookie) {
        printerr("Error: Worker read invalid magic cookie in device file: %s\n",
                devicefilepath);
        goto release_and_return;
    }

    if (get32(devicefile) != devicefileversionnumber) {
        printerr("Error: Worker can't read device file because of a wrong device "
                 "file version.\n");
        goto release_and_return;
    }

    // check if the device file is a left over from another McSync device archive
    if (lstat(devicefilepath, &devicefilestat) == -1) {
        printerr("Error: Worker can't access stat info of device file: %s\n",
                 devicefilepath);
        goto release_and_return;
    }

    // we use the inode of the device file and its devices id to guess whether we are on the same device
    storedinode = get32(devicefile);
    storedosdeviceid = get32(devicefile);

    if (storedinode != devicefilestat.st_ino || storedosdeviceid != devicefilestat.st_dev) {

        if (!hqs_deviceid)
            goto release_and_return;

        printerr("Warning: Worker detected foreign device file, "
                 "starts creation of new one!\n");
        // create a backup of the old file device
        char *backup = strdupcat(devicefilepath, ".backup", NULL);
        // does not matter if removing an old backup file fails, it does not have
        // to exist other problems are probably caught by rename further down any way
        (void)remove(backup);
        fclose(devicefile);
        if (rename(devicefilepath, backup) == -1) {
            printerr("Error: Backup of foreign device file failed (%s)\n",
                     strerror(errno));
            free(backup);
            goto release_and_return;
        }
        free(backup);

        // create a new device file with the id from hq
        if (!createdevicefile(devicefilepath, hqs_deviceid)) {
            printerr("Error: Device file creation has failed.\n");
            goto release_and_return;
        }
        deviceid = strdup(hqs_deviceid);
        goto release_and_return;
    }

    deviceid = getstring(devicefile, 0);

release_and_return:

    if(!releaselockfile(devicefilepath))
        printerr("Error: Worker could not realease lock file! Path: %s\n",
                 devicefilepath);

    if (devicefile)
        fclose(devicefile);

    return deviceid;
} // deviceidondisk

char *extractdeviceroot(char *address) // allocates string, free when done
{
    // somehow this worker was successfully reached using the given address,
    // thus it should be of correct format
    char *location = index(address, ':');
    location++;
    return replacetilde(location);
} // extractdeviceroot

void workermain(connection worker_plug)
{
    int32 dowork = 1;
    char buf[90];
    int32 msg_src;
    int64 msg_type;
    char *msg_data = NULL;

    char *deviceroot = NULL;
    char *devicefilepath = NULL;
    char *deviceid = NULL;

    addabortsignallistener(); // as last resort for local worker threads,
                              // will possibly leak memory

    // tell hq we are up!
    snprintf(buf, 90, "%d", worker_plug->plugnumber);
    sendmessage(worker_plug, hq_int, msgtype_workerisup, buf);

    // we need to know where to look for our id
    deviceroot = extractdeviceroot(worker_plug->address);
    devicefilepath =  strdupcat(deviceroot, device_file_path, NULL);

    while (dowork) {
        while (! receivemessage(worker_plug, &msg_src, &msg_type, &msg_data)) {
            usleep(pollingrate);
        }
        switch (msg_type) {
            case msgtype_info:
                    printerr("Worker got info message: \"%s\" from %d\n",
                                    msg_data, msg_src);
            break;
            case msgtype_identifydevice: // msg_data is hq's device id suggestion
            {
                if (deviceid != NULL)
                    free(deviceid);

                deviceid = deviceidondisk(msg_data, devicefilepath);

                if (deviceid == NULL) { // this is serious, can't do anything without proper id
                    printerr("Error: Worker does not know device id, quits...\n");
                    dowork = 0;
                    break;
                }

                snprintf(buf, 90, "%s%c%s", msg_data, 0, deviceid);
                sendmessage2(worker_plug, hq_int, msgtype_deviceid, buf);
            }
            break;
            case msgtype_scan:
                    {
                        char *scanroot, *currentid;
                        stringlist *prunepoints;

                        // make sure the device has not changed
                        currentid = deviceidondisk(NULL, devicefilepath);

                        if (currentid == NULL) { // this is serious, can't do anything without proper id
                            printerr("Error: Worker does not know device id, quits...\n");
                            dowork = 0;
                            break;
                        }

                        if (strcmp(deviceid, currentid) != 0) {
                            printerr("Error: Worker has detected change of "
                                     "device at its address, quits...\n");
                            dowork = 0;
                            break;
                        }
                        receivescancommand(msg_data, &scanroot, &prunepoints);
                        workerscan(deviceroot, scanroot, deviceid, prunepoints, worker_plug);
                        free(scanroot);
                        free(currentid);
                        freestringlist(prunepoints);
                    }
            break;
            case msgtype_exit:
                printerr("Worker got exit message... good bye!\n");
                dowork = 0;
            break;
            default:
                    printerr("Worker got unexpected message"
                                    " of type %lld from %d: \"%s\"\n",
                                    msg_type, msg_src, msg_data);
        }
        free(msg_data);
        msg_data = NULL;
    }

    // we were asked to stop working...
    sendmessage(worker_plug, recruiter_int, msgtype_goodbye, "");
    sleep(1);
    if (deviceid != NULL)
        free(deviceid);

    free(devicefilepath);
    free(deviceroot);

    if (slavemode)
        cleanexit(__LINE__);
    else
        pthread_exit(NULL);
} // workermain

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of device worker ////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

