#include "definitions.h"

int32 getlockfile(char *path, int32 maxtime)
{// blocks for maxtime milliseconds until the lock for file at path can be acquired
 // returns 1 if sucessful, 0 otherwise. We use lock files because we might need to
 // access data on old file systems such as FAT32 and thus can't use flock et al.
    FILE *lockfile;
    char *lockfilepath = strdupcat(path, ".locked", NULL);

    lockfile = fopen(lockfilepath, "r");
    // we need microseconds for usleep
    maxtime *= 1000;
    while (lockfile && (maxtime > 0)) {
        fclose(lockfile);
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
