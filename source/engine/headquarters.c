#include "definitions.h"


//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// start of algo main //////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

char* statusword[] = {
    "inactive",
    "reaching",
    "connected",
};

void setstatus(int32 who, status_t newstatus)
{
    device* mach;
    status_t oldstatus;
    char buf[90];

    // find device for "who"
    for (mach = devicelist; mach != NULL; mach = mach->next) {
        if (mach->reachplan.routeraddr == who)
            break;
    }
    if (mach == NULL) {
        printerr("Error: Machine %d not in machine list.\n", who);
        return;
    }
    // set the new status
    oldstatus = mach->status;
    mach->status = newstatus;
    // take any status-change-based actions
    switch (newstatus) {
        case status_inactive:
                sendmessage(algo_plug, TUI_int, msgtype_disconnect,
                            mach->deviceid);
            break;
        case status_reaching:
            break;
        case status_connected:
                snprintf(buf, 90, "%s", mach->deviceid); // unnecessary to use buf
                sendmessage(algo_plug, TUI_int, msgtype_connected, buf);
            break;
    }
    printerr("Changed status of %d (%s) from %d (%s) to %d (%s).\n",
                who, mach->nickname,
                oldstatus, statusword[oldstatus],
                newstatus, statusword[newstatus]);
} // setstatus

static int NextFreeAddress = firstfree_int;

void algo_reachfor(char* deviceid, char* reachfrom_deviceid) // RFID can be NULL
{
    device* target_m = NULL;
    device* m;
    int reachfrom_addr = -1;
    char buf[90];

    for (m = devicelist; m != NULL; m = m->next) {
        if (!strcmp(m->deviceid, deviceid))
            target_m = m;
        if (reachfrom_deviceid
            && !strcmp(m->deviceid, reachfrom_deviceid)) {
            if (m->status == status_connected) {
                reachfrom_addr = m->reachplan.routeraddr;
            } else {
                printerr("Error: Hop-from machine (id \"%s\") not connected.\n",
                        reachfrom_deviceid);
                return;
            }
        }
    }
    if (target_m == NULL) { // can this happen?
        printerr("Error: Target machine id \"%s\" not found.\n", deviceid);
        return;
    }
    if (reachfrom_addr == -1) {
        if (reachfrom_deviceid == NULL) { // didn't find it cause we didn't look
            reachfrom_addr = topworker_int;
        } else {
            printerr("Error: Hop-from machine id \"%s\" not found.\n",
                    reachfrom_deviceid);
            return;
        }
    }

    // start a remote mcsync for this device
    target_m->reachplan.routeraddr = NextFreeAddress; // awaiting status_connected
    setstatus(NextFreeAddress, status_reaching);
    snprintf(buf, 90, "%s%c%d", deviceid, 0, NextFreeAddress);
    sendmessage2(algo_plug, reachfrom_addr, msgtype_newplugplease, buf);
    // Notice we don't send the device target_m!
    // It will be recovered from the deviceid by topworker in channel_launch.
    // This lets us use a worker besides topworker (multi-hop case), because
    // the deviceid for a device is the same everywhere.
    NextFreeAddress++;
} // algo_reachfor

int waitmode = 0; // changed to 1 on startup if "-wait" flag is provided

void algo_init(void) // called as soon as local worker is ready to do work
{
    if (waitmode)
        return;
    // here we do whatever the configuration file tells us to do on startup

} // algo_init

void algomain(void)
{
    int32 msg_src;
    int64 msg_type;
    char* msg_data;
    device* m;

    virtualtreeinit();

    while (1) {
        while (! receivemessage(algo_plug, &msg_src, &msg_type, &msg_data)) {
            usleep(1000);
        }
        // we got a message
        switch (msg_type) {
            case msgtype_info:
                    printerr("algo got info message: \"%s\" from %d\n",
                                    msg_data, msg_src);
                    break;
            case msgtype_workerisup:
                    // we need to find out (topworker) or verify who we are
                    sendmessage(algo_plug, msg_src, msgtype_identifydevice, "");
                    // we won't say we're connected till we know to whom!
                    break;
            case msgtype_deviceid:
                    // need to find the device with the id in msg_data
                    // (we already know it if we asked for connection,
                    // but not for top worker -- topworker tells us who we are)
                    printerr("Heard that plug %d is for device %s.\n",
                            msg_src, msg_data);
                    for (m = devicelist; m != NULL; m = m->next) {
                        if (! strcmp(m->deviceid, msg_data)) {
                            // m is the device with the deviceid
                            if (m->reachplan.routeraddr == msg_src) {
                                // already set to what we would expect
                            } else {
                                if (m->reachplan.routeraddr == -1
                                        && msg_src == topworker_int) {
                                    // it is from the topworker
                                    m->reachplan.routeraddr = topworker_int;
                                } else {
                                    printerr("Error: Plug confusion!");
                                    printerr(" (%d reported name \"%s\", already"
                                            " owned by %d)\n", msg_src, msg_data,
                                            m->reachplan.routeraddr);
                                }
                            }
                            break;
                        }
                    }
                    if (m == NULL) {
                        printerr("Error: Reported device id not known: %s\n",
                                    msg_data);
                    }
                    setstatus(msg_src, status_connected);
                    if (msg_src == topworker_int)
                        algo_init();
                    break;
            case msgtype_newplugplease1:
                    algo_reachfor(msg_data, NULL); // msg_data is m->deviceid
                    break;
            case msgtype_newplugplease2:
                    // msg_data is destid, sourceid
                    algo_reachfor(msg_data, secondstring(msg_data));
                    break;
            case msgtype_disconnect:
                    setstatus(atoi(msg_data), status_inactive);
                    break;
            default:
                    printerr("algo got unexpected message"
                                    " of type %lld from %d: \"%s\"\n",
                                    msg_type, msg_src, msg_data);
        } // switch on message type
        free(msg_data);
    } // loop on messages
} // algomain

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of algo main ////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

