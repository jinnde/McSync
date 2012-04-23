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



//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of spawning ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

// It appears that upon forking the new process only gets a single thread,
// namely the one that returns 0 from fork().
// So pthread_atfork(NULL, NULL, &pthread_exit_noargs) is not only unnecessary,
// but in fact it is deadly, because everything registered by pthread_atfork()
// is executed by the forking thread, which keeps its thread_id through the fork!






//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of spawning /////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

