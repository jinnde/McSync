# Source Code Structure

McSync is organized as a bunch of interacting agents.
The source files can be understood in relation to these.

    definitions.h           used by everybody

    main.c                  figures out whether it should be CMD / HQ / WKR and runs routermain
    communication.c         handles sending and receiving of messages over channels (PO)
    network.c               connects to remote machine and establishes connection to remote mcsync

    workerops.c             code for WKR
    diskscan.c

    headquarters.c          code for HQ
    comparisons.c

    tui.c                   code for CMD (user interface)
    specs.c                 (de)serialize preferences and configuration

To compile, go to the uppermost directory, and type 'make'.
To run, go one directory higher, and type `'./bin/mcsync'`.

## The Types of Agents

### Commander (CMD)

The commander is the TUI or GUI, where the user is making decisions.
The program started by the user is the commander, running on the machine where the user invoked it, like any executable.
The commander establishes a headquarters (either on the same machine, or on a high-bandwidth machine) to do the real work.
The commander communicates lightly with the headquarters, receiving just enough info for display to the user,
and providing just enough info for the headquarters to do all the work.
The commander might be running on a handheld device with a lousy or expensive connection.
The commander and the headquarters are the only ones who use the virtual tree.

### Headquarters (HQ)

The headquarters communicates with all the workers, requesting scans, comparing to archived histories,
running the history comparison algorithms, creating instructions, and generally telling the workers what to do, based on what the commander tells it to do.

### Workers (WKR)

A worker accesses files on one device, in response to requests from (and communicating results to) the
headquarters.  Each McSync location is handled by one worker in one executable (run from that McSync location).

### Router or Post Office (PO)

Every device has a local router.
The routers are the only ones who know whether messages actually need to be sent to other places, and the only ones who send them.
Other processes just hand their messages to and from the local router.  The routers are like the post office.  Nobody else has to worry about how their message gets to its destination, they just say who it's for.  The network topology for now is a tree.



## Structure

To understand the processes and threads within them, we need to understand that McSync
is in general running on multiple machines, accessing multiple devices.  (Devices are
memory storage devices, such as hard drives and usb sticks.)

Each machine in general has one executable running, but might have more.  For example,
there may be a separate front end interface process, or mcsync might not realize
that two machines are the same.

These executables are linked in a tree.
Each executable has threads for the PO, WKR, and maybe HQ or CMD.


                    ,------- PO ------- PO --------- PO
                   /          \         / \           \
                  /           WKR     WKR  \          WKR
                 /                          \
      PO ----- PO -------- PO                `---- PO
      /        /\           \                       \
    CMD      HQ  WKR        WKR                     WKR


Each worker is reponsible for all file operations on a given device.
Although CMD and HQ can be in the same executable (same PO), here they are shown as separate.

We will think of each PO as being on a different machine, although multiple devices could be connected to one machine, in which case their executables would all be running on that machine.

The processes communicate via streams (such as stdin and stdout).

Within each process there are one or more agents. McSync is these agents working together.
The agents are: one CMD, one HQ, a PO for each executable, and a WKR for each device.
CMD talks only to HQ and POs, while HQ talks only to CMD and WKRs.

Agents communicate with a message passing system that hides the fact that the sender and
receiver may or may not be in the same process or on the same machine.  Each agent has a
unique integer address, and messages are simply addressed to their recipient(s) and delivered.

Each executable has a router (post office, PO) for getting messages to their destination.
The router knows what other executables the process is directly connected to,
and knows which way to send a message addressed to any agent.
More specifically, it knows which subtree adresses are in which directions,
and all else is sent up the tree.

The connections are implemented with plugs. A plug is a connector that allows two
threads in the same process to send messages to each other. An agent is connected to
its local PO with a plug. For connections between POs (i.e., between processes),
at each end there are threads (shipping and receiving, SR) connected to the PO with a plug,
and these two SRs communicate directly with each other using inter-process streams
(actually a distinct pair of threads is used for each one-way link: in, out, err).
We don't call an SR an agent, because we never address messages to an SR.
Thus the PO simply routes messages among a set of plugs.

    (up the tree) SR ---- plug : plug ----.    ,---- plug : plug ---- WKR 5 (hard disk)
    (down to 3,4) SR ---- plug : plug ---- PO 2 ---- plug : plug ---- WKR 6 (usb)
      (down to 7) SR ---- plug : plug ----'    `---- plug : plug ---- maybe HQ and/or CMD

(Note that currently, each PO only has one local worker, unlike this example.)

From the PO's point of view, it just sees a list of plugs, and messages arrive on them, and
arriving messages are sent back out on the appropriate plugs based on their recipient lists.

Behind each plug is either an agent or a stream leading to remote agents, the PO doesn't care.

Currently stderr is only used by RCH (see below), and the source process of the reach
simply logs anything arriving from the RCH's stderr along with everything else, but
this logging is only started once the reach finishes (in thread_main).  Once the
RCH starts up a remote SR and PO, it stops sending data on stderr.  So it looks like
forward_raw_errors never processes anything.


## Creating Structure

To connect to another machine, you can ask your PO to create a reaching (RCH) agent.
This RCH will attempt to reach the target machine.
Once on that machine, the RCH will attempt to find a McSync directory there.
If the machine has no device with a known McSync directory, it can install McSync on one.
Then it can run McSync on that machine and get a PO running.
Then the RCH will transform into a remote SR and PO.

To do this, RCH needs two kinds of information.

    1. how to get from one machine to another
    2. where to look for McSync directories


Locations:

    * Intuitively, a location is an address you can ssh to, nothing more (no McSync need be there, nor any fixed machine).
    * A location is a name with 1 or more (from,goto) = (network / machine, ip address / hostname) pairs.
    * A pair can be augmented with specific instructions for how to make the connection.
    * "Here" also serves as a location in some contexts.  ("Create HQ at: Here")
    * By default all locations are on "this network" until you specify networks explicitly.
    * A pair (network, "none") indicates that a machine at that location can reach that network, but not vice versa.
    * The interface can show a tree of theoretically reachable locations.
    * Locations can belong to location groups.
    * A location has an implicit list of machines that can be there, since machines have locations where they can be.

Machines:

    * By "machine", we mean only a (perhaps temporary) cpu and operating system that supports network connections.
    * A machine is a name, with 0 or more locations / groups (where it can be).
    * A machine has a list of possible McSync dirs, plus path prefixes (perhaps with wildcards) for movable devices.
    * "Unknown" also serves as a machine in some contexts.  (Two usb sticks can be synced in any machine.)

Devices:

    * By "device" we always mean a storage device with a file system on it.
    * A device becomes known to McSync when it has a McSync directory.  The dir stores the device id.
    * A device can be movable, in which case it has a dir suffix (no wildcards?).

Connections:

    * A location A can (try to) connect to a location B.
    * To make a connection from the first machine, we assume a location for it.  (Or ask a script for a guess.)
    * When connecting from A to B, it can be useful to know what machine is at each end,
      so that we know how to start the connection and how to tell when we are connected.
    * Given a list of machines we might be on, we can try to narrow the list.
    * Given a list of machines we might be on, we can try to find a McSync directory.
    * Given a McSync directory, we can try to compile or run an executable.

    * Locations and Machines are only useful for reaching.
    * In the end, only a device can be usefully connected as a remote PO.
      (Because only a device has storage to contain a McSync executable.)

To get from one machine to another, we try locations that are listed as potentially reachable.
When a location presents us with a machine, we can try to detect the machine based on the login sequence,
$HOSTNAME, presence of certain files, or whatever else is available.
Such heuristics can narrow down the list of possible machines, but they are not necessary.
For the possible machines, we try the places we might find .mcsync (non-movable devices first).
The first one we find, we run.

It is just this structure creation that limits the topology to a tree.  The POs wouldn't
mind a web.  It just isn't clear how you would make a loop in the first place.  Well, you
would need to use sockets instead of pipes.  No code for that yet.



## RCH

    Steps in establishing a connection [$1:]$2:$3:$3:$4(dir)
    L/I/R = local/intermediate/remote
    T = TUI thread
    A = algorithm thread
    W = worker thread
    P = plug thread
    C = child process

        expect: script name OR ssh: machine, user
            user@machine:~/.mcsync
            @laptop2:user@machine:user@machine:~/.mcsync
                $1 = where to hop from -- not fully implemented
                $2 = first machine to ssh to
                $3 = continuing machines to ssh to
                $4 = final McSync directory
            sp2sp;running:min;$:AC;$:<input>
                not fully implemented yet

    ::TUI tells algo to connect to a certain device::
    LT  TUIprocesschar  receives 'c'onnect command, sends "newplugplease" (NPP1/2)
                        message to algorithm w/ deviceid string(s)
    LA  algomain        receives NPP1 (devid) or NPP2(dstid,srcid) and calls reachfor

    ::algo asks worker to create a connection to the device::
    LA  algo_reachfor   sends NPP (deviceid + routeraddr) to worker, uses
                     $1 deviceid to find machine and set its routeraddr
    ---                 at this point we would go to a remote machine's worker
                        for a multi-hop connection
    IW  workermain      receives NPP message and calls channel_launch
    IW  channel_launch  creates the new plug, sets target_machine to the machine with
                        the given deviceid, sets routeraddr (on intermediate machine)
    IP  thread_main     recognizes plug as needing connection and calls reachforremote

    ::dedicated thread tries to actually reach device::
    IP  reachforremote  does regular scrolling, gives birth, gives further commands
                        to process, waits for McSync to show signs of life, sends
                  $3,$4 router address
    IP  givebirth       forks, sends child (one-way) to firststeps
    IC  firststeps      fixes pipes
    IC  transmogrify $2 become ssh or whatever through execl
    ---                 keep receiving commands from reachforremote
    RC  main            prints messages to show signs of life, receives router address
    RC  routermain      tells channel_launch to create a parent plug and a worker
                        plug with the given plug id
    RC  channel_launch  creates the new plug, sets target_machine and routeraddr
    RW  thread_main     sees it is the worker_plug
    RW  workermain      sends algorithm a "workerisup" message
    ---                 every hop along the way adds the worker's plug number to the
                        thisway it came from

    ::communication is up::
    LA                  algorithm receives this and asks worker for deviceid string
    RW                  worker sends deviceid
    LA                  algo finds machine with given deviceid, sets status_connected
    LT                  TUI shows machine is connected
--------------------------------------------------------

Topworker should be eliminated.

Tui should know the PO for each connected device and HQ, and the location path for each PO.
It can draw the tree.

Tui should tell its PO to reach for preferred HQ location if not "here".

Tui should tell (by default HQ's) PO to reach for a new WKR, remote location, or HQ.

When reaching for a remote location, it can be done either step by step (each step being
sent as a command from the tui), or interactive by the user through the tui (using raw
streams), or as a set batch of steps.

--------------------------------------------------------


thread 1:
main calls routermain.
The post office (router) launches all its customers (and calls them kids).
That is, routermain sets up channels with channel_launch.
Then it enters its infinite main loop: delivering mail.
Whenever there is no mail, it sleeps for a millisecond.
It snoops on workerisup messages, adding the source to the thisway for where it came from.

Messages are sent between agents -- every agent has a postal address (an integer).
At a given post office, there is a connection_list of plugs, each listing the agents
which lie in that direction ("thisway").  The parent plug is used for all unknown agents.
Plugs are created by channel_launch.
While the post office shuffles messages from plug to plug, the other sides of the plugs
are handled by dedicated threads.
Plugs that go out-of-process include threads to read and write messages on
communication links (streams).
Within-process plugs might include a worker (WKR) or the algo (HQ).

Right now a channel_launch request by a worker winds up setting the plug number in the
devicelocater based on the deviceid... what's the point of that contortion?

there are 4 types of agents
1. tui = CMD = 2
2. algo = HQ = 1
3. worker = WKR = 3
4. parent = 0  this is not actually an agent

Finally, channel_launch starts a "listener" thread, which is where thread_main is used.

read more at top of network.c...



## McSync operation

*This section should be moved to another readme file.  Everything else in this file is about agents and their structure, which is a prerequisite for understanding the code.  This section is about what the HQ will typically do, and so it belongs elsewhere, probably just in Algorithm.txt.*

The user starts the TUI/GUI/batch commander (CMD).
The commander sets up a headquarters (HQ).
The commander sets up a network of routers (PO) and workers (WKR).

CMD can do whatever it wants, since it represents the user doing arbitrary things.
CMD and HQ work together tightly.  Their communication consists of (1) the HQ keeping CMD
up-to-date on all the directories that CMD has looked at, and (2) the CMD giving little
directives to the HQ, like start a scan here, or record a preference there.

Here we will outline the typical sequence of events for syncing a directory tree between machines.

1. CMD tells HQ to get scans (Ah) and history updates (AH) from workers.
    To get a scan or history update, HQ tells worker what scans and histories it already has, and worker sends delta.

2. HQ analyzes results and sends CMD updates for whatever device directories CMD is tracking,

3. HQ also forwards any useful gossip (probably very little) back to other workers.

4. CMD and HQ collect guidance from the user.

5. When the user is ready, HQ has the identification guidance and preference guidance it needs.

6. HQ sends instructions to the workers, who update the histories and files accordingly.

7. Successfully updated files yield new scans which the workers can add into the history, as no new identification guidance is needed for this.

8. The workers send in the gossip resulting from these new scans.

9. The HQ propagates this gossip as needed.
