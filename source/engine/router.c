/*
    communication.c

This handles the interprocess communication.
Also, the main loop of McSync is here: routermain().

Connections to remote machines are set up by recruiter.c, not here, but once it is
set up, the streams connecting the machines are read from and written to here.

McSync starts up remote processes on other machines and maintains a communication
network between these processes.  The communication network takes the form of a
tree, rooted at the initial process, which is where the CMD is.  A different
machine (perhaps one with better bandwidth) may be chosen to run HQ.  CMD has
responsibility for setting up the network.


                    ,------- PO ------- PO --------- PO
                   /          \         / \           \
                  /           WKR     WKR  \          WKR
                 /                          \
      PO ----- PO -------- PO                `---- PO
      /        /\           \                       \
    CMD      HQ  WKR        WKR                     WKR


  (up the tree) SR ---- plug ----.    ,---- plug ---- WKR 5 (hard disk)
  (down to 3,4) SR ---- plug ---- PO 2 ---- plug ---- WKR 6 (usb)
    (down to 7) SR ---- plug ----'    `---- plug ---- maybe HQ and/or CMD

Note that both sides of a plug use the same plug struct.
The two threads "own" different parts of the struct and its queues.

When McSync is invoked on a machine, main() transfers control to the router (PO),
which sets up some plugs and then starts routing among them.

A plug is a struct with various parts: some parts are used by the recruiter
while setting up the remote connection before communication can start, some
parts are the queues used by the communicating parties, some parts help the
router tell whether this is the plug a message is looking for.  Think of it
as a pair of cubbyholes in the router room, made out of wood.  This wooden
object has installation and kid-making instructions written on the top, holes
for transferring stuff, and an address (or many) for reference during use.
The kid (on the other side of the cubbyhole wall) takes things out of the
outbox and puts things in the inbox.  Plugs for communicating with remote
places have stream cables plugged in, which the kid uses.

One of the communicating parties is always the router, and the other is called
the kid because the router spawns all the other threads it communicates with.

The master McSync PO spawns a CMD and a WKR.
Later, it might set up a connection to a remote device, REMOTE (SR).
Each slave PO spawns a PARENT (SR) and a WKR.
Any PO might spawn a RCTR, and if that succeeds then it turns into a REMOTE.
Any PO might spawn a HQ, if no other PO has one.
An HQ can be shut down if it has no ongoing activities to manage.
CMD, HQ, RCTR, and WKR are local kids.
PARENT and REMOTE use multiple threads to handle streams connecting machines.
   PARENT is run on a slave for communicating with the higher parts of the tree.
   REMOTE is for communicating down a particular branch of the tree.

List of agents:
PO - post office (router)
CMD - user interface
WKR - worker (does scans, reads/writes histories, executes instructions)
HQ - headquarters (receives scans, acts as database for CMD,
                   sends identification, gossip, and instructions)
RCTR - recruiter (attempts to reach remote machines)
SR - shipping/receiving (thin agents that just forward
                         everything to/from some streams) (REMOTE, PARENT)

The agents communicate only with messages.
(There are exceptions right now, where CMD & HQ share a virtual tree struct, but
that should get fixed one of these days.)
The PO takes care of making sure every message reaches all its destinations,
whether local or remote.

A message has a sender (->source) and one or more receivers (->destinations).
It has a type (->type), which is one of a set of predefined constants
(messagetype_connectdevice, etc.).
Its body is simply some number of bytes (->len, ->data).

Each type of message is used for a particular purpose and implies a particular
data format for the contents of the message.

CONNECTIVITY

Conversation A --- Finding and connecting to a new device.

new version:
::: USER to CMD ::: please connect from machine X to machine Y
::: CMD to X ::: please connect to machine Y with pob N
::: X creates RCH ::: tries to connect
::: new main to RCH ::: I'm here!
::: RCH to new main ::: here's your plug address
::: RCH to CMD ::: sorry, the connection for N couldn't be established
(at least one of the previous two messages takes place)
::: X to network ::: I'm here!  My pob (router address) is N.
::: X to CMD ::: I'm here!  I'm number N.
::: X to CMD ::: I found device D.  Its address is M.


::: CMD to HQ ::: please connect to device d
::: HQ to RCTR ::: please connect to device d
::: RCTR to HQ ::: here's a plug I created for device d
::: HQ to RCTR ::: please use this plug and try to reach this network address
::: main to RCTR ::: I'm here!
::: RCTR to main ::: here's your plug address and the network address we used
::: RCTR to HQ ::: sorry, the connection couldn't be established
(at least one of the previous two messages takes place)
::: WKR to HQ ::: I'm here!  Here's my PO address.
::: HQ to WKR ::: I think you're device d.  Are you?
::: WKR to HQ ::: You think I'm d.  I think I'm e.
::: HQ to CMD ::: I just marked device d as connected.

User presses 'c', tui calls try_to_connect().
This sends (CMD->HQ, msgtype_connectdevice, deviceid) if device is not connected.
## It should give a PO an address to try to connect to, rather than HQ a deviceid.
## Of course tui is helping user reach a given device, but other code doesn't care.
HQ does its own sanity checks, and if the device is not connected (routeraddr==-1)
then it sends (HQ->RCTR, msgtype_newplugplease, deviceid).  Otherwise, if the
## You may wonder, which RCTR... the one local to HQ!
device is connected (which it can't be), it calls hq_reachfor(device struct).
## Maybe it could be if a connection attempt is already in progress.  Sounds bad.
When RCTR receives (HQ->RCTR, msgtype_newplugplease, deviceid), it calls
add_connection(next_free_pob)
and then sendnewplugresponse(deviceid, next_free_pob)
which sends (RCTR->HQ, msgtype_newplugplease)
## CMD should keep track of next_free_pob, not the recruiter which is on some
## remote subtree of the network and doesn't have a clue what address to use.
## It would be good to have two separate message types for the two messages.
If HQ receives (RCTR->HQ, msgtype_newplugplease, deviceid, plugnumber)
then it sets routeraddr and calls hq_reachfor(the device).
## It is too early to set routeraddr -- that would mean messages can be sent.
If the device is unknown, it drops the ball.
Then hq_reachfor() calls sendrecruitcommand(routeraddr, whichtouse)
which sends (HQ->RCTR, msgtype_recruitworker, routeraddr, whichtouse)
and sets the device to status_reaching.
## Only CMD should care about detailed status.
## HQ only needs to know what devices are currently connected.


#define msgtype_recruitworker       5   HQ->RCTR    asks for child to be raised
#define slave_start_string  non-message main->RCTR  sent at startup in slave mode
#define hi_slave_string     non-message RCTR->main  response with plugid + address
#define msgtype_failedrecruit       6   RCTR->HQ    sent if child is being killed
## Here it would be better for child to confirm validity with recruiter first.
## That way, recruiter is single point of success/failure, and child won't try to
## talk to HQ if recruiter gave up on it and issued orders for it to be killed.
## Recruiter should send it a "you're in" signal from its main loop thread.
#define msgtype_workerisup          8   WKR->HQ     sent at startup in slave mode
#define msgtype_identifydevice      11  HQ->WKR     first thing HQ wants is dev id
#define msgtype_deviceid            12  WKR->HQ     triggers identifydevice(),
#define msgtype_connected           9   HQ->CMD     which sends this


Conversation B --- Disconnecting from a device.

            CMD: user presses 'x' = disconnect
#define msgtype_disconnectdevice    2   CMD->HQ (device id)
            HQ: checks that id exists and is connected, sets status to reachingXXX
#define msgtype_removeplugplease    4   HQ->RCTR (device pob)  (also sent to RCTR in other circumstances)
            RCTR: asks device to exit, calls disconnectplug when goodbye comes (or max 1 second)
#define msgtype_exit                16  RCTR->DEV ()
            DEV: exits loop, ignoring remaining messages, sends goodbye, sleeps 1 second, exits thread
#define msgtype_goodbye             17  DEV->RCTR ()
            RCTR: message eaten by callback which calls disconnect plug (also called if no callback)
            RCTR: disconnectplug: calls remove_connection, if goodbye not received kills plug->listener
                    for remote connection, kills shipper/receiver/forwarder threads and child process
                                            and closes 3 pipes
#define msgtype_removeplugplease    4   RCTR->HQ (device pob, but called plugnumber)
            HQ: finds device having that plugnumber (bug) and sets its routeraddr to -1
                sets the status of that device to inactive, setstatus sends message
#define msgtype_disconnected        10  HQ->CMD (device id)
            CMD: refreshes the display to show disconnected state

SCANS

Conversation C --- Doing a scan.

#define msgtype_scanvirtualdir      13  CMD->HQ
#define msgtype_scan                14  HQ->WKR
#define msgtype_scandone            15
#define msgtype_scanupdate          18
#define msgtype_scanloaded          19

DEBUGGING

#define msgtype_info                7   receiver prints out who they are


*/

#include "definitions.h"

connection cmd_plug, hq_plug, recruiter_plug, machine_worker_plug, parent_plug;

// The new_plug function needs to stop the main routing in order to add new
// connection to the list
static pthread_mutex_t connections_mutex = PTHREAD_MUTEX_INITIALIZER;

const char* msgtypelist[] = {
    "error (msgtype==0)",
    "connectdevice","disconnectdevice",
    "newplugplease", "removeplugplease",
    "recruitworker", "failedrecruit",
    "info",
    "workerisup", "connected", "disconnect",
    "identifydevice", "deviceid",
    "scanvirtualdir", "scan", "scandone",
    "exit", "goodbye",
    "scanupdate", "scanloaded" };


//////////////////////////////////////////////////////////////////////////////////
///////////////////////////// start of serialization /////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

// needed for endianness handling
#if !defined(__GNUC__) || __GNUC__ < 43
#define swap32(x) \
       (((x << 24) & 0xff000000) | \
        ((x <<  8) & 0x00ff0000) | \
        ((x >>  8) & 0x0000ff00) | \
        ((x >> 24) & 0x000000ff))

#define swap64(x) \
       (((x << 56) & 0xff00000000000000ULL) | \
        ((x << 40) & 0x00ff000000000000ULL) | \
        ((x << 24) & 0x0000ff0000000000ULL) | \
        ((x <<  8) & 0x000000ff00000000ULL) | \
        ((x >>  8) & 0x00000000ff000000ULL) | \
        ((x >> 24) & 0x0000000000ff0000ULL) | \
        ((x >> 40) & 0x000000000000ff00ULL) | \
        ((x >> 56) & 0x00000000000000ffULL))
#else
#define swap32(x) ((int32) __builtin_bswap32(x)) // use efficient asm instructions
#define swap64(x) ((int64) __builtin_bswap64(x))
#endif

#ifndef BIG_ENDIAN_MACHINE // provided by compile.sh
#define stream_to_host_32(x) (x) // stream data is always little endian
#define stream_to_host_64(x) (x)
#define host_to_stream_32(x) (x)
#define host_to_stream_64(x) (x)
#else
#define stream_to_host_32(x) (swap32(x))
#define stream_to_host_64(x) (swap64(x))
#define host_to_stream_32(x) (swap32(x))
#define host_to_stream_64(x) (swap64(x))
#endif
// end of endianness

#define size_of_int32  4 // ensured by checktypes(void) in main.c
#define size_of_int64  8

int32 deserializeint32(char **source)
{
    int32 n;
    memcpy(&n, *source, size_of_int32);
    *source += size_of_int32;
    return stream_to_host_32(n);
} // deserializeint32

int64 deserializeint64(char **source)
{
    int64 n;
    memcpy(&n, *source, size_of_int64);
    *source += size_of_int64;
    return stream_to_host_64(n);
} // deserializeint64

char *deserializestring(char **source) // may allocate string, free when done
{
    char *str = NULL;
    int32 len = deserializeint32(source); // first 4 bytes are length of string

    if (len) {
        str = (char*) malloc(len);
        memcpy(str, *source, len);
    }
    *source += len;
    return str;
} // deserializestring

stringlist *deserializestringlist(char **source) // free when done
{ // *source is manipulated by each deserialization function
    stringlist *head, *tail, *temp;
    int32 count = deserializeint32(source);

    head = tail = NULL;
    while (count--) {
        temp = (stringlist*) malloc(sizeof(stringlist));
        temp->string = deserializestring(source);
        temp->next = NULL;
        if (!head)
            head = tail = temp;
        else {
            tail->next = temp;
            tail = temp;
        }
    }
    return head;
} // deserializestringlist

void serializeint32(bytestream b, int32 n)
{
    n = host_to_stream_32(n);
    bytestreaminsert(b, (void*) &n, size_of_int32);
} // serializeint32

void serializeint64(bytestream b, int64 n)
{
    n = host_to_stream_64(n);
    bytestreaminsert(b, (void*) &n, size_of_int64);
} // serializeint32

void serializestring(bytestream b, char *str)  // prepends the size of the string,
                                       // the empty string has size 1, NULL size 0
{
    int32 len = str ? strlen(str) + 1 : 0;
    serializeint32(b, len);
    if (len)
        bytestreaminsert(b, (void*) str, len);
} // serializestring

void serializestringlist(bytestream b, stringlist *list) // prepends the number of
            // strings that have been serialized, list == NULL => sends count == 0
{
    int32 count = 0;
    stringlist *listitem;
    // prepend count
    for (listitem = list; listitem != NULL; listitem = listitem->next)
        count++;
    serializeint32(b, count);

    for (listitem = list; listitem != NULL; listitem = listitem->next)
        serializestring(b, listitem->string);
} // serializestringlist

//////////////////////////////////////////////////////////////////////////////////
////////////////////////////// end of serialization //////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
///////////////////////// start of agent helper functions ////////////////////////
//////////////////////////////////////////////////////////////////////////////////

// Sometimes an agent can't properly finish a task without having some message
// from another agent. This is for example the case for the exit message which
// gives the other agent some time to react to it before it is disconnected. We
// can't really block, because then we would not be able to receive the message
// and jump back to the blocking function. The solution to the problem is callback
// functions. Every time the agent looks for a message, it will also remove some
// waiting time from the callback requests. If the message arrives on time the
// callback function will be called with the success bit on. If it times out
// before the message arrives, this bit will be off. The following functions allow
// an agent to implement this callback functionality if needed.

// adds a callback request to the queue, the call back function has to be of type
// void (*fn) (int32 msg_type, int32 msg_src, char* msg_data, int32 success);
void waitformessage(queue callbackqueue, int32 msg_type, int32 msg_src,
                    int32 timeout, message_callback_function fn)
{
    message_callback callback =
        (message_callback) malloc(sizeof(struct message_callback_struct));
    callback->msg_type = msg_type;
    callback->msg_src = msg_src;
    callback->timeout = timeout;
    callback->fn = fn;
    queueinserttail(callbackqueue, (void*) callback);

} // waitformessage

// is called in the message receiving loop of the agent, ticks the waiting time.
// If a request times out, the callback function is invoked with the success bit
// off.
void callbacktick(queue callbackqueue, int32 microseconds)
{
    queuenode node, tmp;
    node = callbackqueue->head;

    message_callback callback;

    while (node != NULL) {
        callback = (message_callback) node->data;
        callback->timeout -= microseconds;
        if (callback->timeout <= 0) {
            tmp = node->next;
            queueremove(callbackqueue, node);
            callback->fn(callback->msg_type, callback->msg_src, NULL, 0); // 0=T/O
            free(callback);
            node = tmp;
            continue;
        }
        node = node->next;
    }
} // callbacktick

// This is called every time a message has arrived and checks if it is one of the
// messages the agent has been waiting for. In this case it calls the function
// provided with the success bit on.
int32 messagearrived(queue callbackqueue, int32 msg_type, int32 msg_src,
                     char *msg_data)
{
    queuenode node = callbackqueue->head;
    message_callback callback;

    while (node != NULL) {
        callback = (message_callback) node->data;
        if (callback->msg_type == msg_type && callback->msg_src == msg_src) {
            queueremove(callbackqueue, node);
            callback->fn(callback->msg_type, callback->msg_src, msg_data, 1);
            free(callback);
            return 1;
        }
        node = node->next;
    }
    return 0;
}   // messagearrived

//////////////////////////////////////////////////////////////////////////////////
///////////////////////// end of agent helper functions //////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
/////////////////////// start of ascii-safe serialization ////////////////////////
//////////////////////////////////////////////////////////////////////////////////

unsigned char unhex(unsigned char a, unsigned char b)
{
    //log_line("%c%c", a, b);
    if (a > '9')
        a -= 'A' - 10;
    else
        a -= '0';
    if (b > '9')
        b -= 'A' - 10;
    else
        b -= '0';
    //log_line("[%c]", (a << 4) + b);
    return (a << 4) + b;
} // unhex

void tohex(unsigned char c, char* a, char* b)
{
    char aa, bb;
    aa = (c >> 4) & 0x0F;
    bb = c & 0x0F;
    if (aa > 9)
        aa += 'A' - 10;
    else
        aa += '0';
    *a = aa;
    if (bb > 9)
        bb += 'A' - 10;
    else
        bb += '0';
    *b = bb;
} // tohex

int getcsafe(FILE* input)
{
    int a, b;
    do {
        a = getc(input); // if we do this in args to unhex,
    } while (a == -1 && (usleep(1000),1));
    do {
        b = getc(input); // then evaluation order is undefined!
    } while (b == -1 && (usleep(1000),1));
    //log_line("[%c%c]", a, b);
    return unhex(a, b);
} // getcsafe

int getc_unlockedsafe(FILE* input)
{
    int a, b;
    do {
        a = getc_unlocked(input); // if we do this in args to unhex,
    } while (a == -1 && (usleep(1000),1));
    do {
        b = getc_unlocked(input); // then evaluation order is undefined!
    } while (b == -1 && (usleep(1000),1));
    return unhex(a, b);
} // getc_unlockedsafe

void putcsafe(char c, FILE* output)
{
    char a, b;
    tohex(c, &a, &b);
    putc(a, output);
    putc(b, output);
    //log_line("[%c%c]", a, b);
} // putcsafe

void putc_unlockedsafe(char c, FILE* output)
{
    char a, b;
    tohex(c, &a, &b);
    putc_unlocked(a, output);
    putc_unlocked(b, output);
} // putc_unlockedsafe

void put32(FILE* output, int32 data)
{
    putc((char) (data >> 24), output);
    putc((char) (data >> 16), output);
    putc((char) (data >>  8), output);
    putc((char) (data      ), output);
} // put32

void put32safe(FILE* output, int32 data)
{
    putcsafe((char) (data >> 24), output);
    putcsafe((char) (data >> 16), output);
    putcsafe((char) (data >>  8), output);
    putcsafe((char) (data      ), output);
} // put32safe

void put64(FILE* output, int64 data)
{
    putc((char) (data >> 56), output);
    putc((char) (data >> 48), output);
    putc((char) (data >> 40), output);
    putc((char) (data >> 32), output);
    putc((char) (data >> 24), output);
    putc((char) (data >> 16), output);
    putc((char) (data >>  8), output);
    putc((char) (data      ), output);
} // put64

void put64safe(FILE* output, int64 data)
{
    putcsafe((char) (data >> 56), output);
    putcsafe((char) (data >> 48), output);
    putcsafe((char) (data >> 40), output);
    putcsafe((char) (data >> 32), output);
    putcsafe((char) (data >> 24), output);
    putcsafe((char) (data >> 16), output);
    putcsafe((char) (data >>  8), output);
    putcsafe((char) (data      ), output);
} // put64safe

int32 get32(FILE* input)
{
    int32 data;
    data = getc(input);
    data <<= 8; data += getc(input); data <<= 8; data += getc(input);
    data <<= 8; data += getc(input);
    return data;
} // get32

int32 get32safe(FILE* input)
{
    int32 data;
    data = getcsafe(input);
    data <<= 8; data += getcsafe(input); data <<= 8; data += getcsafe(input);
    data <<= 8; data += getcsafe(input);
    return data;
} // get32safe

int64 get64(FILE* input)
{
    int64 data;
    data = getc(input);
    data <<= 8; data += getc(input); data <<= 8; data += getc(input);
    data <<= 8; data += getc(input); data <<= 8; data += getc(input);
    data <<= 8; data += getc(input); data <<= 8; data += getc(input);
    data <<= 8; data += getc(input);
    return data;
} // get64

int64 get64safe(FILE* input)
{
    int64 data;             // we choose the byte order ourself for portability
    data = getcsafe(input);
    data <<= 8; data += getcsafe(input); data <<= 8; data += getcsafe(input);
    data <<= 8; data += getcsafe(input); data <<= 8; data += getcsafe(input);
    data <<= 8; data += getcsafe(input); data <<= 8; data += getcsafe(input);
    data <<= 8; data += getcsafe(input);
    return data;
} // get64safe

void putstring(FILE* output, char* s)
{
    while (*s != 0) {
        putc(*s, output);
        s++;
    }
    putc(0, output);
} // putstring

//////////////////////////////////////////////////////////////////////////////////
//////////////////////// end of ascii-safe serialization /////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
/////////////////////////// start of waiting functions ///////////////////////////
//////////////////////////////////////////////////////////////////////////////////

char* threadname(void)
{
    pthread_t us = pthread_self();
    return  (hq_plug && us == hq_plug->listener) ? "HQ" :
            (cmd_plug && us == cmd_plug->listener) ? "CMD" :
            (parent_plug && us == parent_plug->listener) ? "parent" :
            "other thread";
} // threadname

void waitforsequence(FILE* input, char* sequence, int len, int echo)
{
    // There are always a set of possible positions, one set for each most
    // advanced position.  E.g. in maramarosmaramarmot the possibilities after
    // reading xmaramarosmaramar would be .mar.amar.osmaramar.mot and the
    // transitions would be 16-m->17, 16-o->8, 16-a->4, 16-m->1, 16-?->0.
    // Notice the other transitions 7-a->4, 3-m->1, 0-?->0, so we just need
    // the first fallback point for each position.  16 points to 7 because
    // the shortest possible period of the first 16 is 16-7=9.
    // Looking for periodicities, we have nothing until 4 sticks, then 4 goes
    // away when we see the s, then 9 sticks for a while.  Note that if the
    // shortest period is valid now, it has been valid up to now, so the shortest
    // period never gets shorter.  It can increase in size, however, as in
    // 123124.123.12 with next digit 3.  There may be more efficiencies to be had.
    int subend, period, pos;
    FILE* debug_out = ourerr;
    int shortestperiod[len]; // alloc on stack, might fail on really old compilers

    period = 1; // starting case is one char, min period 1
    shortestperiod[0] = 1; // makes it work when we don't even match first char
    shortestperiod[1] = 1; // makes sense
    for (subend = 1; subend < len; subend++) { // consider [0..subend]
        if (sequence[subend] != sequence[subend - period]) {
            // we need to look for a longer period
            trynextperiod:
            period++; // now it is just a candidate period
            for (pos = subend; pos >= period; pos--) // fails faster going down?
                if (sequence[pos - period] != sequence[pos]) // this is n^2 part
                    goto trynextperiod;
            // that loop must end by the time period gets up to subend + 1
        }
        shortestperiod[subend + 1] = period; // 123124: sp[5]=3, sp[6]=6
    }
    if (debug_out) {
        int i;
        fprintf(debug_out, "%c[0;31;47m[%s looking for \"", 27, threadname());
        for (i = 0; i < len; i++)
            fprintf(debug_out, "%c", sequence[i]); // fprintf sanitizes char
        fprintf(debug_out, "\"]%c[0;0;0m", 27);
        fflush(debug_out);
    }
    pos = 0; // how many chars have matched so far, seq[pos] should match next
    while (pos < len) { // do until we have matched full string
        int c = getc(input);
        if (c == -1) {
            usleep(1000);
            continue;
        }
        if (echo) {
            fprintf(stderr, "%c", c);
            fflush(stderr);
        }
        while (pos >= 0 && c != sequence[pos]) { // find a lesser value of pos
            // e.g. rcvd 12312412312, pos = 11, period = 6, c = 3, seq expects 5
            // echo the <period> characters that are being eliminated
            int i;
            if (debug_out) {
                for (i = 0; i < shortestperiod[pos]; i++) {
                    fprintf(debug_out, "%c[0;32;47m%c%c[0;0;0m",
                                        27, (pos == 0) ? c : sequence[i], 27);
                }
                fflush(debug_out);
            }
            pos -= shortestperiod[pos]; // next pos to try is 11 - 6 = 5
        }
        pos++; // we matched!  (maybe matched "*" at position -1)
    }
    if (echo) {
        fprintf(stderr, "\n");
        fflush(stderr);
    }
    if (debug_out) {
        int i;
        fprintf(debug_out, "%c[0;31;47m[%s got \"", 27, threadname());
        for (i = 0; i < len; i++)
            fprintf(debug_out, "%c", sequence[i]); // fprintf sanitizes char
        fprintf(debug_out, "\"]%c[0;0;0m", 27);
        fflush(debug_out);
    }
} // waitforsequence

void waitforstring(FILE* input, char* string)
{
    waitforsequence(input, string, strlen(string), 0);
} // waitforstring

void waitforcookiesafe(FILE* input)
{
    char cookie[8];
    tohex((unsigned char) (magiccookie >> 24), cookie + 0, cookie + 1);
    tohex((unsigned char) (magiccookie >> 16), cookie + 2, cookie + 3);
    tohex((unsigned char) (magiccookie >>  8), cookie + 4, cookie + 5);
    tohex((unsigned char) (magiccookie      ), cookie + 6, cookie + 7);
    waitforsequence(input, cookie, 8, 0);
} // waitforcookiesafe

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of waiting functions ////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
///////////////////////////////// start intlist //////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

void freeintlist(intlist skunk)
{
    if (skunk->values != NULL)
        free(skunk->values);
    free(skunk);
} // freeintlist

intlist emptyintlist(void)
{
    intlist il = (intlist) malloc(sizeof(struct intlist_struct));
    il->count = 0;
    il->values = NULL;
    return il;
} // emptyintlist

intlist singletonintlist(listint n)
{
    intlist il = (intlist) malloc(sizeof(struct intlist_struct));
    il->count = 1;
    il->values = (listint*) malloc(sizeof(listint));
    il->values[0] = n;
    return il;
} // singletonintlist

void addtointlist(intlist il, listint n) // keeps list sorted, works for multisets
{
    int i;
    il->count++;
    il->values = (listint*) realloc(il->values, il->count * sizeof(listint));
    for (i = 0; i < il->count - 1; i++) {
        // (array[i], n) = (min, max)(array[i], n);
        if (il->values[i] > n) {
            listint norig = n;
            n = il->values[i];
            il->values[i] = norig;
        }
    }
    il->values[i] = n; // the new array member
} // addtointlist

int intlistcontains(intlist il, listint n) // test membership
{
    int i;
    for (i = 0; i < il->count && il->values[i] <= n; i++)
        if (il->values[i] == n)
            return 1;
    return 0;
} // intlistcontains

void removefromintlist(intlist il, listint n)
{
    int i, j;
    for (i = 0; i < il->count - 1; i++) {
        if (il->values[i] == n) {
            for (j = i; j < il->count - 1; j++)
                il->values[j] = il->values[j + 1];
            break; // only remove it once!
        }
    }
    il->count--;
} // removefromintlist

int intlistsoverlap(intlist a, intlist b) // return 1 if they have any in common
{
    int ap = 0, bp = 0;
    while (ap < a->count && bp < b->count) {
        if (a->values[ap] < b->values[bp])
            ap++;
        else if (a->values[ap] > b->values[bp])
            bp++;
        else
            return 1;
    }
    return 0;
} // intlistsoverlap

void restrictintlistto(intlist a, intlist b) // shrinks a so it is a subset of b
{
    int ain = 0, aq = 0, bp = 0; // all b4 ain stays in a, we check if aq is in b
    while (aq < a->count && bp < b->count) {
        if (a->values[aq] < b->values[bp])
            aq++; // value in a is not in b, ignore it
        else if (a->values[aq] > b->values[bp])
            bp++; // value in b is not in a, ignore it
        else
            bp++, a->values[ain++] = a->values[aq++]; // works for multisets too
    }
    a->count = ain;
} // restrictintlistto

void pulloutintlist(intlist a, intlist b, intlist c) // pulls b out of a into c
// this takes members of a that are in b out of a and into c
{
    int ain = 0, aq = 0, bp = 0; // all b4 ain stays in a, we check if aq is in b
    while (aq < a->count && bp < b->count) {
        if (a->values[aq] < b->values[bp])
            a->values[ain++] = a->values[aq++]; // keep aq in a
        else if (a->values[aq] > b->values[bp])
            bp++; // value in b is not in a, ignore it
        else
            bp++, addtointlist(c, a->values[aq++]); // works for multisets too
    }
    a->count = ain;
} // pulloutintlist

void putintlist(FILE* output, intlist il)
{
    int i;
    put32(output, il->count);
    for (i = 0; i < il->count; i++)
        put32(output, il->values[i]);
} // putintlist

void putintlistsafe(FILE* output, intlist il)
{
    int i;
    put32safe(output, il->count);
    for (i = 0; i < il->count; i++)
        put32safe(output, il->values[i]);
} // putintlistsafe

intlist getintlist(FILE* input)
{
    int i;
    intlist il = emptyintlist();
    il->count = get32(input);
    il->values = (listint*) realloc(il->values, il->count * sizeof(listint));
    for (i = 0; i < il->count; i++)
        il->values[i] = get32(input);
    return il;
} // getintlist

intlist getintlistsafe(FILE* input)
{
    int i;
    intlist il = emptyintlist();
    il->count = get32safe(input);
    il->values = (listint*) malloc(il->count * sizeof(listint));
//    il->values = (listint*) realloc(il->values, il->count * sizeof(listint));
    for (i = 0; i < il->count; i++)
        il->values[i] = get32safe(input);
    return il;
} // getintlistsafe

//////////////////////////////////////////////////////////////////////////////////
////////////////////////////////// end intlist ///////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
///////////////////////////// start message handling /////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

void freemessage(message skunk)
{
    if (skunk->destinations != NULL)
        freeintlist(skunk->destinations);
    if (skunk->data != NULL)
        free(skunk->data);
    free(skunk);
} // freemessage

void freemessagequeue(message skunk)
{
    message tmp;
    while (skunk != NULL) {
        tmp = skunk;
        skunk = skunk->next;
        freemessage(tmp);
    }
} // freemessagequeue

void nsendmessage(connection plug, int recipient, int type, char* what, int len)
{
    message msg;

    msg = (message) malloc(sizeof(struct message_struct));
    assert(plug->thisway->count == 1); // we should not be remote or parent
    msg->source = plug->thisway->values[0]; // assumes we are not remote or parent
    msg->destinations = singletonintlist(recipient);
    msg->type = type;
    msg->len = len;
    msg->data = (char*) malloc(len+1);
    memcpy(msg->data, what, len);
    msg->data[len] = 0; // extra unsent 0 makes it safer for diagnostic printing
    msg->nextisready = 0;
    msg->next = NULL;
    plug->messages_fromkid_tail->next = msg;
    plug->messages_fromkid_tail->nextisready = 1;
    plug->messages_fromkid_tail = msg;
} // nsendmessage

void sendmessage(connection plug, int recipient, int type, char* what)
{
    nsendmessage(plug, recipient, type, what, strlen(what)); // don't send final 0
} // sendmessage

void sendmessage2(connection plug, int recipient, int type, char* what)
{
    int first = strlen(what);
    nsendmessage(plug, recipient, type, what,
                    first + 1 + strlen(what + first + 1)); // don't send second 0
} // sendmessage2

void sendint32(connection plug, int32 recipient, int32 type, int32 n)
{
    bytestream serialized = initbytestream(4);

    serializeint32(serialized, n);
    nsendmessage(plug, recipient, type, serialized->data, serialized->len);
    freebytestream(serialized);
} // sendint32

char* secondstring(char* string)
{
    char* pos = string;
    while (*pos != 0)
        pos++;
    return ++pos;
} // secondstring

int receivemessage(connection plug, listint* src, int64* type, char** data)
// returns 0 if no messages waiting, else please free data when done
{
    message msg, head;
    char* buf;

    head = plug->messages_tokid_head;
    if (head->nextisready == 0) {
        return 0;
    }
    msg = head->next;
    *src = msg->source;
    *type = msg->type;
    buf = (char*) malloc(msg->len + 1);
    memcpy(buf, msg->data, msg->len + 1);
    *data = buf;
    plug->messages_tokid_head = msg;
    freemessage(head);
    return 1;
} // receivemessage

void receiveint32(char *source, int32 *n)
{
    *n = deserializeint32(&source);
} // receiveint32

//////////////////////////////////////////////////////////////////////////////////
////////////////////////////// end message handling //////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start specific messages /////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

////////// sent from CMD to HQ

void sendscanvirtualdirrequest(virtualnode *root, virtualnode *node)
{
    bytestream b = initbytestream(128);
    getvirtualnodepath(b, root, node);
    bytestreaminsertchar(b, '\0');
    nsendmessage(cmd_plug, hq_int, msgtype_scanvirtualdir, b->data, b->len);
    freebytestream(b);
} // sendscanvirtualdirrequest

////////// sent from HQ to WKR

void sendscancommand(connection plug, int recipient, char *scanroot,
                     char *virtualscanroot, stringlist *prunepoints)
{
    bytestream serialized = initbytestream(128);

    serializestring(serialized, scanroot);
    serializestring(serialized, virtualscanroot);
    serializestringlist(serialized, prunepoints);
    nsendmessage(plug, recipient, msgtype_scan,
                 serialized->data, serialized->len);
    freebytestream(serialized);
} // sendscancommand

void receivescancommand(char *source, char **scanroot, char **virtualscanroot,
                        stringlist **prunepoints)
{
    *scanroot = deserializestring(&source);
    *virtualscanroot = deserializestring(&source);
    *prunepoints = deserializestringlist(&source);
} // receivescancommand

////////// sent from HQ to recruiter

void sendrecruitcommand(connection plug, int32 plugnumber, char *address)
{
    bytestream serialized = initbytestream(64);

    serializeint32(serialized, plugnumber);
    serializestring(serialized, address);
    nsendmessage(plug, recruiter_int, msgtype_recruitworker,
                 serialized->data, serialized->len);
    freebytestream(serialized);
} // sendrecruitcommand

void receiverecruitcommand(char *source, int32 *plugnumber, char **address)
{
    *plugnumber = deserializeint32(&source);
    *address = deserializestring(&source);
} // receiverecruitcommand

////////// sent from WKR to HQ

void sendscandonemessage(connection plug, char *virtualscanroot,
                         char *scanfilepath, char *historyfilepath)
{
    bytestream serialized = initbytestream(256);

    serializestring(serialized, virtualscanroot);
    serializestring(serialized, scanfilepath);
    serializestring(serialized, historyfilepath);
    nsendmessage(plug, hq_int, msgtype_scandone,
                 serialized->data, serialized->len);
    freebytestream(serialized);
} // sendscandonemessage

void receivescandonemessage(char *source, char **virtualscanroot,
                            char **scanfilepath, char **historyfilepath)
{
    *virtualscanroot = deserializestring(&source);
    *scanfilepath = deserializestring(&source);
    *historyfilepath = deserializestring(&source);
} // receivescandonemessage

////////// sent from recruiter to HQ

void sendnewplugresponse(int32 recipient, char *theirreference, int32 plugnumber)
{
    bytestream serialized = initbytestream(20);

    serializestring(serialized, theirreference);
    serializeint32(serialized, plugnumber);
    nsendmessage(recruiter_plug, recipient, msgtype_newplugplease,
                 serialized->data, serialized->len);
    freebytestream(serialized);
} // sendnewplugresponse

void receivenewplugresponse(char *source, char **reference, int32 *plugnumber)
{
    *reference = deserializestring(&source);
    *plugnumber = deserializeint32(&source);
} // receivenewplugresponse

//////////////////////////////////////////////////////////////////////////////////
///////////////////////////// end specific messages //////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
////////////////////////// start of shipping & receiving /////////////////////////
//////////////////////////////////////////////////////////////////////////////////

void abort_thread_execution(int sig, siginfo_t *info, void *ucontext)
{
    pthread_exit(NULL);
}   // abort_thread_execution

void addabortsignallistener(void)
{
    struct sigaction sa;
    sa.sa_handler = NULL;
    sa.sa_sigaction = &abort_thread_execution;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGUSR1, &sa, NULL) < 0) {
             log_line("Error: Thread could not set the signal handler used for "
                      "aborting its execution. It will not be executed.\n");
             return;
     }
} // addabortsignallistener

void* forward_raw_errors(void* voidplug)
{
    FILE* stream_in;
    connection plug = voidplug; // so compiler knows type

    addabortsignallistener();

    stream_in = plug->errfromkid;
    while (1) {
        int c;
        c = getc(stream_in);
        if (c != EOF) {
            if (c == '\n')
                log_line(" (from %d)", plug->thisway->values[0]);
            log_line("%c", (char)c);
        }
    }
} // forward_raw_errors

void* stream_receiving(void* voidplug)
{
    FILE* stream_in;
    connection plug = voidplug; // so compiler knows type

    addabortsignallistener();

    stream_in = plug->fromkid;
    while (1) {
        int64 len, pos;
        message *msg = &plug->unprocessed_message;
        (*msg) = (message) malloc(sizeof(struct message_struct));
        // read message from stream
        flockfile(stream_in);
        // we start each message with a cookie because then if things get mixed
        // up for any reason, they are more likely to get sorted out, and we are
        // less likely to try to malloc petabytes of memory for nothing
        waitforcookiesafe(stream_in);
        (*msg)->source = get32safe(stream_in);
        (*msg)->destinations = getintlistsafe(stream_in);
        (*msg)->type = get64safe(stream_in);
        (*msg)->len = len = get64safe(stream_in);
        (*msg)->data = (char*) malloc(len+1); // users can ignore the extra byte
        for (pos = 0; pos < len; pos++) {
            (*msg)->data[pos] = getc_unlockedsafe(stream_in);
        }
        (*msg)->data[pos] = 0; // we make sure a 0 follows the chars, so if you
            // know it is a short char sequence, you can treat it as a string
        (*msg)->nextisready = 0;
        funlockfile(stream_in);
        // now it is ready to be added to the queue
        (*msg)->next = NULL;
        plug->messages_fromkid_tail->next = (*msg);
        // BUG -- need barrier instruction to make sure all writes have completed!
        plug->messages_fromkid_tail->nextisready = 1;
        // we have now added this message
        plug->messages_fromkid_tail = (*msg);
        plug->unprocessed_message = NULL;
    }
} // stream_receiving

void* stream_shipping(void* voidplug)
{
    connection plug = voidplug; // so compiler knows type
    FILE* stream_out;
    message msg; // head is an already-processed message, we process the next one
    stream_out = plug->tokid;

    addabortsignallistener();

    while (1) {
        if (plug->messages_tokid_head->nextisready == 1) { // is next one ready?
            int64 len, pos;
            msg = plug->messages_tokid_head->next;
            // send message to stream
            flockfile(stream_out);
            put32safe(stream_out, magiccookie);
            put32safe(stream_out, msg->source);
            putintlistsafe(stream_out, msg->destinations);
            put64safe(stream_out, msg->type);
            put64safe(stream_out, msg->len);
            len = msg->len;
            for (pos = 0; pos < len; pos++) {
                putc_unlockedsafe(msg->data[pos], stream_out);
            }
            funlockfile(stream_out);
            fflush(stream_out);
            // now we are done with this message
            freemessage(plug->messages_tokid_head); // no other thread uses head
                // so it is ok that it points to garbage between these two lines
            plug->messages_tokid_head = msg;
        } else {
            usleep(1000);
        }
    }
} // stream_shipping

//////////////////////////////////////////////////////////////////////////////////
/////////////////////////// end of shipping & receiving //////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////// start of router /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

// Our strategy is that there is a router managing communication of messages
// between a bunch of independent processes (threads).
// Each process has 3 queues, of input, output, and error messages.
// The router shuffles the messages around based on their destination.
// Remote processes use local threads to convert between messages and stream I/O.

message trivial_message_queue(void)
{
    message tr = (message)malloc(sizeof(struct message_struct));
    tr->destinations = NULL; // so we know not to try to free it
    tr->data = NULL;         // so we know not to try to free it
    tr->next = NULL;
    tr->nextisready = 0;
    tr->source = 0;
    tr->type = 0;
    return tr;
} // trivial_message_queue

connection new_connection(void)
{
    connection plug = (connection) malloc(sizeof(struct connection_struct));
    plug->messages_tokid_head =
    plug->messages_tokid_tail = trivial_message_queue();
    plug->messages_fromkid_head =
    plug->messages_fromkid_tail = trivial_message_queue();
    return plug;
} // new_connection

void freesshsession(ssh_session *skunk)
{
    if (skunk->uname)
        free(skunk->uname);

    if (skunk->mname)
        free(skunk->mname);

    if (skunk->path) {
        remove(skunk->path);
        free(skunk->path);
    }
} // freesshsession

void freeconnection(connection skunk) {

    freeintlist(skunk->thisway);
    if (skunk->address)
        free(skunk->address);

    // clear the message queues
    freemessagequeue(skunk->messages_fromkid_head);
    freemessagequeue(skunk->messages_tokid_head);

    if (skunk->unprocessed_message != NULL)
        free(skunk->unprocessed_message); // XXXXXXXXXXXXXX why not freemessage?

    freesshsession(&skunk->session);

    free(skunk);
} // freeconnection


connection connection_list = NULL;

connection findconnectionbyplugnumber(int32 plugnumber) // XXXXXXXXXXXXXXX has to go!
{
    connection plug;

    pthread_mutex_lock(&connections_mutex);
    for (plug = connection_list; plug != NULL; plug = plug->next)
        if (plug->thisway->values[0] == plugnumber)
            break;
    pthread_mutex_unlock(&connections_mutex);
    return plug;
} // findconnectionbyplugnumber

// adds a connection to connection_lists. Waits until routermain stops being busy
// and then blocks access to the list it until the new connection is added
connection add_connection(int address) // address==0 for parent plug (has no addr)
{
    connection plug = new_connection();

    plug->listener = NULL;
    plug->stream_shipper = NULL;
    plug->stream_receiver = NULL;
    plug->stderr_forwarder = NULL;
    plug->tokid = NULL;
    plug->fromkid = NULL;
    plug->errfromkid = NULL;
    plug->unprocessed_message = NULL;
    plug->disconnecting = 0;
    plug->session.uname = NULL;
    plug->session.mname = NULL;
    plug->session.path = NULL;

    if (address) { // normal case (any plug but the parent plug)
        plug->thisway = singletonintlist(address);
        // add to front of list
        pthread_mutex_lock(&connections_mutex);
        plug->next = connection_list;
        memorybarrier(&plug->next, &connection_list);
        connection_list = plug;
        pthread_mutex_unlock(&connections_mutex);
    } else {
        // it is parent (compiler might mis-optimize parent_plug)
        plug->thisway = emptyintlist(); // parent doesn't list destinations
        // add parent to end of list (it will always stay at end)
        pthread_mutex_lock(&connections_mutex);
        connection* final = &connection_list;
        while ((*final) != NULL)
            final = &((*final)->next);
        plug->next = NULL;
        memorybarrier(&plug->next, final);
        *final = plug;
        pthread_mutex_unlock(&connections_mutex);
    }
    return plug; // caller might want it
} // add_connection


// Removes a connection from connection_lists.  Waits until routermain stops being
// busy and blocks accces to the list until the given connection has been removed.
connection remove_connection(int32 plugnumber)
{
    connection plug;
    connection previous = NULL;

    pthread_mutex_lock(&connections_mutex);

    plug = connection_list;
    while (plug != NULL) {
        if (0) // should look for plugnumber but plugs don't have numbers
            break;
        previous = plug;
        plug = plug->next;
    }
    if (!plug) {
        log_line("Error: Post office got connection remove request with unused "
                 "plug number [%d]", plugnumber);

        pthread_mutex_unlock(&connections_mutex);
        return NULL;
    }
    if (!previous)
        connection_list = plug->next;
    else
        previous->next = plug->next;

    pthread_mutex_unlock(&connections_mutex);

    return plug;
} // remove_connection

// main() transfers control to routermain()
void routermain(int32 plug_id)
// plug_id:  our machine's routing address, 2 if master, higher if slave
{
    // stdin/stdout are used by (master ? TUI : parent)

    intlist cream = emptyintlist(); // cream is (almost) always empty!
                // it is the sublist of dests to skim off for the current plug

    readspecsfile(specs_file_path); // even slaves need the list of devices

    pthread_attr_init(&pthread_attributes);
    pthread_attr_setdetachstate(&pthread_attributes, PTHREAD_CREATE_DETACHED);

    // set up the basic set of channels
    if (plug_id == 2) { // master machine
        //hq_plug = add_connection(hq_int);
        cmd_plug = add_connection(cmd_pob);
        //recruiter_plug = add_connection(recruiter_int);

        //channel_launch(hq_plug, &headquarters_main);
        channel_launch(cmd_plug, &cmd_main);
        //channel_launch(recruiter_plug, &recruiter_main);
        machine_worker_plug = add_connection(plug_id);
        channel_launch(machine_worker_plug, &machineworker_main);
    } else { // slave machine
        connection machineworkerplug;
        parent_plug = add_connection(0);
        machine_worker_plug = add_connection(plug_id);
        channel_launch(parent_plug, &parent_main );
        channel_launch(machineworkerplug, &machineworker_main);
    }

    // now start routing
    while (1) {
        int found_message_waiting = 0;
        connection source_plug, dest_plug;
        message head, msg;

        pthread_mutex_lock(&connections_mutex);
        for (source_plug = connection_list; source_plug != NULL;
                                            source_plug = source_plug->next) {
            // see if any messages are ready (process just one from each queue)
            if (source_plug->messages_fromkid_head->nextisready) {
                found_message_waiting = 1;
                head = source_plug->messages_fromkid_head;
                msg = head->next;
                source_plug->messages_fromkid_head = msg;
                freemessage(head);
                if (1) { // for anyone interested in watching the messages
                    int i;
                    log_line("Message going from %d (%s) to",
                                    msg->source,
                                    msg->source == hq_int ?       "Headquarters" :
                                    msg->source == cmd_pob ?      "Command" :
                                    msg->source == recruiter_int ?"Recruiter" :
                                                                  "Worker");
                    for (i = 0; i < msg->destinations->count; i++) {
                        log_line("%s %d",
                                        i ? "," : "",
                                        msg->destinations->values[i]);
                    }
                    if (msg->destinations->count == 1)
                        log_line(" (%s)", (i = msg->destinations->values[0], // i!
                            i == hq_int ?        "Headquarters" :
                            i == cmd_pob ?       "Command"      :
                            i == recruiter_int ? "Recruiter"    :
                                                 "Worker"         ));
                    log_line(".\n");
                    log_line("(type = %lld (%s), contents = \"%s\")\n",
                                    msg->type, msgtypelist[msg->type],
                                    msg->len > 100 ? "long message" : msg->data);
                }

                // spy on message: if workerisup then add to thisway XXXXXX should go to machine
                if (msg->type == msgtype_workerisup) {
                    // if we are final hopping point or worker device,
                    // then the plug number is already stored in thisway
                    if (! intlistcontains(source_plug->thisway, msg->source))
                        addtointlist(source_plug->thisway, msg->source);
                }

                // msg is what we want to route -- look for destinations
                for (dest_plug = connection_list; dest_plug != NULL;
                                                  dest_plug = dest_plug->next) {

                    // parent at end of loop if we have one (in slave mode)
                    if (dest_plug == parent_plug) {
                        intlist temp = cream; // move all remaining dests to cream
                        cream = msg->destinations;
                        msg->destinations = temp;
                    } else { // the normal case -- move this plug's dests to cream
                        pulloutintlist(msg->destinations,
                                        dest_plug->thisway, cream);
                    }
                    // right now, cream might not be empty -- we'll fix this soon
                    if (cream->count != 0 && dest_plug->disconnecting != 1) {

                        // should be sent to this plug
                        message newmsg;
                        newmsg = (message) malloc(sizeof(struct message_struct));
                        newmsg->source = msg->source;
                        newmsg->destinations = cream;
                        cream = emptyintlist(); // fixed!
                        newmsg->type = msg->type;
                        newmsg->len = msg->len;
                        if (msg->destinations->count == 0) { // nowhere else to go
                            newmsg->data = msg->data; // can just give it the data
                            msg->data = NULL;
                        } else {
                            // we have to copy the message data
                            newmsg->data = (char*) malloc(newmsg->len + 1);
                            memcpy(newmsg->data, msg->data, newmsg->len + 1);
                        }
                        newmsg->nextisready = 0;
                        newmsg->next = NULL;
                        dest_plug->messages_tokid_tail->next = newmsg;
                        dest_plug->messages_tokid_tail->nextisready = 1;
                        dest_plug->messages_tokid_tail = newmsg;

                        // the exit message is the last message to be delivered
                        // to a plug
                        if (msg->type == msgtype_exit) {
                            dest_plug->disconnecting = 1;
                        }
                    }
                }
                if (msg->destinations->count != 0) { // undeliverable addresses
                    // this should only happen when a message has been sent to a
                    // disconnecting device
                    int i;
                    log_line("Error! Message from %d couldn't find",
                                    msg->source);
                    for (i = 0; i < msg->destinations->count; i++) {
                        log_line("%s %d",
                                        i ? "," : "",
                                        msg->destinations->values[i]);
                    }
                    log_line(".\n");
                    log_line("(type = %lld, contents = \"%s\")\n",
                                    msg->type,
                                    msg->len > 100 ? "long message" : msg->data);
                }
            }
        }
        pthread_mutex_unlock(&connections_mutex);

        if (! found_message_waiting) { // nothing to do
            usleep(pollingrate);
        }
    }
} // routermain

//////////////////////////////////////////////////////////////////////////////////
///////////////////////////////// end of router //////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

