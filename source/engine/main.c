#include "definitions.h"


// globals -- these are externed in definitions.h

intlist OurChildren; // pids of all child processes, so we can kill them on exit

FILE* ourerr = NULL; // we use this (often via log_line) instead of stderr

void ourperror(char* whatdidnotwork) // writes to ourerr, not stderr
{
    log_line("%s: %s\n", whatdidnotwork, strerror(errno));
} // ourperror

virtualnode virtualroot; // has no siblings and no name


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of main ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

void print_version(const char* preamble)
{
    int bignum = programversionnumber;
    int release = bignum % 100;
    bignum /= 100;
    int subversion = bignum % 100;
    bignum /= 100;
    int version = bignum;
    printf("%sThis is mcsync, version %d.%d.%d.\n", preamble,
                                                    version, subversion, release);
} // print_version

int main(int argc, char**argv)
{
    char* interface;
    OurChildren = emptyintlist();
    int exit_code = 0; // 0 = no error      Note: when setting to __LINE__,
    // only lowest byte is returned as exit code, hope this file is <256 lines!
    
    // create error stream
    //ourerr = stderr;
    ourerr = fopen("data/log", "w"); // do "tail -f" on each machine to debug
    if (ourerr == NULL) {
        perror("Couldn't open log file data/log");
        cleanexit(__LINE__);
    }
    log_line("\n-------------------------------------------------------\n");
    
    // version-only invocation
    if (argc == 2 &&
            (    !strcmp(argv[1], "-version")
              || !strcmp(argv[1], "--version")
              || !strcmp(argv[1], "-v")
              || !strcmp(argv[1], "-V"))) {
        print_version("");
        cleanexit(0);
    }

    // any sanity checks we want to run?  (should not be run for version-only)
    if (!checktypes())
        cleanexit(__LINE__);

    // standard invocation
    interface = NULL;
    if (argc == 1)
        interface = "tui";
    if (argc == 2 && strlen(argv[1]) > 2 && !strncmp(argv[1], "-i", 2))
        interface = argv[1] + 2; // +2 to skip over "-i"
    if (argc == 3 && !strcmp(argv[1], "-i"))
        interface = argv[2];
    if (interface != NULL) {
        int which = 0; // same as 'interface' but an easy int
        if (!strcmp(interface, "cli") || !strcmp(interface, "CLI")) {
            printf("Very sorry, CLI is not yet implemented.\n");
            exit_code = __LINE__;
            goto ShowHelpAndExit;
        }
        if (!strcmp(interface, "tui") || !strcmp(interface, "TUI")) {
            which = 2;
        }
        if (!strcmp(interface, "gui") || !strcmp(interface, "GUI")) {
            printf("Very sorry, GUI is not yet implemented.\n");
            exit_code = __LINE__;
            goto ShowHelpAndExit;
        }
        if (!strcmp(interface, "batch") || !strcmp(interface, "BATCH")) {
            printf("Very sorry, batch mode is not yet implemented.\n");
            exit_code = __LINE__;
            goto ShowHelpAndExit;
        }
        if (which == 0) {
            printf("Error: unknown interface: \"%s\"\n", interface);
            exit_code = __LINE__;
            goto ShowHelpAndExit;
        }
        // now we want to start up mcsync with the appropriate interface
        // we want to start 2 workers: machine worker and some cmd
        //   routing address 3 (cmd_pob): cmd
        //   routing address 6 (next_free_pob): machine (connects executable's device)
        //   routing address 3: device (could be "disconnected" by user while running)
        pthread_mutex_init(&virtualtree_mutex, NULL);
        cmd_thread_start_function = TUImain; // start in master mode, using tui as cmd
        routermain(2); // 2 is the routing address of the master machine
        cleanexit(0); // never reached
    }

    // slave invocation
    if (argc == 2 && !strcmp(argv[1], "-slave")) {
        // no command line argument simplifies reaching: A -> B is always the same
        // but this means we have to get our pobox number through i/o
        int32 machine_routing_address;
        raw_io(); // so waitforstring() below can receive its input
                // which it can receive anytime after sending slave_start_string
        log_line("slave started ok\n");
        //ourerr = fopen("/dev/null", "w"); // ssh is mixing it with stdout!
        printf("%s\n", slave_start_string);
        // from now on we must only send messages
        fflush(stdout);
        waitforstring(stdin, hi_slave_string);
        machine_routing_address = get32safe(stdin);
        log_line("received machine routing address: %d\n", machine_routing_address);
        // from now on we only get messages
        routermain(machine_routing_address);
        return 0;
    }

    // usage invocation
    if (argc == 2 &&
            (    !strcmp(argv[1], "-help")
              || !strcmp(argv[1], "--help")
              || !strcmp(argv[1], "-h")
              || !strcmp(argv[1], "-H"))) {
        ShowHelpAndExit:
        print_version("  ");
        printf("  usage:\n");
        printf("    mcsync\n");
        printf("                Interactive interface for syncing data.\n");
        printf("    mcsync -i <interface type>\n");
        printf("                Specifies desired interface:\n");
        printf("                    cli = command line interface.\n");
        printf("                    tui = 2d textual interface in terminal window.\n");
        printf("                    gui = graphical user interface.\n");
        printf("                    batch = no interface, run in batch mode.\n");
        printf("    mcsync -slave\n");
        printf("                Slave mode, does binary I/O instead of ascii.\n");
        printf("                (Invoked by mcsync when it needs a remote agent.)\n");
        printf("    mcsync -version\n");
        printf("                Print version number and exit.\n");
        printf("    mcsync -help\n");
        printf("                Print this usage info and exit.\n");
        cleanexit(exit_code);
    }

    // erroneous invocation
    if (1) {
        int argi;
        printf("Did not understand arguments.  They were:\n");
        for (argi = 0; argi < argc; argi++)
            printf("%d: %s\n", argi, argv[argi]);
        if (argc == 1)
            printf("  (there were no arguments)\n");
        printf("Better luck next time.\n");
        exit_code = __LINE__;
        goto ShowHelpAndExit;
    }
} // main

//////////////////////////////////////////////////////////////////////////////////
////////////////////////////////// end of main ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////


void cleanexit(int code) // kills children and exits
{
    // we need to kill all our children with -9
    int kidnum;
    for (kidnum = 0; kidnum < OurChildren->count; kidnum++)
        kill(OurChildren->values[kidnum], SIGKILL);
    usleep(1000);
    log_line("exiting with exit code %d\n", code);
    exit(code);
} // cleanexit

//////////////////////////////////////////////////////////////////////////////////
////////////////////////////////// thread main ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

void channel_launch(connection plug, thread_main_func thread_main)
{
    pthread_create(&plug->listener, &pthread_attributes,
                    thread_main, (void *)plug);
} // channel_launch

void* machineworker_main(void *voidplug)
{
    connection plug = (connection) voidplug; // so compiler knows type
    workermain(plug);
    return NULL;
} // machineworker_main

void* parent_main(void *voidplug)
{
    connection plug = (connection) voidplug; // so compiler knows type
    // we use three threads (this + 2 others) to handle the three I/O streams
    plug->tokid = stdout; // "kid" refers to remote end -- here kid's the parent!
    plug->fromkid = stdin; // and stream_receiving will read from stdin, etc.
    pthread_create(&plug->stream_shipper, pthread_attr_default,
        &stream_shipping, (void *)plug); // created shipping thread never returns
    stream_receiving(plug); // never returns
    return NULL; // keep compiler happy
} // parent_main

void* recruiter_main(void *voidplug)
{
    recruitermain(); // never returns
    return NULL;
} // recruiter_main

void* headquarters_main(void *voidplug)
{
    hqmain(); // never returns
    return NULL;
} // headquarters_main

void *cmd_main(void *voidplug)
{
    cmd_thread_start_function(); // returns when user exits,
                                 // set by main.c to user choice
                                 // (e.g. TUImain or climain)
    cleanexit(0); // kill all other threads and really exit
    return NULL;
} // cmd_main

