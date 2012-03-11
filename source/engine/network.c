#include "definitions.h"

/*

This stack of functions is only called as described here.  It is a pretty linear sequence,
but thread_main and raisechild are in separate threads,
and firststeps is in a separate process.

main --- the entry point for McSync, whether slave or master.  Calls routermain.
routermain --- the post office.  Delivers mail and sets up plugs.
                This should be the only place that calls channel_launch, but workermain
                is still calling it.
channel_launch --- creates a new plug, and a new thread for the other side of the plug.
                The new thread starts in thread_main.
thread_main --- sets up an agent: tui, algo, worker, or SR (parent or remote agent).
                To set up a remote agent, calls reachforremote, and on success creates
                the shipping and receiving threads.
reachforremote --- calls givebirth on the one hand to create a child process, calls
                raisechild (in a separate thread) on the other hand to "type" into the
                child process until McSync is up and running, and on the third hand
                (with the calling thread) watches whether raisechild claims success
                within twenty seconds (polling a flag at 40 Hz).
                It tries all this for each way it might reach the target machine,
                and returns 0 on success.
raisechild --- listens and types into an ssh session, perhaps logging in to further
                remote machines, and tries to get a McSync running.
givebirth --- like a fancy fork.  It forks, sending the child to firststeps (not back to
                the caller).  Being fancy, it creates pipes to communicate with the kid,
                closes the kid's ends after forking, and creates streams for the pipes.
                The streams and pipes are stored in the plug.
firststeps --- the child's prong of the fancy fork.  This moves the created pipes to
                be stdin, stdout, and stderr.  Then it transfers control to transmogrify.
                This might ought to close other file descriptors besides the pipe ends?????
transmogrify --- this turns itself into (replaces itself with) an expect or ssh process.
                That ends our code's control of the child process.  After that, we only
                control the child through its stdin and stdout.  Specifically, raisechild
                is doing this using the streams in the plug.

bugs:
* forward_raw_errors seems to never actually do anything, only being called after nothing
more will be written on stderr.
* the router, not the worker, should be setting up remote connections.
* firststeps might ought to close other random file descriptors that the parent process has.
* right now a channel_launch request by a worker winds up setting the plug number in the
devicelocater based on the deviceid... what's the point of that contortion?
* should deal with SIGPIPE so that a crash in the reaching process doesn't kill the
parent.  For example, it could set the failed flag that reachforremote is polling.

*/

/*

from the SSH man page:

     -T      Disable pseudo-tty allocation.

     -t      Force pseudo-tty allocation.  This can be used to execute
             arbitrary screen-based programs on a remote machine, which can be
             very useful, e.g. when implementing menu services.  Multiple -t
             options force tty allocation, even if ssh has no local tty.

     ...

     If a pseudo-terminal has been allocated (normal login session), the user
     may use the escape characters noted below.

     If no pseudo-tty has been allocated, the session is transparent and can
     be used to reliably transfer binary data.  On most systems, setting the
     escape character to ``none'' will also make the session transparent even
     if a tty is used.

I need a tty because otherwise the externally accessible "login" machine at my work does not
permit me to continue with a 2nd ssh hop to the machine I actually wanted to connect to.

The problem is then that across the full connection, the characters are being modified
somewhere, namely ... to ...

With this second ssh hop, I can use -T ...

*/

char* homedirectory(void) // caches answer -- do not alter or free!
{
    static char* home = NULL;
    if (home == NULL)
        home = strdup(getpwuid(getuid())->pw_dir);
    return home;
} // homedirectory

char* hostname(void) // caches answer -- do not alter or free!
{
    static char* host = NULL;
    if (host == NULL)
        host = getenv("HOST");
    if (host == NULL)
        host = getenv("HOSTNAME");
    if (host == NULL) {
        printf("Error: environment variable $HOST not set (nor $HOSTNAME)\n");
        host = "";
    }
    return host;
} // hostname

char* username(void) // caches answer -- do not alter or free!
{
    static char* name = NULL;
    if (name == NULL)
        name = strdup(getpwuid(getuid())->pw_name);
    return name;
} // username


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of spawning ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

// It appears that upon forking the new process only gets a single thread,
// namely the one that returns 0 from fork().
// So pthread_atfork(NULL, NULL, &pthread_exit_noargs) is not only unnecessary,
// but in fact it is deadly, because everything registered by pthread_atfork()
// is executed by the forking thread, which keeps its thread_id through the fork!

void transmogrify(device* target) // turn into an ssh process
{
    char* mname = NULL;
    char* uname = NULL;

    // parse address to figure out what to do
    char* instr = target->reachplan.whichtouse; // skips device name part if any
    // here we just need to parse the first ssh command
    char* pos;
    pos = index(instr, ':'); // find first ':'
    if (pos == NULL) {
        printerr("Error: no ':' in address\n");
        cleanexit(__LINE__);
    }
    *pos = 0; // we have already forked, so who cares if we trash it!
    pos = index(instr, '`'); // see if there's a '`'
    if (pos != NULL) // if so, end address there
        *pos = 0;
    mname = index(instr, '@');
    if (mname == NULL) {
        uname = username();
        mname = instr;
    } else {
        uname = instr;
        *mname = 0;
        mname = mname + 1;
    }

    // fcntl(d, F_SETFD, 1); // makes descriptor close on successful execve
    if (1) { // ExpectOrSSH: SSH
        execl("/usr/bin/ssh",// the program to run
            "ssh",          // arg0, here we just follow convention
            "-tt",          // give us a pseudo-terminal
            //"-s",         // do the subsystem thing, whatever that is NO!
            "-e", "none",   // don't let there be any escape char to ssh's mini-UI
            "-l", uname,    // login as given user
            mname,          // on given machine
            //"\"/Users/cook/Common/Coding/Unix/McSync/"
            //             "McSyncDeviceArchive/src/mcsync -slave 4\"",
            // the commented things break it for unknown reasons
            NULL);          // end of command-line arguments
    } else { // ExpectOrSSH: Expect
        execl("/usr/bin/expect", "expect", "/Users/cook/Common/Coding/Unix/"
                "McSync/McSyncDeviceArchive/config/laptop2laptop.exp", NULL);
    }
    // that should not return!
    ourperror("execl failed");
    // we probably ought to signal parent
    cleanexit(__LINE__); // don't return!
} // transmogrify

void firststeps(connection plug) // executed by child process, doesn't return
{
    // we are the child process
    // remap stdin, stdout, and stderr to communicate with parent
    if (   close(plug->kidinpipe[WRITE_END]) // not strictly necessary, but
        || close(plug->kidoutpipe[READ_END]) // is tidy and saves table space
        || close(plug->kiderrpipe[READ_END])) {
        ourperror("Pipe close failed in child");
    }
    // I can't tell from the docs what happens to stream if des is closed
    // but I would guess: nothing, because des only described file
    if (fclose(stdin)) { // dup2 would do this anyway if we didn't do it here
        ourperror("Child: Error closing stdin");
        cleanexit(__LINE__);
    }
    if (fclose(stdout)) {
        ourperror("Child: Error closing stdout");
        cleanexit(__LINE__);
    }
    if (fclose(stderr)) {
        // who can we tell... we're deaf dumb and blind!
        cleanexit(__LINE__); // that's who!
    }
    // remap the new pipe connections to stdin, stdout, and stderr
    if (   dup2(plug->kidinpipe[READ_END],    STDIN_FILENO /*=0*/) == -1
        || dup2(plug->kidoutpipe[WRITE_END], STDOUT_FILENO /*=1*/) == -1
        || dup2(plug->kiderrpipe[WRITE_END], STDERR_FILENO /*=2*/) == -1) {
        // who can we tell... we're deaf dumb and blind!
        cleanexit(__LINE__); // that's who!
    }
    // apparently stdin, stdout, and stderr do not need to be opened before use?
    // because we just closed them...
    if (   close(plug->kidinpipe[READ_END])
        || close(plug->kidoutpipe[WRITE_END])
        || close(plug->kiderrpipe[WRITE_END])) {
        ourperror("Pipe close failed in child"); // go tell mom
    }
    transmogrify(plug->target_device); // doesn't return
} // firststeps

int givebirth(connection plug) // fork off an ssh we can talk to
{
    pid_t kidpid = -1;

    if (    pipe(plug->kidinpipe)
         || pipe(plug->kidoutpipe)
         || pipe(plug->kiderrpipe)) {
        ourperror("Couldn't create pipe");
        goto failure;
    }

    kidpid = fork(); // all other threads continue only in parent
    if (kidpid == -1) {
        ourperror("Fork failed");
        goto failure;
    }
    if (kidpid == 0) {
        // we are the child process
        firststeps(plug);
        // doesn't return
    }
    // we are the parent process
    addtointlist(OurChildren, kidpid); // we must do this before we might exit
    plug->processpid = kidpid;

    // patch up communication with child
    if (   close(plug->kidinpipe[READ_END])
        || close(plug->kidoutpipe[WRITE_END])
        || close(plug->kiderrpipe[WRITE_END])) {
        ourperror("Pipe close failed in parent");
        // it will work anyway even if these didn't close, but they can pile up
    }
    plug->tokid = fdopen(plug->kidinpipe[WRITE_END], "w");
    if (! plug->tokid) {
        ourperror("Error opening stream to child");
        goto failure;
    }
    plug->fromkid = fdopen(plug->kidoutpipe[READ_END], "r");
    if (! plug->fromkid) {
        ourperror("Error opening stream from child");
        goto failure;
    }
    plug->errfromkid = fdopen(plug->kiderrpipe[READ_END], "r");
    if (! plug->errfromkid) {
        ourperror("Error opening error stream from child");
        goto failure;
    }

    return kidpid;

    failure:
    if (kidpid != -1) {
        kill(kidpid, SIGKILL);
        removefromintlist(OurChildren, kidpid);
    }
    return -1;
} // givebirth

volatile int passinginput;
// the point of this function is to allow typing to the remote process while connecting
void* passinput(void* streamout) // threadgiver thread starts here
{
    FILE* sout = (FILE*) streamout; // so compiler knows type
    // passinginput = 1; // optimizer does not understand this is VOLATILE!!!
    raw_io(); // turn off local echoing, pass chars as they are typed
    while (passinginput) { // gets set to 0 by other thread to kill us
        int c = getch(); // but if this blocks, one char will be taken after 0
        //printerr("<%d:%d:%d:%c>", ERR, EOF, passinginput, c);
        if (c != ERR) {
            if (passinginput) {
                putc(c, sout);
                fflush(sout);
            } else {
                ungetc(c, stdin);
            }
        } else {
            usleep(1000); // this can happen if raw_io() includes nodelay()
        }
    }
    return NULL;
} // passinput

int raised, failed;

char* copynchars(char* source, int len)
{
    char* copy = (char*) malloc(len + 1);
    strncpy(copy, source, len);
    copy[len] = 0;
    return copy;
} // nchars

void* raisechild(void* voidplug)
{
    connection plug = (connection) voidplug;
    char* nuthin = "";
    if (0) { // ExpectOrSSH: Expect
        waitforstring(plug->fromkid, "expect script is running");
        fprintf(plug->tokid, "hover\n");
    }
    if (1) { // ExpectOrSSH: SSH
        char* uname, * mname;
        char* sep = index(plug->target_device->reachplan.whichtouse, ':');
        char* psep = index(plug->target_device->reachplan.whichtouse, '`');
        if (sep == NULL) {
            printerr("Error: Couldn't find ':' in net path.\n");
            failed = 1;
            return NULL;
        }
        // wait for prompt from machine we ssh'd to
        if (psep != NULL && psep < sep) { // we know what to wait for
            waitforsequence(plug->fromkid, psep + 1, sep - psep - 1, 1);
        } else { // we don't know what to wait for, so just wait a bit
            usleep(6000000);
        }
        pthread_t inputgiver;
        passinginput = 1;
        pthread_create(&inputgiver, pthread_attr_default, &passinput,plug->tokid);
        while (1) {
            char* nextsep = index(sep + 1, ':');
            char* at;
            if (nextsep == NULL)
                break; // the while (1)
            for (psep = sep; psep < nextsep; psep++)
                if (*psep == '`')
                    break; // just the for
            // if there was no '`', then psep == nextsep
            for (at = sep; at < psep; at++)
                if (*at == '@')
                    break; // just the for
            // we can't butcher the string with 0s cause other threads use it too
            if (at == psep) { // if there was no @
                uname = nuthin;
                mname = copynchars(sep + 1, psep - sep - 1);
            } else { // there was an @
                uname = copynchars(sep + 1, at - sep - 1);
                mname = copynchars(at + 1, psep - at - 1);
            }
            fprintf(plug->tokid, "ssh -e none %s%s %s\n",
                    (uname && *uname) ? "-l " : "",
                    (uname && *uname) ? uname : "",
                    mname);
            fflush(plug->tokid);
            printerr("Reaching %s...\n", mname);
            free(mname);
            if (uname != nuthin)
                free(uname);
            // wait for prompt from machine we ssh'd to
            if (psep < nextsep) { // we know what to wait for
                waitforsequence(plug->fromkid, psep + 1, nextsep - psep - 1, 1);
            } else { // we don't know what to wait for, so just wait a bit
                usleep(6000000);
            }
            sep = nextsep;
        }
        passinginput = 0; // kill inputgiver
        printerr("Just set passing input to 0.\n");
        pthread_cancel(inputgiver);
        pthread_detach(inputgiver); // docs don't say what detach/cancel mean
        // and they don't have any effect if thread is blocked in getc!!!
        // so there is no way to keep the thread from stealing a future char!!
        // actually detach means don't have it wait to join other when exiting
        fprintf(plug->tokid, "\n%s/mcsync -slave\n", sep + 1);
        fflush(plug->tokid);
    }
    waitforstring(plug->fromkid, slave_start_string); // till mcsync is live
    fprintf(plug->tokid, hi_slave_string);
    put32safe(plug->tokid, plug->target_device->reachplan.routeraddr);
    fflush(plug->tokid);
    raised = 1;
    return NULL;
} // raisechild

int reachforremote(connection plug) // try to get mcsync started on remote site
{
    // here we try the various ways of getting to the device
    // supplying passwords along the way as necessary
    // and once there, we invoke the device's "mcsync -slave <addr>"
    int retval = 0;
    pthread_t raiser;
    password_pause = 1; // tell TUI (or parent) to not steal stdin characters
    TUIstop2D(); // switch to normal terminal scrolling mode
    printerr("<entering terminal mode>\n");
    stringlist* netpath;
    for (netpath = plug->target_device->reachplan.ipaddrs;
            netpath != NULL;
            netpath = netpath->next) {
        plug->target_device->reachplan.whichtouse = netpath->string;
        int kidpid = givebirth(plug); //////// <<<<<<<<<<<<<<<<<<<<<<<< ////////
        if (kidpid == -1)
            continue;
        raised = 0;
        failed = 0;
        struct timeval basetime, latertime;
        gettimeofday(&basetime, NULL);
        pthread_create(&raiser, pthread_attr_default, &raisechild, (void*)plug);
        while (! raised && ! failed) {
            usleep(25000); // we don't know how long this actually takes
            // check for timeout
            gettimeofday(&latertime, NULL);
            if ((latertime.tv_sec - basetime.tv_sec) * 1000 * 1000
                    + (latertime.tv_usec - basetime.tv_usec) > 20 * 1000 * 1000) {
                printerr("Timed out.\n");
                pthread_cancel(raiser);
                pthread_detach(raiser); // docs don't say what detach/cancel mean
                kill(kidpid, SIGKILL);
                removefromintlist(OurChildren, kidpid);
                raised = 0; // just in case it made it to 1 as it died
                failed = 1;
            }
        }
        if (! failed) // i.e. if it succeeded
            break;
    }
    if (netpath == NULL) { // total failure -- we failed on all netpaths
        //setstatus(plug->target_device->reachplan.routeraddr, status_inactive);
        //status only has meaning on master device!
        char buf[90];
        snprintf(buf, 90, "%d", plug->thisway->values[0]);
        sendmessage(worker_plug, algo_int, msgtype_disconnect, buf); // BUG
        // we are not the worker thread!!!! and sendmessage is not thread-safe...
        // but our own msg queue will never start functioning in this error case
        retval = 1;
    }
    printerr("<leaving terminal mode>\n");
    TUIstart2D(); // switch back to 2D interface
    password_pause = 0;
    return retval; // 0 == success
} // reachforremote

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of spawning /////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

