#include "definitions.h"

#define input_buffer_size 1024
#define command_buffer_size 64
#define path_buffer_size 256

virtualnode cmd_virtualroot; // has no siblings and no name
                             // only to be used by the tui thread
virtualnode *browsingdirectory;

void cliprintvirtualnode(virtualnode *node)
{
    virtualnode *child = node->down;
    while (child != NULL) {
        printf("%s\n", child->name);
        child = child->next;
    }
} // cliprintvirtualnode

void clireceivemessage()
{
    int32 msg_src;
    int64 msg_type;
    char* msg_data;

    while (! receivemessage(cmd_plug, &msg_src, &msg_type, &msg_data)) {
        usleep(1000);
    }

    switch (msg_type) {
        case msgtype_virtualdir:
        {
                virtualnode *dir, *child, *receivedchild;
                queue receivedlist = initqueue();
                queuenode receiveditem;
                char *path;

                receivevirtualdir(msg_data, &path, receivedlist);

                dir = findnode(&cmd_virtualroot, path);

                if (!dir) {
                    printf("ERROR: McSync does not know about virtual dir: %s\n", path);
                    goto free_and_return;
                }

                if (!dir->touched) {
                    printf("ERROR: McSync got listing for untouched dir: %s\n", path);
                    goto free_and_return;
                }

                // if a child of dir is in the receivedlist and touched, it is updated
                // if it's missing in the received list, it has to be removed from dir too
                // if it's in the received list but untouched, we can leave it as it is
                for (child = dir->down; child != NULL; child = child->next) {
                    for (receiveditem = receivedlist->head; receiveditem != NULL; receiveditem = receiveditem->next) {
                        receivedchild = (virtualnode*) receiveditem->data;
                        if (!strcmp(child->name, receivedchild->name))
                            break;
                    }
                    if (!receiveditem) // The child was removed on HQ
                        virtualnoderemovenode(&child);
                    else { // Node with same name was found

                        receivedchild = (virtualnode*) queueremove(receivedlist, receiveditem);

                        if (child->touched) // Has HQ signaled the change here (with a msgtype_touch)? Might think about the touched attribute naming, but "dirty" would not be better either :)
                            overwritevirtualnode(&child, &receivedchild); // frees child, received is always untouched
                        else
                            freevirtualnode(receivedchild); // there was no change
                    }
                }
                // left over nodes in receivedlist are new children of dir!
                for (receiveditem = receivedlist->head; receiveditem != NULL; receiveditem = receiveditem->next) {
                    receivedchild = (virtualnode*) queueremove(receivedlist, receiveditem);
                    virtualnodeaddchild(&dir, &receivedchild);
                    if (receivedchild->numchildren > 0)
                        receivedchild->touched = 1;
                }

                dir->touched = 0;

            free_and_return:
                free(path);
                free(msg_data);
                // freequeue does not free data, make sure we do not leak it if we jumped here because of a problem
                while(receivedlist->size > 0) {
                    receivedchild = (virtualnode*) queueremove(receivedlist, receivedlist->head);
                    freevirtualnode(receivedchild);
                }
                freequeue(receivedlist);
        }
            break;
        default:
            free(msg_data);
            break;
    }
} // clireceivemessage


int cligetcommand(char *line, char **argv) // returns number of parsed tokens
{
    char* str = strdup(line);
    char* rest = NULL;
    char* token = strtok_r(str, " ", &rest);
    int32 size = 0, lastlen = 0;
    while (token != NULL) {
        argv[size] = strdup(token);
        token = strtok_r(NULL, " ", &rest);
        size++;
    }
    free(str);
    rest = NULL;
    if (size) {     // delete trailing '\n'
        lastlen = strlen(argv[size-1]);
        argv[size-1][lastlen-1] = 0;
    }
    return size;
} // cligetcommand

void cliprinthelp(void)
{
    printf("This is McSync, version %d.\n", programversionnumber);
    printf("\n");
    printf(" %-7s %-7s\n", "h(elp)", "show this message");
    printf(" %-7s %-7s\n", "q(uit)", "quit McSync");
    printf(" %-7s %-7s\n", "ls", "list virtual files of current directory");
    printf(" %-7s %-7s\n", "cd", "change virtual directory (currently only single depth)");
    printf("\n");
} // cliprinthelp

void clicdintodir(virtualnode *dir, char *path)
{

    if (dir->touched) {
        sendvirtualnoderquest(&cmd_virtualroot, dir);
        clireceivemessage();
    }
    browsingdirectory = dir;
    if (dir != &cmd_virtualroot)
        sprintf(path , "%s", dir->name);
    else
        sprintf(path , "%s", "/");

} // clichangeintodir

void climain(void)
{
    initvirtualroot(&cmd_virtualroot);
    browsingdirectory = &cmd_virtualroot;
    char path[path_buffer_size];
    char line[input_buffer_size] ; // the input line
    char *argv[command_buffer_size];  // the command line argument

    path[0] = '/';
    path[1] = '\0';

    cliprinthelp();

    while (1) {

        int32 commandcount = 0;

        printf("%s> ", path);

        if (!fgets(line, input_buffer_size, stdin)) {
            printf("Invalid input!\n");
            continue;
        }

        commandcount = cligetcommand(line, argv);

        if (!commandcount) {
            continue;
        }

        if (!strcmp(argv[0], "quit") || !strcmp(argv[0], "q")) {
            printf("Good Bye!\n");
            return;
        }

        if (!strcmp(argv[0], "help") || !strcmp(argv[0], "h") ) {
            cliprinthelp();
            goto next_command;
        }

        if (!strcmp(argv[0], "ls") && commandcount == 1) {
            if (browsingdirectory->touched) {
                sendvirtualnoderquest(&cmd_virtualroot, browsingdirectory);
                clireceivemessage();
            }
            cliprintvirtualnode(browsingdirectory);
            goto next_command;
        }

        if (!strcmp(argv[0], "cd") && commandcount == 2) {
            virtualnode *node;

            if (!strcmp(argv[1], "..")) {
                if (browsingdirectory->up)
                    clicdintodir(browsingdirectory->up, path);
                goto next_command;
            }
            // look for the node in the current directory
            for (node = browsingdirectory->down; node != NULL; node = node->next)
                if (!strcmp(node->name, argv[1]))
                    break;

            if (!node)
                printf("Not in current dir: %s\n", argv[1]);
            else
                clicdintodir(node, path);

            goto next_command;
        }

        next_command:
            while(commandcount--)
                free(argv[commandcount]);
    }
} // climain
