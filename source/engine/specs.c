#include "definitions.h"
#include "util/cJSON/cJSON.h"

/*

This reads and writes the preferences for the system.

We store prefs in json format so we can use an off-the-shelf parser.
Our approach to forward-compatability is that named object properties
should have hard-coded names and can be freely added in future versions.
Exception: Any name beginning with "comment" is reserved to never be used.

The preferences are stored in config/specs.

It reads/writes the following globals:
    machinelist
    directorysetlist
    devicelist
    graftlist

Quick summary of format:
 { "version" : 1,
   "machines" : [
                    { "name" : "MyLaptop",
                      "script" : "isitmylaptop.sh",         EMPTY STRING == NONE
                      "comment1" : "script (from reaching machine) makes sure we"
                      "comment2" : "don't talk to executables on strange machines",
                      "dir" : [ "~/.mcsync",
                                "/Volumes/DriveNames",
                                ... ],
                      "addresses" : [
                                        { "fromnet" : "home net",
                                          "how" : "ssh:user@192.168.1.1",
                                          "reaches" : "internet" },
                                        ... ]
                    },
                    ... ],
   "directory sets" : [
                        { "name" : "DriveNames",
                          "expansion" : [ "USB1/.mcsync",
                                          "USB2/home/.mcsync",
                                          ... ]
                        },
                        ... ],
   "devices" : [
                    { "name" : "Old USB",
                      "id" : "5123ac84701983c4a7d2d9c139823",
                      "color" : "red" },
                    ... ],
   "grafts" : [
                { "device" : "Old USB",
                  "device graft point" : "Work/Documents",
                  "virtual graft point" : "docs",
                  "prune points" : [ "Project4/BigFiles",
                                     "Project6/",           SLASH MEANS CONTENTS
                                     ... ]
                },
                ... ],
   "tree prefs" : [
                    { "pref point" : "docs/Project3/svn",
                      { "prune grafts here" : true, ... } },
                    { "pref point" : "docs/Project1",
                      { "autoscan" : true,
                        "preferred device" : "server", ... } },
                    ... ]
 }

*/

machine         *machinelist;
directoryset    *directorysetlist;
device          *devicelist;
graft           *graftlist;


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of specs IO ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

char *nextline(FILE *f)
{
    char *line;
    do {
        line = getstring(f, '\n');
    } while ((line[0] == 0 || line[0] == '#') && (free(line), 1));
    return line;
} // nextline

int32 readspecs1(FILE *f) // reads the rest of a version-1 specs file,
                        // returns 1 if sucessful
{
    char *line, *part2;

    device **devicelistp = &devicelist;
    graft **graftlistp = &graftlist;
    *devicelistp = NULL; // because we will be searching the list

    // the loop below figures out what to do with the most recently read line
    // so first, we read a line
    line = nextline(f);

    while (1) {
        if (! strcmp(line, "end of specs")) {
            *graftlistp = NULL;
            free(line);
            break;

        } else if (! strncmp(line, "device ", 7)) { // a device
            // read about a device
            device *drecord;
            stringlist **addrsp;

            drecord = (device*) malloc(sizeof(device));
            *devicelistp = drecord;
            devicelistp = &(drecord->next);
            *devicelistp = NULL; // because we will be searching the list

            drecord->nickname = strdup(line + 7);
            free(line);
            drecord->status = status_inactive;
            drecord->reachplan.routeraddr = -1;


            drecord->deviceid = nextline(f);

            line = nextline(f);

            if (!strcmp(line, "[ok]"))
                drecord->linked = 1;
            else
                drecord->linked = 0;
            free(line);

            addrsp = &(drecord->reachplan.ipaddrs);
            while (1) {
                stringlist *string;
                line = nextline(f);
                if (!strncmp(line, "graft ", 6) || !strncmp(line, "device ", 7))
                    break;
                string = (stringlist*) malloc(sizeof(stringlist));
                *addrsp = string;
                addrsp = &(string->next);

                string->string = line;
            }
            *addrsp = NULL;

        } else if (! strncmp(line, "graft ", 6)) { // a graft
            part2 = index(line + 6, '$');
            // read about a graft
            graft *grecord;
            stringlist **ignoresp;
            device *where;

            grecord = (graft*) malloc(sizeof(graft));
            *graftlistp = grecord;
            graftlistp = &(grecord->next);

            // set host, hostpath, virtualpath, and prunepoints

            grecord->hostpath = strdup(part2 + 1);

            *part2 = '\000';
            for (where = devicelist; where != NULL; where = where->next) {
                if (! strcmp(where->nickname, line + 6)) {
                    grecord->host = where;
                    break;
                }
            }
            if (where == NULL) {
                printf("Error: specs file: graft uses undefined device: %s\n",
                        line + 6);
                return 0;
            }
            free(line);

            grecord->virtualpath = nextline(f);

            ignoresp = &(grecord->prunepoints);
            while (! strncmp(line = nextline(f), "ignore ", 7)) {
                stringlist *string = (stringlist*) malloc(sizeof(stringlist));
                *ignoresp = string;
                ignoresp = &(string->next);
                string->string = strdup(line + 7);
                free(line);
            }
            *ignoresp = NULL;

        } else {
            printf("Error: unexpected line found in specs file:\n%s\n",
                   line);
            return 0;
        }
    }
    return 1;
} // readspecs1

int32 readspecsfile(char *specsfile) // sets up devicelist and graftlist
{
    FILE *f;
    char *line;
    int fileversion;

    f = fopen(specsfile, "r");
    if (f == NULL) {
        printf("Error: Could not open specs file %s (%s)\n",
                specsfile, strerror(errno));
        return 0;
    }

    line = nextline(f);

    if (strncmp(line, "version ", 8)) {
        printf("Error: First line of specs file (%s) should be 'version n'"
                ", where n is the version of the file format (e.g. %d).\n",
                specsfile, specsfileversionnumber);
        return 0;
    }
    fileversion = atoi(line + 8);
    if (fileversion < 1) {
        printf("Error: specs file (%s) does not specify a positive integer"
                " version.\n", specsfile);
        return 0;
    }
    if (fileversion > specsfileversionnumber) {
        printf("Warning: specs file (%s) was written with format version %d,"
                " but I only know formats up to version %d.\n"
                "I will try to read it anyway.\n",
                specsfile, fileversion, specsfileversionnumber);
    }

    free(line);

    switch (fileversion) {
        case 1:
                return readspecs1(f);
                break;
        default:
                printf("I will try reading the specs file using format version "
                        "1.\n");
                return readspecs1(f);
                break;
    }
    fclose(f);
} // readspecsfile

int32 writespecsfile(char *specsfile) // writes devicelist and graftlist,
                                      // returns 1 if sucessful
{
    FILE *f;
    device *d;
    graft *g;
    stringlist *s;

    f = fopen(specsfile, "w");
    if (f == NULL) {
        printf("Error: Could not open specs file %s for writing (%s)\n",
                specsfile, strerror(errno));
        return 0;
    }

    fprintf(f, "version 1\n");

    for (d = devicelist; d != NULL; d = d->next) {
        fprintf(f, "\ndevice %s\n%s\n%s\n", // <- if you change this, fix linkdeviceinspecs
                   d->nickname, d->deviceid,
                   d->linked ? "[ok]" : "[??]");
        for (s = d->reachplan.ipaddrs; s != NULL; s = s->next) {
            fprintf(f, "%s\n", s->string);
        }
    }

    for (g = graftlist; g != NULL; g = g->next) {
        fprintf(f, "\ngraft %s$%s\n%s\n",
                g->host->nickname, g->hostpath, g->virtualpath);
        for (s = g->prunepoints; s != NULL; s = s->next) {
            fprintf(f, "ignore %s\n", s->string);
        }
    }

    fprintf(f, "\nend of specs\n");

    fclose(f);

    return 1;
} // writespecsfile

int32 specstatevalid(void) // returns 1 if there exists a device with any reachplan and a graft;
{
    graft *g;
    device *d;

    // at least one device
    for (d = devicelist; d != NULL; d = d->next) {
        // with address and McSync folder
        if (d->reachplan.ipaddrs != NULL) {
            // and at least one corresponding graft
            for(g = graftlist; g != NULL; g = g->next) {
                if(g->host == d)
                    return 1;
            }
        }
    }
    return 0;
} // specstatevalid

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of specs IO /////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

