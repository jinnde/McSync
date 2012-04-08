#include "definitions.h"

connection TUI_plug, algo_plug, worker_plug, parent_plug; // for direct access

const char* msgtypelist[] = { "error (msgtype==0)",
    "newplugplease", "NPP1","NPP2", "info", "workerisup",
    "connected", "disconnect", "identifydevice", "deviceid",
    "scan", "lstree", "virtualnode" };


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of serialization //////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

// needed for endianess handling
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
#define stream_to_host_32(x) (x) // stream data is little endian
#define stream_to_host_64(x) (x)
#define host_to_stream_32(x) (x)
#define host_to_stream_64(x) (x)
#else
#define stream_to_host_32(x) (swap32(x))
#define stream_to_host_64(x) (swap64(x))
#define host_to_stream_32(x) (swap32(x))
#define host_to_stream_64(x) (swap64(x))
#endif
// end of endianess

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
    int32 len = deserializeint32(source); // the first byte is the length of the string

    if (len) {
        str = (char*) malloc(len);
        memcpy(str, *source, len);
    }
    *source += len;
    return str;
} // deserializestring

virtualnode *deserializevirtualnode(char *str) // returns allocated virtual node, free when done
{
    virtualnode *node = (virtualnode*) malloc(sizeof(virtualnode));
    char *pos = str;

    node->name             = deserializestring(&pos);
    node->filetype         = deserializeint32(&pos);
    node->accesstime       = deserializeint64(&pos);
    node->modificationtime = deserializeint64(&pos);
    node->permissions      = deserializeint32(&pos);
    node->numericuser      = deserializeint32(&pos);
    node->numericgroup     = deserializeint32(&pos);
    node->user             = deserializestring(&pos);
    node->group            = deserializestring(&pos);
    node->redyellow        = deserializeint32(&pos);
    node->redgreen         = deserializeint32(&pos);
    node->numchildren      = deserializeint32(&pos);
    node->subtreesize      = deserializeint32(&pos);
    node->subtreebytes     = deserializeint32(&pos);
    node->cols             = deserializeint32(&pos);
    node->firstvisiblenum  = deserializeint32(&pos);
    node->selectionnum     = deserializeint32(&pos);
    node->colwidth         = deserializeint32(&pos);
    return node;
} // deserializevirtualnode

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

void serializestring(bytestream b, char *str)
{
    int32 len = str ? strlen(str) + 1 : 0;
    serializeint32(b, len); // prepend the size of the string
    if (len)
        bytestreaminsert(b, (void*) str, len);
} // serializestring

bytestream serializevirtualnode(virtualnode *node) // returns allocated string, free when done
{
    bytestream b = initbytestream(120);

    serializestring(b, node->name);
    serializeint32(b, node->filetype);
    serializeint64(b,node->accesstime);
    serializeint64(b,node->modificationtime);
    serializeint32(b, node->permissions);
    serializeint32(b, node->numericuser);
    serializeint32(b, node->numericgroup);
    serializestring(b, node->user);
    serializestring(b, node->group);
    serializeint32(b, node->redyellow);
    serializeint32(b, node->redgreen);
    serializeint32(b, node->numchildren);
    serializeint32(b, node->subtreesize);
    serializeint32(b, node->subtreebytes);
    serializeint32(b, node->firstvisiblenum);
    serializeint32(b, node->cols);
    serializeint32(b, node->selectionnum);
    serializeint32(b, node->colwidth);

    return b;
} // serialize_virtual_tree

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of serialization ////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

unsigned char unhex(unsigned char a, unsigned char b)
{
    //printerr("%c%c", a, b);
    if (a > '9')
        a -= 'A' - 10;
    else
        a -= '0';
    if (b > '9')
        b -= 'A' - 10;
    else
        b -= '0';
    //printerr("[%c]", (a << 4) + b);
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
    //printerr("[%c%c]", a, b);
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
    //printerr("[%c%c]", a, b);
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

char* threadname(void)
{
    pthread_t us = pthread_self();
    return (worker_plug && us == worker_plug->listener) ? "worker" :
            (algo_plug && us == algo_plug->listener) ? "algo" :
            (TUI_plug && us == TUI_plug->listener) ? "TUI" :
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
        //fprintf(debug_out, "%p %p\n", input, stdin);
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
        //fprintf(debug_out, "%p %p\n", input, stdin);
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
    // fprintf(ourerr, "<waiting for \"%s\">\n", string);
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

void putstring(FILE* output, char* s)
{
    while (*s != 0) {
        putc(*s, output);
        s++;
    }
    putc(0, output);
} // putstring



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

void freemessage(message skunk)
{
    if (skunk->destinations != NULL)
        freeintlist(skunk->destinations);
    if (skunk->data != NULL)
        free(skunk->data);
    free(skunk);
} // freemessage

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

void sendvirtualnode(connection plug, int recipient, virtualnode* node)
{
    bytestream serialized = serializevirtualnode(node);
    nsendmessage(plug, recipient, msgtype_virtualnode, serialized->data, serialized->len);
    freebytestream(serialized);
} // sendvirtualnode

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



//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of router /////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

// Our strategy is that there is a router managing communication of messages
// between a bunch of independent processes (threads).
// Each process has 3 queues, of input, output, and error messages.
// The router shuffles the messages around based on their destination.
// Remote processes use local threads to convert between messages and stream I/O.

void* forward_raw_errors(void* voidplug)
{
    FILE*           stream_in;
    connection      plug = voidplug; // so compiler knows type

    stream_in = plug->errfromkid;
    while (1) {
        int c;
        c = getc(stream_in);
        if (c != EOF) {
            if (c == '\n')
                printerr(" (from %d)", plug->thisway->values[0]);
            printerr("%c", (char)c);
            // fflush(ourerr); probably slow and not necessary
        }
    }
} // forward_raw_errors

void stream_receiving(connection plug)
{
    FILE* stream_in;

    stream_in = plug->fromkid;
    while (1) {
        int64 len, pos;
        message msg;
        msg = (message) malloc(sizeof(struct message_struct));
        // read message from stream
        flockfile(stream_in);
        // we start each message with a cookie because then if things get mixed
        // up for any reason, they are more likely to get sorted out, and we are
        // less likely to try to malloc petabytes of memory for nothing
        waitforcookiesafe(stream_in);
        msg->source = get32safe(stream_in);
        msg->destinations = getintlistsafe(stream_in);
        msg->type = get64safe(stream_in);
        msg->len = len = get64safe(stream_in);
        msg->data = (char*) malloc(len+1); // users can ignore the extra byte
        for (pos = 0; pos < len; pos++) {
            msg->data[pos] = getc_unlockedsafe(stream_in);
        }
        msg->data[pos] = 0; // we make sure a 0 follows the chars, so if you
            // know it is a short char sequence, you can treat it as a string
        msg->nextisready = 0;
        funlockfile(stream_in);
        // now it is ready to be added to the queue
        plug->messages_fromkid_tail->next = msg;
        plug->messages_fromkid_tail->nextisready = 1;
        // we have now added this message
        plug->messages_fromkid_tail = msg;
    }
} // stream_receiving

void* stream_shipping(void* voidplug)
{
    connection plug = voidplug; // so compiler knows type
    FILE* stream_out;
    message msg; // head is an already-processed message, we process the next one
    stream_out = plug->tokid;

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

void* thread_main(void* voidplug) // all new plug threads start here
{
    connection      plug = voidplug; // so compiler knows type

    // TUI, algo, and worker each handle both input and output messages
    if (plug == TUI_plug) { // from routermain init
        TUImain(); // returns when user exits
        cleanexit(0); // kill all other threads and really exit
        return NULL;
    }
    if (plug == algo_plug) { // from routermain init
        algomain(); // never returns
        return NULL;
    }
    if (plug == worker_plug) { // from routermain init
        workermain(); // never returns
        return NULL;
    }
    // we need to process a parent plug or remote plug (2 sides of the same thing)
    // we use three threads (this + 2 others) to handle the three I/O streams
    if (plug == parent_plug) { // from routermain init
        plug->tokid = stdout; // "kid" is the remote thing -- here it's the parent
        plug->fromkid = stdin; // and stdout_packager will read from stdin, etc.
        // parent plug doesn't actually need a stderr handler
    } else { // a remote connection, from workermain
        if (plug->target_device == NULL) {
            printerr("Error: no target machine\n");
            return NULL;
        }
        if (reachforremote(plug)) { // fills plug with streams to remote mcsync
            // nonzero return means it failed
            return NULL;
        }
        // remote connection needs stderr forwarder
        pthread_create(&plug->stderr_forwarder, pthread_attr_default,
                        &forward_raw_errors, (void *)plug);
    }
    // put two threads (this + 1 other) on the I/O stream/message conversions
    pthread_create(&plug->stdout_packager, pthread_attr_default,
                    &stream_shipping, (void *)plug);
    stream_receiving(plug); // never returns
    return NULL; // keep compiler happy
} // thread_main

message trivial_message_queue(void)
{
    message tr = (message)malloc(sizeof(struct message_struct));
    tr->destinations = NULL; // so we know not to try to free it
    tr->data = NULL;         // so we know not to try to free it
    tr->nextisready = 0;
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

connection connection_list = NULL;

void channel_launch(connection* store_plug_here, char* deviceid, int plugnumber)
// plugnumber is 0 if called to start parent
// deviceid is NULL if called by routermain's initialization of plugs
// and non-NULL if called by a worker trying to reach another device
{
    // channel_launch is called either by the router thread at the beginning
    // of routermain (setting up the fundamental plugs, providing plugnumber),
    // or later by workermain (setting up a plug to a remote device, providing
    // deviceid).  So there is no thread conflict.
    connection plug = new_connection();

    if (store_plug_here != NULL) { // if caller wants the plug, we give it to them
        *store_plug_here = plug;
    }

    if (plugnumber) { // normal case (any plug but the parent plug)
        plug->thisway = singletonintlist(plugnumber);
        // look to see if we are the plug for a device
        if (deviceid != NULL) {
            device* m;
            for (m = devicelist; m != NULL; m = m->next) {
                if (!strcmp(m->deviceid, deviceid)) {
                    plug->target_device = m; // recover device from address
                    m->reachplan.routeraddr = plugnumber; // might already be that
                    // it only isn't if we are on a different device from algo
                    break;
                }
            }
            if (m == NULL) {
                printerr("Error: Didn't find machine (id = \"%s\")\n", deviceid);
                free(plug);
                return;
            }
        }
        // add to front of list
        plug->next = connection_list;
        connection_list = plug;
    } else {
        // it is parent (compiler might mis-optimize parent_plug)
        plug->thisway = emptyintlist(); // parent doesn't list destinations
        // add parent to end of list (it will always stay at end)
        connection* final = &connection_list;
        while ((*final) != NULL)
            final = &((*final)->next);
        plug->next = NULL;
        *final = plug;
    }

    pthread_create(&plug->listener, pthread_attr_default,
                    &thread_main, (void *)plug);
} // channel_launch

void routermain(int master, int plug_id)
// master:  0 => slave mode, 1 => master mode, 2 => batch mode, 3 => wait mode
// plug_id:  only used for slave mode
{
    // stdin/stdout are used by (master ? TUI : parent)
    intlist cream = emptyintlist(); // cream is (almost) always empty!

    readspecsfile("config/specs"); // even slaves need the list of devices

    // set up the basic set of channels
    if (master) {
        channel_launch(&algo_plug, NULL, algo_int);
        channel_launch(&TUI_plug, NULL, TUI_int);
        channel_launch(&worker_plug, NULL, topworker_int);
    } else {
        channel_launch(&parent_plug, NULL, 0); // "NULL, 0" == "parent"
        channel_launch(&worker_plug, NULL, plug_id);
    }

    // now start routing
    while (1) {
        int found_message_waiting = 0;
        connection source_plug, dest_plug;
        message head, msg;
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
                    printerr("Message going from %d (%s) to",
                                    msg->source,
                                    msg->source == algo_int ? "algo" :
                                    msg->source == TUI_int ? "TUI" :
                                    msg->source == topworker_int ? "topworker" :
                                    "remote worker");
                    for (i = 0; i < msg->destinations->count; i++) {
                        printerr("%s %d",
                                        i ? "," : "",
                                        msg->destinations->values[i]);
                    }
                    if (msg->destinations->count == 1)
                        printerr(" (%s)",
                            msg->destinations->values[0] == algo_int ? "algo" :
                            msg->destinations->values[0] == TUI_int ? "TUI" :
                            msg->destinations->values[0] == topworker_int ?
                                "topworker" : "remote worker");
                    printerr(".\n");
                    printerr("(type = %lld (%s), contents = \"%s\")\n",
                                    msg->type, msgtypelist[msg->type],
                                    msg->len > 100 ? "long message" : msg->data);
                    if (!master)
                        printerr("source_plug = %p (parent = %p, worker = %p)\n",
                                (void*)source_plug, (void*)parent_plug, (void*)worker_plug);
                }
                // spy on message: if workerisup then add to thisway
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
                    if (cream->count != 0) {
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
                        dest_plug->messages_tokid_tail->next = newmsg;
                        dest_plug->messages_tokid_tail->nextisready = 1;
                        dest_plug->messages_tokid_tail = newmsg;
                    }
                }
                if (msg->destinations->count != 0) { // undeliverable addresses
                    // this should never happen
                    int i;
                    printerr("Error! Message from %d couldn't find",
                                    msg->source);
                    for (i = 0; i < msg->destinations->count; i++) {
                        printerr("%s %d",
                                        i ? "," : "",
                                        msg->destinations->values[i]);
                    }
                    printerr(".\n");
                    printerr("(type = %lld, contents = \"%s\")\n",
                                    msg->type,
                                    msg->len > 100 ? "long message" : msg->data);
                }
            }
        }
        if (! found_message_waiting) { // nothing to do
            usleep(1000);
        }
    }
} // routermain

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of router ///////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

