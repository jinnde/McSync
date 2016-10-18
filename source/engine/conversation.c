/*
    conversation.c

Communication takes place between the McSync agents by them sending messages
to each other.  Often the agents are on different machines, so they need to
use messages to transmit even the simplest of information.  Typical McSync
operations involve a sequence of requests and responses between the agents,
which we refer to as a conversation.

The intent of this file is to consolidate these conversations into a single
place so that their structure becomes more apparent.  Formerly, each "sentence"
of the conversation was in the file corresponding to its agent, making the
source code look rather like a novel whose sentences have been sorted according
to their speaker.

This file puts the conversations in their natural order, but the reader should
remember that consecutive blocks of code inside converse() are often being
executed on different machines, and so variables do not necessarily contain
the same values for example.  The different blocks of code are effectively in
parallel universes.

*/

/*
    proof that the C precompiler cannot make an increasing sequence
    via a repeated increment instruction:
    definitions are only expanded upon use, not when defined.
    the definition is stored without any expansion.
    so whatever expansion happens is based on only latest textual def of each sym.
    in other words, a def doesn't build on others, but just gets listed with them.
    
    n.b.
    expansion expands args and plugs them into the definition
        (except for #arg, which makes "arg", and arg ## something, which forms a token)
        (successful ## means a valid token was generated with the glue)
    
    #define n 9
    #define x(n) n #n #n n ## n ## n
    #define a b
    #define b c
    x(a) // c "a" "a" aaa
    #define a "2"
    x(a) // "2" "a" "a" aaa

*/    



void start_conversation(int agent_type, int64 conv_starter_msg_type)
{
    // this calls converse with funny arguments to make sure no message matches
    // but at the right point in the conversation, something will match and the
    // conversation will start
    converse(0, NULL, conv_starter_msg_type, NULL);
} // start_conversation

// The convstarts are defined below as well as in definitions.h.
// The compiler will warn if the definitions don't match.
// The msg_types on the other hand are private to this file.

// returns 1 if message is indeed part of a conversation
int converse(int agent_type, listint* src, int64* type, char** data)
{
    if (0) // a nice way to start...
    {
    }
    
    //////////////////////////////////////////////////////////////////////////////
    // first conversation: finding and connecting to a new device
    #define A_convstart_CMD_user_wants_to_connect       101
    #define A_msg_type_CMD_DEV_connectdevice            102
    #define A_msg_type_DEV_CMD_failedrecruit            103
    
    else if (agent_type == cmd_plug && type == convstart_user_wants_to_connect)
    {
        // we are CMD
        // first do sanity checks
        if (gi_device == NULL || gi_device->status != status_inactive)
            return 0;
        if (!specstatevalid())
            return 0;
        // now do the manipulation
        // TODO: Allow the user to specify which address to use
        if (gi_device->reachplan.ipaddrs)
            gi_device->reachplan.whichtouse = gi_device->reachplan.ipaddrs->string;
        // TODO: allow user to specify where to connect *from*
        sendmessage(cmd_plug, hq_int, msgtype_connectdevice, gi_device->deviceid);
    }
    else if (type == 101) conv_A_01_CMD_DEV_please_connect(int32 from, int32 to)
    {
        // we are DEV (the one that should try to connect)
            {
                char *address;
                int32 plugnumber;
                receiverecruitcommand(msg_data, &plugnumber, &address);
                if (! recruitworker(plugnumber, address))
                    sendint32(recruiter_plug, msg_src, msgtype_failedrecruit, plugnumber);
                free(address);
            }
    }
/*
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
*/
    else return 0;
    return 1;
} // converse
