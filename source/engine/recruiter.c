#include "definitions.h"

/*

This stack of functions is only called as described here.

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

static int32 next_free_address = firstfree_int;

static queue recruitercallbacks;

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

void transmogrify(connection plug) // turn into an ssh process
{
    if (1) { // ExpectOrSSH: SSH
        execl("/usr/bin/ssh",               // the program to run
            "ssh",                          // arg0, here we just follow convention
            "-tt",                          // give us a pseudo-terminal
            //"-s",                         // do the subsystem thing, whatever that is NO!
            "-e", "none",                   // don't let there be any escape char to ssh's mini-UI
            "-l", plug->session.uname,      // login as given user
            plug->session.mname,            // on given machine
            "-M",                           // start in master mode for session multiplexing
            "-S", plug->session.path,       // store the session in the tmp folder
            NULL);                          // end of command-line arguments
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
    transmogrify(plug); // doesn't return
} // firststeps

int givebirth(connection plug) // fork off a process we can talk to
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
    pthread_exit(NULL);
} // passinput

char* copynchars(char* source, int len)
{
    char* copy = (char*) malloc(len + 1);
    strncpy(copy, source, len);
    copy[len] = 0;
    return copy;
} // nchars

int32 raised, failed;

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
        char* sep = index(plug->address, ':');
        char* psep = index(plug->address, '`');
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
        pthread_create(&inputgiver, &pthread_attributes, &passinput, plug->tokid);
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
        fprintf(plug->tokid, "\n%s/mcsync -slave\n", sep + 1);
        fflush(plug->tokid);
    }
    waitforstring(plug->fromkid, slave_start_string); // till mcsync is live
    fprintf(plug->tokid, "%s", hi_slave_string);
    put32safe(plug->tokid, plug->plugnumber);
    putstring(plug->tokid, plug->address);
    fflush(plug->tokid);
    raised = 1;
    pthread_exit(NULL);
} // raisechild

int32 reachforremote(connection plug) // try to get mcsync started on remote site
{
    // here we try to connect to the remote McSync device supplying passwords along
    // the way as necessary and once there, we invoke the device's "mcsync -slave <addr>"
    int retval = 1;
    pthread_t raiser;
    password_pause = 1; // tell TUI (or parent) to not steal stdin characters
    // TODO: This hould really be done using a message to TUI
    TUIstop2D(); // switch to normal terminal scrolling mode
    printerr("<entering terminal mode>\n");

    int kidpid = givebirth(plug); //////// <<<<<<<<<<<<<<<<<<<<<<<< ////////

    if (kidpid == -1) {
        raised = 0;
        failed = 1;
        goto leave_terminal_mode;
    }

    raised = 0;
    failed = 0;
    struct timeval basetime, latertime;
    gettimeofday(&basetime, NULL);
    pthread_create(&raiser, &pthread_attributes, &raisechild, (void*)plug);

    while (! raised && ! failed) {
        usleep(25000); // we don't know how long this actually takes
        // check for timeout
        gettimeofday(&latertime, NULL);
        if ((latertime.tv_sec - basetime.tv_sec) * 1000 * 1000
                 + (latertime.tv_usec - basetime.tv_usec) > 20 * 1000 * 1000) {
                printerr("Timed out.\n");
                pthread_cancel(raiser);
                kill(kidpid, SIGKILL);
                removefromintlist(OurChildren, kidpid);
                raised = 0; // just in case it made it to 1 as it died
                failed = 1;
            }
        }

leave_terminal_mode:
    if (raised == 0 || failed == 1) { // total failure
        printerr("Could not connect!\n");
        retval = 0;
    }
    printerr("<leaving terminal mode>\n");
    TUIstart2D(); // switch back to 2D interface
    password_pause = 0;
    return retval; // 1 == success
} // reachforremote

int32 recruitworker(int32 plugnumber, char *address) // modifies remote addresses
{ // (those who do not start with "local:")
    connection plug = findconnectionbyplugnumber(plugnumber);
    char *pos, *uname, *mname;

    if (!plug)
        return 0;

    plug->address = strdup(address);

    if (!strncmp(address, "local:", 6)) {
        channel_launch(plug, &localworker_initializer);
        return 1;
    }

    // it's a remote connection, butcher address into information for ssh
    pos = index(address, ':');
    if (pos == NULL) {
        printerr("Error: no ':' in address\n");
        return 0;
    }
    *pos = 0; // the plug has a full copy, so we can trash it now.
    pos = index(address, '`'); // see if there's a '`'
    if (pos != NULL) // if so, end address there
        *pos = 0;
    mname = index(address, '@');
    if (mname == NULL) {
        uname = username();
        mname = address;
    } else {
        uname = address;
        *mname = 0;
        mname = mname + 1;
    }
    plug->session.uname = strdup(uname);
    plug->session.mname = strdup(mname);
    plug->session.path = strdupcat(tmpnam(NULL), "-mcsync-ssh-session-", uname, "@", mname, NULL);

    // fill plug with streams to remote mcsync
    if (reachforremote(plug)) { // if non 0 -> success!

       // put two threads (this + 1 other) on the I/O stream/message conversions
       pthread_create(&plug->stream_shipper, &pthread_attributes,
                                 &stream_shipping, (void *)plug);

       pthread_create(&plug->stream_receiver, &pthread_attributes,
                                &stream_receiving, (void *)plug);

       // remote connection needs stderr forwarder
       pthread_create(&plug->stderr_forwarder, &pthread_attributes,
                                 &forward_raw_errors, (void *)plug);

       return 1;
    }

    return 0; // failed to reach
} // recruitworker

void disconnectplug(int32 msg_type, int32 msg_src, char* msg_data, int32 success)
{ // implements the message_callback_function interface
  // success means we got a goodbye message from the device

    int32 plugnumber = msg_src;
    connection plug = remove_connection(plugnumber);

    if (!success)
        printerr("Warning: Disconnecting unresponsive device on plug %d", plugnumber);

    // it it nearly impossible to make threads involved with blocking I/O exit in some sane
    // way. We use a signal that will make them exit on the spot with no way for
    // the threads to clean up after themselves. We need this because most of the
    // stuff our threads do is block on some streams. The thing we have to worry
    // about are memory leaks. There is no problem for stream_shipping
    // because it does not allocate memory and we will be able to free any left
    // message later on. The same is true for forward_raw_errors. The stream reciever,
    // has a point of failure which would leak memory (while it is filling up a new
    // message). To avoid leaking this message, there is a field on the plug called
    // plug->unprocessed_message which stores a reference to such a message. It
    // can thus also be freed. The worst case is cancelling a local worker thread.
    // In this case there will be a memory leak because of the way McSync is
    // currently set up.

    // the best solution for all of this would be a signal on the plug which would
    // be polled by all involved parties instead of blocking, letting the threads
    // decide themselves when to exit and what to clean up. The only reason this
    // can't be implemented at this time, is that it involed changing vast parts
    // of the I/O functions (put32, waitforsequence et al) to support non blocking
    // file streams and to check the field. For the regular connecting / disconnecting
    // the current structure will 'leak' 8kb of unflushed buffer data and it should
    // be fixed at a later point in time.

    if (!success && plug->listener != NULL) { // only local plugs have the listener set
        pthread_kill(plug->listener, SIGUSR1); // will leak memory...
    }

    if(plug->stream_shipper != NULL) { // a remote connection
        pthread_kill(plug->stream_shipper, SIGUSR1);
        pthread_kill(plug->stream_receiver, SIGUSR1);
        pthread_kill(plug->stderr_forwarder, SIGUSR1);

        kill(plug->processpid, SIGKILL);
        close(plug->kidinpipe[WRITE_END]); // does not flush an hogs the unbuffered data...
        close(plug->kidoutpipe[READ_END]);
        close(plug->kiderrpipe[READ_END]);
    }

    freeconnection(plug);

    sendplugnumber(recruiter_plug, hq_int, msgtype_removeplugplease, plugnumber);
} // disconnectplug

void reqruitermain(void)
{
    int32 msg_src;
    int64 msg_type;
    char* msg_data;

    recruitercallbacks = initqueue();

    while (1) {
        while (! receivemessage(recruiter_plug, &msg_src, &msg_type, &msg_data)) {
            if (recruitercallbacks->head != NULL)
                callbacktick(recruitercallbacks, pollingrate);
            usleep(pollingrate);
        }

        // we got a message

        // is someone waiting for this message ... ?
        if (recruitercallbacks->head != NULL) {
            if (messagearrived(recruitercallbacks, msg_type, msg_src, msg_data)) {
                // ... yes, steal it and do not execute the regular actions described
                // in the switch statement down bellow!
                free(msg_data);
                continue;
            }
        }
        // ... no one is waiting, handle the message in the regular way
        switch (msg_type) {
            case msgtype_info:
                    printerr("recruiter got info message: \"%s\" from %d\n",
                                    msg_data, msg_src);
                    break;
            case msgtype_newplugplease: // msg_data is the reference we should
                                        // send back toghether with a new plug number
                add_connection(NULL, next_free_address);
                sendnewplugresponse(msg_src, msg_data, next_free_address);
                next_free_address++;
                break;
            case msgtype_recruitworker:
            {
                char *address;
                int32 plugnumber;
                receiverecruitcommand(msg_data, &plugnumber, &address);
                if (! recruitworker(plugnumber, address))
                    sendplugnumber(recruiter_plug, msg_src, msgtype_failedrecruit, plugnumber);
                free(address);
            }
            break;
            case msgtype_removeplugplease: // msg_data is the number of the plug we should remove
            {
                int32 plugnumber;
                receiveplugnumber(msg_data, &plugnumber);
                // we first try to reach the worker and if no response is given after
                // some time we will be a little harsh and just disconnect or kill it.
                // hopefully remote McSyncs will then eventually get a broken pipe signal
                // and if it is a local stuck thread we will leak memory...
                sendmessage(recruiter_plug, plugnumber, msgtype_exit, "");
                waitformessage(recruitercallbacks, msgtype_goodbye, plugnumber, 1000000,
                               &disconnectplug);
            }
            break;
            case msgtype_goodbye: // we got a goodbye without asking to the worker to exit,
                                  // disconnect plug immediately
                disconnectplug(msgtype_goodbye, msg_src, msg_data, 1);
            break;
            case msgtype_exit:
                cleanexit(__LINE__);
            break;
            default:
                    printerr("worker got unexpected message"
                                    " of type %lld from %d: \"%s\"\n",
                                    msg_type, msg_src, msg_data);
        }

        free(msg_data);
    }
} // reqruitermain
