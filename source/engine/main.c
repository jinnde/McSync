#include "definitions.h"


// globals -- these are externed in definitions.h

intlist OurChildren; // pids of all child processes, so we can kill them on exit

FILE* ourerr = NULL; // we use this (often via printerr) instead of stderr

void ourperror(char* whatdidnotwork) // writes to ourerr, not stderr
{
    printerr("%s: %s\n", whatdidnotwork, strerror(errno));
} // ourperror

device *devicelist; // the list of devices we know about

graft *graftlist; // the list of grafts we know about

virtualnode virtualroot; // has no siblings and no name


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of main ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

int checktypes(void)
{
    int ret = 1;
    {
        int32 j = 1;
        int i = 0;
        while (j) {
            i++;
            j <<= 1;
        }
        if (i != 32) {
            printf("Error: type int32 has %d bits instead of 32\n", i);
            ret = 0;
        }
    }
    {
        int64 j = 1;
        int i = 0;
        while (j) {
            i++;
            j <<= 1;
        }
        if (i != 64) {
            printf("Error: type int64 has %d bits instead of 64\n", i);
            ret = 0;
        }
    }
    return ret;
} // checktypes

int main(int argc, char**argv)
{
    int i;
    //ourerr = stderr;
    ourerr = fopen("data/log", "w"); // do "tail -f" on each machine to debug
    if (ourerr == NULL) {
        perror("Couldn't open log file data/log");
        exit(1); // haven't set up OurChildren yet, so can't do cleanexit
    }

    printerr("\n-------------------------------------------------------\n");
    OurChildren = emptyintlist();

    if (argc == 2 &&
            (    !strcmp(argv[1], "-version")
              || !strcmp(argv[1], "--version")
              || !strcmp(argv[1], "-V")
              || !strcmp(argv[1], "-v"))) {
        printf("This is mcsync, version %d.\n", programversionnumber);
        cleanexit(0);
    }

    if (!checktypes())
        cleanexit(__LINE__);

    if (argc == 2 && !strcmp(argv[1], "-slave")) {
        int32 whoami;
        raw_io(); // so waitforstring() below can receive its input
        // which it can receive anytime after the next line
        printerr("slave started ok\n");
        //ourerr = fopen("/dev/null", "w"); // ssh is mixing it with stdout!
        printf("%s\n", slave_start_string); // after this we must send messages
        fflush(stdout);
        waitforstring(stdin, hi_slave_string);
        whoami = get32safe(stdin);
        routermain(0, whoami); // start in slave mode
        return 0;
    }

    if (argc == 1) {
        cmd_thread_start_function = TUImain; // start in master mode, using tui as cmd
        routermain(1, 0);
        return 0;
    }

    if (argc == 2 && !strcmp(argv[1], "-cli")) {
        cmd_thread_start_function = climain; // start in master mode, using cli as cmd
        routermain(1, 0);
        return 0;
    }

    printf("Did not understand arguments.  They were:\n");
    for (i = 0; i < argc; i++)
        printf("%d: %s\n", i, argv[i]);
    if (argc == 1)
        printf("  (there were no arguments)\n");
    printf("Better luck next time.\n");

    printf("  This is mcsync, version %d.\n", programversionnumber);
    printf("  usage:\n");
    printf("    mcsync\n");
    printf("                Interactive interface for syncing data.\n");
    printf("    mcsync -cli\n");
    printf("                Command line based interface instead of TUI.\n");
    printf("    mcsync -slave N\n");
    printf("                Slave mode, does binary I/O instead of ascii.\n");
    printf("                (Invoked by mcsync when it needs a remote agent.)\n");

    return 0;
} // main

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of main /////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////


void cleanexit(int code) // kills children and exits
{
    // we need to kill all our children with -9
    int kidnum;
    for (kidnum = 0; kidnum < OurChildren->count; kidnum++)
        kill(OurChildren->values[kidnum], SIGKILL);
    usleep(1000);
    printerr("\n");
    exit(code);
} // cleanexit

