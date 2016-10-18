#include "definitions.h"


void machine_main(connection worker_plug)
{
    int32 dowork = 1;
    char buf[90];
    int32 msg_src;
    int64 msg_type;
    char *msg_data = NULL;

    char *deviceroot = NULL;

    addabortsignallistener(); // as last resort for local worker threads,
                              // will very likely leak memory

    // tell cmd we are up!
    snprintf(buf, 90, "%d", worker_plug->thisway->values[0]);
    sendmessage(worker_plug, hq_int, msgtype_workerisup, buf);

    // assemble paths relative to our executable's address
    deviceroot = "~/.mcsync"; // extractdeviceroot(worker_plug->address);

    while (dowork) {
        while (! receivemessage(worker_plug, &msg_src, &msg_type, &msg_data)) {
            usleep(pollingrate);
        }
        switch (msg_type) {
            case msgtype_info:
                    log_line("Machine %s got info message: \"%s\" from %d\n",
                                    "unimplemented", msg_data, msg_src);
            break;
            case msgtype_exit:
                log_line("Worker got exit message... good bye!\n");
                dowork = 0;
            break;
            default:
                    log_line("Worker got unexpected message"
                                    " of type %lld from %d: \"%s\"\n",
                                    msg_type, msg_src, msg_data);
        }
        free(msg_data);
        msg_data = NULL;
    }

    // we were asked to stop working...
    sendmessage(worker_plug, recruiter_int, msgtype_goodbye, "");
    sleep(1);

    free(deviceroot);

    cleanexit(__LINE__); // exiting from the machine
} // machine_main
