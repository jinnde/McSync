#include "definitions.h"

char *generatedeviceid() // unique device id generation using /dev/random
{                        // allocs string, free when done!
    char randombuf[device_id_size];
    char *deviceid;
    FILE *devrandom;
    int32 idstringsize = device_id_size * 2 + 1;
    int32 i, j;

    devrandom = fopen("/dev/random", "rb");
    if (!devrandom) {
        printerr("Error: Can't open /dev/random for unique device id creation (%s)\n",
                 strerror(errno));
        return NULL;
    }

    if (fread(randombuf, 1, device_id_size, devrandom) != device_id_size) {
        printerr("Error: Can't read from /dev/random for unique device id creation (%s)\n",
                 strerror(errno));
        fclose(devrandom);
        return NULL;
    }
    fclose(devrandom);

    // store the device id in hex for human readability
    deviceid = (char*) malloc(idstringsize);
    for (i = 0, j = 0; i < device_id_size; i++, j = i * 2) {
        tohex(randombuf[i], &deviceid[j], &deviceid[j + 1]);
    }
    *(deviceid + idstringsize) = '\0';
    return deviceid;
} // generatedeviceid

char buffer[90], *bufpos; // use is private to next two functions, 90 digit max...

void commanumberrec(int64 n, int dig) // helper func for commanumber
{
    if (n == 0) {
        bufpos = buffer + 1;
        return;
    }
    commanumberrec(n / 10, dig + 1);
    *bufpos++ = (char)('0' + n % 10);
    if (dig % 3 == 0)
        *bufpos++ = dig ? ',' : 0;
} // commanumberrec

char *commanumber(int64 n) // returns human-readable integer in reused buffer
{
    if (n == 0)
        return "0";
    buffer[0] = '-';
    commanumberrec(n > 0 ? n : -n, 0);
    if (n < 0)
        return buffer;
    else
        return buffer + 1;
} // commanumber

int SelectedMachineColor[6];
int UnselectedMachineColor[6];
char CurrentDeviceTask[6][8] = { "   ", "   ", "   ", "[S]", "[W]", "[L]" };
// ^ is printed next to the device nick name in the device list

enum curscolors {
    BLACKonYELLOW = 1,
    BLACKonGREEN,
    BLACKonRED,
    BLACKonCYAN,
    BLACKonWHITE,
    REDonYELLOW,
    REDonBLACK,
    REDonWHITE,
    GREENonBLACK,
    GREENonWHITE,
    YELLOWonBLACK,
    WHITEonBLUE,
    WHITEonRED,
    WHITEonBLACK,
    CYANonBLACK
};

void setupcolors(void)
{
    // Man page says 0 color pair is wired to white on black, it also says curses
    // assumes white on black is the default; on my mac 0 gives term's default.
    // In the following, color -1 (as arg 2 or 3) means default fg or bg,
    // but we shouldn't use default colors because who knows what they go with.
    // curs: color_set(i, NULL) uses pair i
#define TERMINALCOLORS 0
    init_pair(BLACKonYELLOW, COLOR_BLACK, COLOR_YELLOW); // curs: black on yellow
    init_pair(GREENonWHITE, COLOR_GREEN, COLOR_WHITE);
    init_pair(REDonWHITE, COLOR_RED, COLOR_WHITE);
    init_pair(YELLOWonBLACK, COLOR_YELLOW, COLOR_BLACK);
    init_pair(GREENonBLACK, COLOR_GREEN, COLOR_BLACK);
    init_pair(REDonBLACK, COLOR_RED, COLOR_BLACK);
    init_pair(BLACKonGREEN, COLOR_BLACK, COLOR_GREEN);
    init_pair(BLACKonRED, COLOR_BLACK, COLOR_RED);
    init_pair(WHITEonBLUE, COLOR_WHITE, COLOR_BLUE);
    init_pair(WHITEonRED, COLOR_WHITE, COLOR_RED);
    init_pair(BLACKonWHITE, COLOR_BLACK, COLOR_WHITE);
    init_pair(BLACKonCYAN, COLOR_BLACK, COLOR_CYAN);
    init_pair(WHITEonBLACK, COLOR_WHITE, COLOR_BLACK);
    init_pair(REDonYELLOW, COLOR_RED, COLOR_YELLOW);
    init_pair(CYANonBLACK, COLOR_CYAN, COLOR_BLACK);

    SelectedMachineColor[status_inactive] = BLACKonRED;
    SelectedMachineColor[status_reaching] = BLACKonYELLOW;
    SelectedMachineColor[status_connected] = BLACKonGREEN;
    SelectedMachineColor[status_scanning] = BLACKonCYAN;
    SelectedMachineColor[status_storing] = BLACKonCYAN;
    SelectedMachineColor[status_loading] = BLACKonCYAN;

    UnselectedMachineColor[status_inactive] = REDonBLACK;
    UnselectedMachineColor[status_reaching] = YELLOWonBLACK;
    UnselectedMachineColor[status_connected] = GREENonBLACK;
    UnselectedMachineColor[status_scanning] = CYANonBLACK;
    UnselectedMachineColor[status_storing] = CYANonBLACK;
    UnselectedMachineColor[status_loading] = CYANonBLACK;
} // setupcolors

// gi is for global interface --- variables local to the TUI routines
int gi_scrx, gi_scry; // screen size
int gi_msx, gi_msy; // mouse location
int gi_mode = 1; // 1 = mousing devices, 2 = mousing files,
// 3 = entering input (in mode 1)
// 4 means deleting something (in mode 1)
int gi_savemode; // push previous mode while mode is 3
int gi_cols; // for file listing
int gi_selecteddevice = 0; // 0=none selected, 1-n indicates device 1-n

// variables for editing
int gi_editselection = 0; // which editable item is selected, 0=none
int gi_editableitemnumber; // which editable item we are currently printing
int gi_editselx; // screen coordinates of start of selected item
int gi_editsely;
int gi_editcursor; // position of cursor in edited string (0 = at start)
char *gi_newstring; // the edited version
char *gi_errorinfo; // the problem found by the validator (or NULL if ok)
int gi_dirtyspecs = 0; // whether specs have changed since loading the file

// the following variables are set by refreshscreen()
device *gi_device = NULL; // the selected device struct if any, or NULL
graft *gi_graft = NULL; // the selected graft struct if any, or NULL
int gi_editableitemcount; // the number of visible editable items
char **gi_editobject; // the pointer to the original string we get to edit
int gi_editobjecttype = -1; // 1=nickname, 2=utility directory, 3=ip address,
// 4=graft path, 5=virtual graft point, 6=prune point
// 0=device showing but nothing selected, -1=device view but no devices
int gi_numdevices = 0; // the number of devices in devicelist

// variables for the browser
virtualnode *browsingdirectory; // the directory we are currently mousing in
int gi_btop, gi_bbottom; // the top and bottom lines available to the browser
int gi_bstyle; // what colors/format to use: 1=directory stack, 2=mousing contents

void starteditableitems(void)
{
    gi_editableitemnumber = 0;
} // starteditableitems

void endeditableitems(void)
{
    gi_editableitemcount = gi_editableitemnumber;
    if (gi_editselection > gi_editableitemcount)
        gi_editselection = 0;
} // endeditableitems

int editableitem(char **item, int itemtype) // type: 1=nn, ud, ipa, gp, vgp, pp=6
// this gets called to display any editable item, returns true if item is selected
{
    int editing;
    editing = (++gi_editableitemnumber == gi_editselection);
    if (editing) {
        if (gi_errorinfo != NULL) { // we have an error message to flash up
            color_set(WHITEonRED, NULL);
            printw("%s", gi_errorinfo);
            color_set(BLACKonWHITE, NULL);
            return 1;
        }
        color_set(gi_mode == 3 ?
                    WHITEonBLUE : YELLOWonBLACK, NULL); // 3 means editing
        getyx(stdscr, gi_editsely, gi_editselx);
        gi_editobject = item;
        gi_editobjecttype = itemtype;
        printw("%s", gi_mode == 3 ? gi_newstring : *item); // 3 means editing
        color_set(BLACKonWHITE, NULL);
        return 1;
    } else {
        printw("%s", *item);
        return 0;
    }
} // editableitem

void startediting(void)
{
    if (gi_editselection == 0) {
        return;
    }
    gi_savemode = gi_mode;
    gi_mode = 3; // 3 means editing
    gi_editcursor = 0;
    gi_newstring = strdup(*gi_editobject);
} // startediting

void clearrestofline(void)
{
    int k;
    int currentx, currenty;
    getyx(stdscr, currenty, currentx);
    for (k = gi_scrx - currentx; k > 0; k--)
        printw(" ");
} // clearrestofline

void refreshdevices(void)
{
    device *d, *selm = NULL;
    int num = 0;

    move(0,0); // curs: move to top corner
    color_set(YELLOWonBLACK, NULL);
    if (gi_selecteddevice == 0 && devicelist != NULL)
        gi_selecteddevice = 1;
    if (devicelist == NULL)
        printw("  <<< no devices >>>  ");
    for (d = devicelist; d != NULL; d = d->next) {
        num++;
        color_set(UnselectedMachineColor[d->status], NULL);
        if (gi_selecteddevice == num) {
            if (gi_mode == 1 || gi_mode == 3 || gi_mode == 4)
                color_set(SelectedMachineColor[d->status], NULL);
            selm = d;
        }
        printw("   %s%s", d->nickname, CurrentDeviceTask[d->status]);
    }
    color_set(UnselectedMachineColor[status_inactive], NULL);
    clearrestofline();

    color_set(BLACKonWHITE, NULL);
    gi_numdevices = num;
    if (num == 0) {
        gi_editobjecttype = -1;
    } else if (gi_selecteddevice != 0) {
        gi_editobjecttype = 0; // may be overwritten below by editableitem()
    }

    {
        // record how much space is left after device names are shown
        int currentx, currenty;
        getyx(stdscr, currenty, currentx);
        gi_btop = currenty;
    }

    gi_device = selm;
    gi_graft = NULL;

    if (selm != NULL && (gi_mode == 1 || gi_mode == 3 || gi_mode == 4)) {
        // mousing devices or editing or deleting, so show the device details!
        stringlist *st;
        graft *g;

        starteditableitems(); // 2nd arg is type: 1=nn, ud, ipa, gp, vgp, pp=6
        printw("\n\n> name: ");
        editableitem(&(selm->nickname), 1);

        for (st = selm->reachplan.ipaddrs; st != NULL; st = st->next) {
            printw("\n> address: ");
            editableitem(&(st->string), 3);
        }

        for (g = graftlist; g != NULL; g = g->next) {
            if (g->host != selm)
                continue;
            printw("\n\nreal path: ");
            if (editableitem(&(g->hostpath), 4))
                gi_graft = g;

            printw("\ngrafted onto: ");
            if (editableitem(&(g->virtualpath), 5))
                gi_graft = g;

            for (st = g->prunepoints; st != NULL; st = st->next) {
                printw("\n  ignoring: ");
                if (editableitem(&(st->string), 6))
                    gi_graft = g;
            } // looping over prunepoints
        } // looping over graftlist

        endeditableitems();
    } // if a device is selected
} // refreshdevices


void showcontents(virtualnode *dir)
// show filename array according to gi_bstyle on lines gi_btop...gi_bbottom
{
    virtualnode *vn;
    int i;
    int justprintedselection;
    int height = gi_bbottom - gi_btop + 1 - 1; // minus one for the blue line

    // recompute numbers as requested
    if (dir->firstvisiblenum == -1) { // if no scroll state, display from start
        dir->firstvisiblenum = 0;
        dir->firstvisible = dir->down;
    }
    dir->firstvisiblenum = dir->cols * (dir->firstvisiblenum / dir->cols); // jic
    if (dir->selectionnum == -1) { // try to find the number of the beast
        for (vn = dir->down, i = 0; vn != NULL; vn = vn->next, i++)
            if (vn == dir->selection) {
                dir->selectionnum = i;
                break;
            }
        if (dir->selectionnum == -1) { // if not found, select the first item
            dir->selectionnum = dir->firstvisiblenum;
            dir->selection = dir->firstvisible;
        }
    }
    if (gi_bstyle == 2) // if this is the current directory
        height -= 2; // then we will use two lines for file info
    // scroll as necessary
    while (dir->firstvisiblenum > 0 // can scroll up?
            && (dir->firstvisiblenum > dir->selectionnum // selection higher up?
                || dir->firstvisiblenum + dir->cols * (height - 1)
                            > dir->numchildren)) { // at least fill given lines
        dir->firstvisiblenum -= dir->cols;
        for (i = dir->cols; i > 0; i--)
            if (dir->firstvisible->prev != NULL)
                dir->firstvisible = dir->firstvisible->prev;
    }
    while (dir->firstvisiblenum + dir->cols * height <= dir->selectionnum) {
        // selection is below visible area
        dir->firstvisiblenum += dir->cols;
        for (i = dir->cols; i > 0; i--)
            if (dir->firstvisible->next != NULL)
                dir->firstvisible = dir->firstvisible->next;
    }
    // draw
    move(gi_btop, 0);
    // draw the blue line
    color_set(WHITEonBLUE, NULL);
    printw("%s", dir->firstvisiblenum > 0 ? "..." : "   ");
    printw("%s", dir->firstvisiblenum + dir->cols * height < dir->numchildren
                                ? " ..." : "    ");
    for (i = 7; i < gi_scrx; i++) {
        printw(" ");
    }

    if (dir->down == NULL) {
        color_set(REDonWHITE, NULL);
        if (dir->numchildren == 0)
            printw("  <<< Empty Directory >>>  ");
        return;
    }
    // fill grid
    color_set(BLACKonWHITE, NULL);
    justprintedselection = 0;
    for (vn = dir->firstvisible, i = 0;
                vn != NULL && i < dir->cols * height;
                vn = vn->next, i++) {
        int availablewidth = gi_scrx - (dir->cols - 1); // don't count separators
        int columnwidth = availablewidth / dir->cols;
        // fatten some columns so that columns fill screen
        columnwidth += (i % dir->cols < availablewidth % dir->cols);
        columnwidth -= 2; // for margin spaces
        // pick the color
        if (gi_bstyle == 1) { // directory stack?
            if (vn == dir->selection) // is this the selected item?
                color_set(BLACKonYELLOW, NULL);
            else
                color_set(BLACKonWHITE, NULL);
        } else if (gi_bstyle == 2) { // current directory
            if (vn == dir->selection) { // is this the selected item?
                color_set(YELLOWonBLACK, NULL);
                justprintedselection = 1;
            } else
                color_set(BLACKonWHITE, NULL);
        } else { // should never happen
        }
        // print the text
        printw(" %-*.*s ", columnwidth, columnwidth, vn->name);
        // if between columns, print the green separator
        if (vn->next != NULL && (i + 1) % dir->cols != 0) {
            color_set(BLACKonGREEN, NULL);
            printw(" ");
        } else if (justprintedselection) { // print info on the selected file
            justprintedselection = 0;
            if ((i + 1) % dir->cols != 0) { // are we not at end of line?
                color_set(BLACKonWHITE, NULL);
                clearrestofline();
            }
            color_set(WHITEonBLACK, NULL);
            printw("Here's a ton of information on %s.", dir->selection->name);
            clearrestofline();
            printw("For example, it contains %d items (%d children)",
            dir->selection->subtreesize - 1, dir->selection->numchildren);
            clearrestofline();
        }
    }
    // color rest of line
    {
        int cy, cx;
        getyx(stdscr, cy, cx);
        if (cx != 0) {
            color_set(BLACKonWHITE, NULL);
            clearrestofline();
        }
    }
} // showcontents

void showstack(virtualnode *dir)
{
    if (dir == NULL) // base case
        return;
    showstack(dir->up); // first do ancestors
    // now we print our line...
    {
        int oldbottom = gi_bbottom;
        gi_bbottom = gi_btop + 1;
        showcontents(dir);
        gi_bbottom = oldbottom;
        gi_btop += 2; // the lines we just used are no longer available
    }
} // showstack

void refreshbrowser(void)
{
    // gi_btop = 2; this is set in refreshdevices
    gi_bbottom = gi_scry - 3; // leave two lines for help
    gi_bstyle = 1; // directory stack style
    showstack(browsingdirectory->up); // this reduces gi_bbottom further
    gi_bstyle = 2; // mousing contents style

    // show the contents of the directory
    showcontents(browsingdirectory);
    color_set(BLACKonWHITE, NULL); // just in case it didn't get reset
} // refreshbrowser

typedef enum {
    cmd_plain_char, // a char being typed in as part of some text, not a command
    cmd_start_editing,
    cmd_add_device,
} keycommand_t;

#define keymult 3

typedef struct keycommand_struct {
    char* key;
    char* desc;
    char* longdesc;
    int32 validity; // this command is available in any mode whose bit is set
    keycommand_t commandcode; // this code gets sent if any of keys[] are received
    int keys[keymult]; // if fewer are specified in initializer, rest are set to 0
} *keycommand;

struct keycommand_struct keycommand_array[] = {
    {"Ret", "Edit Entry", "Change the value of the currently highlighted item",
        1,  cmd_start_editing,      {KEY_ENTER, 13}}, // ^M
    {"M", "Add Device", "Add a new device to the list of devices",
        1,  cmd_add_device,        {'m', 'M'}},
    {NULL, NULL, NULL, 0, 0, {0}}};

char *devicehelparray[][2] = {
    {"Ret", "Edit Entry"},
    {"C", "Connect"},
    {"X", "Disconnect"},
    {"U", "Unlink"},
    {"M", "Add Device"},
    {"DM", "Delete Device"},
    {"A", "Add Address"},
    {"DA", "Delete Address"},
    {"G", "Add Graft"},
    {"DG", "Delete Graft"},
    {"P", "Add Prune Point"},
    {"DP", "Delete Prune Point"},
    {"S", "Save Configuration"},
    {"V", "View Files"},
    {"Q", "Quit"},
    {NULL, NULL}};

char *browserhelparray[][2] = {
    {"^F", "Forward 1 Entry"},
    {"^B", "Back 1 Entry"},
    {"^N", "Next Line of Entries"},
    {"^P", "Previous Line of Entries"},
    {"/", "Enter Subdirectory"},
    {"Ret", "Leave Subdirectory"},
    {"+", "More Columns"},
    {"-", "Fewer Columns"},
    {"Space", "Toggle Detail View"},
    {"V", "View Devices"},
    {"S", "Start Scans"},
    {"VQ", "Quit"},
    {NULL, NULL}};

char *edithelparray[][2] = {
    {"^G", "Abort Editing"},
    {"Ret", "Done Editing"},
    {"^K", "Erase Rest"},
    {"^F", "Move Forward"},
    {"^B", "Move Backward"},
    {"^A", "Move to Start"},
    {"^E", "Move to End"},
    {"^H", "Delete Char"},
    {"^D", "Delete Char"},
    {"a-z", "Enter Text"},
    {NULL, NULL}};

char *deletehelparray[][2] = {
    {"M", "Delete Device"},
    {"A", "Delete Address"},
    {"G", "Delete Graft"},
    {"P", "Delete Prune Point"},
    {"N", "No, Delete Nothing!"},
    {"any other key", "No, Delete Nothing!"},
    {NULL, NULL}};

void refreshhelp(void)
{
    int i;
    char *(*arr)[][2];
    int currentx, currenty;

    //rectset(gi_scry - 2, gi_scry - 1, 1);
    move(gi_scry - 2, 0);
    if (gi_mode == 1) { // 1 means mousing devices
        arr = &devicehelparray;
    } else if (gi_mode == 2) { // 2 means mousing files
        arr = &browserhelparray;
    } else if (gi_mode == 3) { // 3 means editing
        arr = &edithelparray;
    } else if (gi_mode == 4) { // 4 means deleting something
        arr = &deletehelparray;
    } else {
        // should never happen
        return;
    }

    for (i = 0; (*arr)[i][0] != NULL; i++) {
        int type;
        getyx(stdscr, currenty, currentx);
        if (currentx > 0
            && currenty == gi_scry - 2
            && strlen((*arr)[i][0]) + strlen((*arr)[i][1]) + 3
                + currentx > gi_scrx) // printing next wouldn't fit
            clearrestofline();
        if (currenty == gi_scry - 1
            && strlen((*arr)[i][0]) + strlen((*arr)[i][1]) + 3 + 12
                + currentx > gi_scrx // printing next wouldn't leave room for "O"
            && ((*arr)[i+1][0] != NULL
                || strlen((*arr)[i][0]) + strlen((*arr)[i][1]) + 3
                + currentx > gi_scrx) // we can't just print one more and be done
            && ((*arr)[i+1][0] == NULL || (*arr)[i+2][0] != NULL
                || strlen((*arr)[i][0]) + strlen((*arr)[i][1]) + 3
                 + strlen((*arr)[i+1][0]) + strlen((*arr)[i+1][1]) + 3
                 + currentx > gi_scrx)) { // we can't just print two more
            color_set(BLACKonYELLOW, NULL);
            printw("O");
            color_set(BLACKonWHITE, NULL);
            printw(" Other Cmds");
            clearrestofline();
            break;
        }
        type = gi_editobjecttype;
        if (gi_mode == 1 && (// 1 means mousing devices
            // gi_editobjecttype is: 1=nn, ud, ipa, gp, vgp, pp=6, just m, no m
            // if evaluates to true, don't print help command
               (!strcmp((*arr)[i][0], "Ret") && type <= 0)
            || (!strcmp((*arr)[i][0], "C") && (type == -1
                                        || gi_device->status != status_inactive
                                        || !specstatevalid()))
            || (!strcmp((*arr)[i][0], "X") && (type == -1
                                        || gi_device->status != status_connected))
            || (!strcmp((*arr)[i][0], "U") && (type == -1
                                        || !gi_device->linked))
            || (!strcmp((*arr)[i][0], "DM") && type == -1)
            || (!strcmp((*arr)[i][0], "A") && type == -1)
            || (!strcmp((*arr)[i][0], "DA") && type != 3)
            || (!strcmp((*arr)[i][0], "G") && type == -1)
            || (!strcmp((*arr)[i][0], "DG") && (type < 4 || type > 6))
            || (!strcmp((*arr)[i][0], "P") && (type < 4 || type > 6))
            || (!strcmp((*arr)[i][0], "DP") && type != 6)
            || (!strcmp((*arr)[i][0], "S") && (!gi_dirtyspecs || !specstatevalid()))
            )) // yes, we could test with i, but this way it's easier to maintain
            continue;
        if (gi_mode == 4 && (// 4 means deleting something
            // gi_editobjecttype is: 1=nn, ud, ipa, gp, vgp, pp=6, just m, no m
               (!strcmp((*arr)[i][0], "M") && type == -1)
            || (!strcmp((*arr)[i][0], "A") && type != 3)
            || (!strcmp((*arr)[i][0], "G") && (type < 4 || type > 6))
            || (!strcmp((*arr)[i][0], "P") && type != 6)
            )) // we could test with i, but it's easier to maintain this way
            continue;
        color_set(strcmp((*arr)[i][0], "S") ? BLACKonYELLOW : REDonYELLOW, NULL);
        printw("%s", (*arr)[i][0]);
        color_set(strcmp((*arr)[i][0], "S") ? BLACKonWHITE : REDonWHITE, NULL);
        printw(" %s  ", (*arr)[i][1]);
    }
    clearrestofline();
} // refreshhelp

void refreshscreen(void)
{
    clear(); // curs: clear the screen
    getmaxyx(stdscr, gi_scry, gi_scrx); // curs: get screen size

    refreshdevices();

    if (gi_mode == 2)
        refreshbrowser();

    refreshhelp();

    // finally, place the cursor
    if (gi_mode == 3) // 3 means editing
        move(gi_editsely, gi_editselx + gi_editcursor);
    else
        move(0, gi_scrx - 1); // upper right corner seems to be least obtrusive

    refresh(); // curs: copy virtual screen to physical terminal
    // supposedly it only sends changed portions; I'm not convinced
    // probably changed means written-to, even if it's the same as before
} // refreshscreen


char *validate(char *str, int type) // returns NULL if str ok, error string if not
{
    switch (type) { // type: 1=nn, ud, ipa, gp, vgp, pp=6
    case 1: // device nickname
        {
            device *m;
            if (*str == 0) // is it the empty string?
                return "The empty string is not a good device name!";
            if (index(str, ' ') != NULL)
                return "No spaces allowed in the device name!"
                        "  (underscore _ is ok)";
            for (m = devicelist; m != NULL; m = m->next) {
                if (m != gi_device && !strcmp(m->nickname, str))
                    return "Device name already in use!";
            }
        }
        break;
    case 3: // ip address
        {
        }
        break;
    case 2: // utility directory on a disk drive somewhere
    case 4: // graft point (directory on a drive somewhere)
        {
            if (str[0] != '/' && str[0] != '~') // test works on "" too
                return "Directory must start with '/' or '~'";
        }
        break;
    case 5: // virtual graft point
    case 6: // prune point
        {
            if (str[0] != '/') // test works fine on empty string
                return "Virtual directory must start with '/'";
        }
        break;
    default: // should never happen
        break;
    }
    return NULL;
} // validate

void endediting(int keep)
{
    gi_mode = gi_savemode; // pop back to the saved mode
    if (keep) {
        gi_errorinfo = validate(gi_newstring, gi_editobjecttype);
        if (gi_errorinfo == NULL) { // passed validator
            if (gi_editobjecttype == 5) { // 5=virtual graft point
                // mapgraftpoint(gi_graft, *gi_editobject, 0, 1);
                // mapgraftpoint(gi_graft, gi_newstring, 0, 0);
            }
            if (gi_editobjecttype == 6) { // 6=prune point
                // mapgraftpoint(gi_graft, *gi_editobject, 1, 1);
                // mapgraftpoint(gi_graft, gi_newstring, 1, 0);
            }
            // replace the string
            free(*gi_editobject);
            *gi_editobject = gi_newstring;
            gi_dirtyspecs = 1; // only for specs-related things!
        } else { // validator does not like it
            beep();
            gi_mode = 3; // 3 means editing
            refreshscreen();
            usleep(1000000); // let the error message show for one second
            gi_errorinfo = NULL;
            // screen will be refreshed when we return now
        }
    } else {
        free(gi_newstring);
    }
} // endediting

void handlemouseevents(int ch)
{
    MEVENT mousedata; // short id; int x,y,z; mmask_t bstate;
    switch (ch) {
        case KEY_RESIZE:
                // some terms ignore resizes or only change dims at end of resize
                // and some terms don't send this resize "key".  oh well.
                refreshscreen();
                break;
        case KEY_MOUSE:
                getmouse(&mousedata); // curs: get mouse event info
                gi_msx = mousedata.x;
                gi_msy = mousedata.y;
                break;
        default:
                ;
    }
} // handlemouseevents

int TUIprocesskeycommand(keycommand_t cmd, int ch)
{
    return 0;
} // TUIprocesskeycommand

// new version for using new help struct
int TUIprocesschar2(int ch) // returns 1 if user wants to quit
{
    keycommand_t cmd = cmd_plain_char;
    return TUIprocesskeycommand(cmd, ch);
} // TUIprocesschar

int TUIprocesschar(int ch) // returns 1 if user wants to quit
{
    if (ch == -1) return 0; // sent for example while resizing on mac
    if (ch == KEY_RESIZE || ch == KEY_MOUSE) {
        // handle info-providing events independently of mode
        handlemouseevents(ch);
        return 0;
    }
    if (gi_mode == 1) { // 1 means mousing devices
        switch (ch) {
            case KEY_LEFT:
            case 2: // ctrl-b
                    if (gi_editselection != 0) {
                        gi_editselection = 0;
                        refreshscreen();
                        break;
                    }
                    if (gi_selecteddevice > 0)
                        if (--gi_selecteddevice == 0)
                            gi_selecteddevice = gi_numdevices;
                    refreshscreen();
                    break;
            case KEY_RIGHT:
            case 6: // ctrl-f
                    if (gi_editselection != 0) {
                        gi_editselection = 0;
                        refreshscreen();
                        break;
                    }
                    if (gi_selecteddevice > 0)
                        if (++gi_selecteddevice > gi_numdevices)
                            gi_selecteddevice = 1;
                    refreshscreen();
                    break;
            case KEY_UP:
            case 16: // ctrl-p
                    if (gi_selecteddevice != 0) {
                        gi_editselection--;
                        if (gi_editselection < 0)
                            gi_editselection = gi_editableitemcount;
                        refreshscreen();
                    }
                    break;
            case KEY_DOWN:
            case 14: // ctrl-n
                    if (gi_selecteddevice != 0) {
                        gi_editselection++;
                        if (gi_editselection > gi_editableitemcount)
                            gi_editselection = 0;
                        refreshscreen();
                    }
                    break;
            case KEY_ENTER:
            case 13: // ctrl-m, all enter keys on mac send 13
                    if (gi_editobjecttype > 0) {
                        startediting();
                        refreshscreen();
                    } else {
                        beep();
                    }
                    break;
            case 'c': // connect
                    if (gi_device != NULL
                                && gi_device->status == status_inactive) {
                        // TODO: Allow the user to specify which address he/she
                        // would like to use
                        if (!specstatevalid())
                            break;

                        if (gi_device->reachplan.ipaddrs)
                            gi_device->reachplan.whichtouse = gi_device->reachplan.ipaddrs->string;
                        sendmessage(cmd_plug, hq_int, msgtype_connectdevice,
                                    gi_device->deviceid);
                    } else {
                        beep();
                    }
                    break;
            case 'x': // disconnect
                    if (gi_device != NULL
                                && gi_device->status == status_connected) {
                        sendmessage(cmd_plug, hq_int, msgtype_disconnectdevice,
                                    gi_device->deviceid);
                    } else {
                        beep();
                    }
            break;
            case 'u': // unlink a device
                    if (gi_device != NULL && gi_device->linked) {
                        gi_device->linked = 0;
                        free(gi_device->deviceid);
                        gi_device->deviceid = generatedeviceid();
                        gi_dirtyspecs = 1;
                        refreshscreen();
                    } else {
                        beep();
                    }
                    break;
            case 'm': // add device
                    {
                        device **m;
                        int i = 0;
                        for (m = &devicelist; *m != NULL; m = &((*m)->next))
                            i++;
                        *m = (device*) malloc(sizeof(device));
                        (*m)->next = NULL;
                        (*m)->nickname = strdup("newdevice");
                        (*m)->deviceid = generatedeviceid();
                        (*m)->linked = 0;
                        (*m)->status = status_inactive;
                        (*m)->reachplan.ipaddrs = NULL;
                        (*m)->reachplan.routeraddr = -1;
                        gi_selecteddevice = i + 1;
                        gi_editselection = 1;
                        refreshscreen(); // just to set gi_editobject, etc.
                        startediting(); // in order for startediting to work
                    }
                    gi_dirtyspecs = 1;
                    refreshscreen();
                    break;
            case 'a': // add address
                    if (gi_device != NULL) {
                        stringlist **st;
                        int i = 2;
                        st = &(gi_device->reachplan.ipaddrs);
                        while (*st != NULL) {
                            st = &((*st)->next);
                            i++;
                        }
                        *st = (stringlist*) malloc(sizeof(stringlist));
                        (*st)->next = NULL;
                        (*st)->string = strdup("local:~/.mcsync");
                        gi_editselection = i;
                        refreshscreen(); // just to set gi_editobject, etc.
                        startediting();
                        gi_dirtyspecs = 1;
                        refreshscreen();
                    } else {
                        beep();
                    }
                    break;
            case 'g': // add graft
                    if (gi_device != NULL) {
                        graft **gf;
                        gf = &(graftlist);
                        while (*gf != NULL)
                            gf = &((*gf)->next);
                        *gf = (graft*) malloc(sizeof(graft));
                        (*gf)->next = NULL;
                        (*gf)->host = gi_device;
                        (*gf)->hostpath = strdup("~");
                        (*gf)->virtualpath = strdup("/Home");
                        (*gf)->prunepoints = NULL;
                        mapgraftpoint(*gf, (*gf)->virtualpath, 0, 0);
                        gi_editselection = gi_editableitemcount - 1;
                        gi_dirtyspecs = 1;
                        refreshscreen();
                    } else {
                        beep();
                    }
                    break;
            case 'p': // add prune point
                    if (gi_graft != NULL) {
                        stringlist **st;
                        st = &(gi_graft->prunepoints);
                        while (*st != NULL)
                            st = &((*st)->next);
                        *st = (stringlist*) malloc(sizeof(stringlist));
                        (*st)->next = NULL;
                        (*st)->string = strdup("/Home/local");
                        // TODO: Send message to HQ
                        // mapgraftpoint(gi_graft, (*st)->string, 1, 0);
                        gi_dirtyspecs = 1;
                        refreshscreen();
                    } else {
                        beep();
                    }
                    break;
            case 'd': // delete <M>achine / <A>ddress / <G>raft / <P>rune pt
                    gi_mode = 4; // 4 means deleting something
                    refreshscreen(); // get the deleting help on the screen
                    gi_mode = 1; // 1 means mousing devices
                    loop:
                        timeout(1000000);
                        ch = getch();
                        if (ch == KEY_MOUSE || ch == KEY_RESIZE) {
                            handlemouseevents(ch);
                            goto loop;
                        }
                        timeout(-1);
                    switch (ch) {
                        case 'm': // delete device
                            if (gi_device->status != status_inactive)
                                break;
                            if (gi_selecteddevice != 0) {
                                graft **g;
                                device **m;
                                for (g = &graftlist; *g != NULL; ) {
                                    if ((*g)->host == gi_device) {
                                        // trash the graft!
                                        graft *sk = *g;
                                        while (sk->prunepoints != NULL) {
                                            stringlist *skunk=sk->prunepoints;
                                            mapgraftpoint(sk, skunk->string, 1, 1);
                                            sk->prunepoints = skunk->next;
                                            free(skunk->string);
                                            free(skunk);
                                        }
                                        mapgraftpoint(sk, sk->virtualpath, 0, 1);
                                        free(sk->hostpath);
                                        free(sk->virtualpath);
                                        *g = sk->next;
                                        free(sk);
                                    } else {
                                        g = &((*g)->next);
                                    }
                                }
                                for (m = &devicelist; *m != NULL; ) {
                                    if (*m == gi_device) {
                                        // trash the device!
                                        device *sk = *m;
                                        while (sk->reachplan.ipaddrs != NULL) {
                                            stringlist *skunk;
                                            skunk = sk->reachplan.ipaddrs;
                                            sk->reachplan.ipaddrs = skunk->next;
                                            free(skunk->string);
                                            free(skunk);
                                        }
                                        free(sk->nickname);
                                        free(sk->deviceid);
                                        *m = sk->next;
                                        free(sk);
                                    } else {
                                        m = &((*m)->next);
                                    }
                                }
                                if (gi_selecteddevice > 0)
                                    gi_selecteddevice--;

                                gi_dirtyspecs = 1;
                            } else {
                                beep();
                            }
                            break;
                        case 'a': // delete address
                            if (gi_editobjecttype == 3) {
                                stringlist **s = &(gi_device->reachplan.ipaddrs);
                                while (*s != NULL) {
                                    if (&((*s)->string) == gi_editobject) {
                                        stringlist *sk = *s;
                                        free(sk->string);
                                        *s = sk->next;
                                        free(sk);
                                        break;
                                    } else {
                                        s = &((*s)->next);
                                    }
                                }
                                gi_dirtyspecs = 1;
                            } else {
                                beep();
                            }
                            break;
                        case 'g': // delete graft
                            if (gi_editobjecttype >= 4) {
                                graft **g;
                                for (g = &graftlist; *g != NULL; ) {
                                    graft *sk = *g;
                                    stringlist *skunk = sk->prunepoints;
                                    int found = 0;
                                    while (skunk != NULL) {
                                        if (&(skunk->string) == gi_editobject)
                                            found = 1;
                                        skunk = skunk->next;
                                    }
                                    if (&(sk->hostpath) == gi_editobject ||
                                        &(sk->virtualpath) == gi_editobject ||
                                        found == 1) {
                                        // trash the graft!
                                        while (sk->prunepoints != NULL) {
                                            skunk = sk->prunepoints;
                                            mapgraftpoint(sk,
                                                        skunk->string, 1, 1);
                                            sk->prunepoints = skunk->next;
                                            free(skunk->string);
                                            free(skunk);
                                        }
                                        mapgraftpoint(sk,
                                                        sk->virtualpath, 0, 1);
                                        free(sk->hostpath);
                                        free(sk->virtualpath);
                                        *g = sk->next;
                                        free(sk);
                                    } else {
                                        g = &((*g)->next);
                                    }
                                }
                                gi_dirtyspecs = 1;
                            } else {
                                beep();
                            }
                            break;
                        case 'p': // delete prune point
                            if (gi_editobjecttype == 6
                                    && gi_graft != NULL) { // just to be safe
                                stringlist **s = &(gi_graft->prunepoints);
                                while (*s != NULL) {
                                    if (&((*s)->string) == gi_editobject){
                                        stringlist *sk = *s;
                                        mapgraftpoint(gi_graft,
                                                        (*s)->string, 1, 1);
                                        free(sk->string);
                                        *s = sk->next;
                                        free(sk);
                                        break;
                                    } else {
                                        s = &((*s)->next);
                                    }
                                }
                                gi_dirtyspecs = 1;
                            } else {
                                beep();
                            }
                            break;
                        case 'n': // delete nothing
                            break;
                        default:
                            beep();
                    }
                    refreshscreen(); // we have to at least refresh the help
                    break;
            case 's': // save specs file
                    if(specstatevalid()) {
                        writespecsfile(specs_file_path);
                        gi_dirtyspecs = 0;
                    }
                    refreshscreen();
                    break;
            case 'v': // switch to file view (mousing files)
                    // we leave the selection untouched while in other mode
                    gi_mode = 2; // 2 means mousing files
                    refreshscreen();
                    break;
            case 'q': // TODO: Disconnect all devices...
                    return 1;
            case ' ':
            case '\t':
            default:
                    //move(10,10); printw("%d", ch); refresh();
                    beep(); // try for another char next time around
        }
    } else if (gi_mode == 2) { // 2 means mousing files
        switch (ch) {
            case KEY_LEFT:
            case 2: // ctrl-b
                    if (browsingdirectory != NULL
                            && browsingdirectory->selection != NULL
                            && browsingdirectory->selection->prev != NULL) {
                        browsingdirectory->selectionnum--;
                        browsingdirectory->selection =
                                browsingdirectory->selection->prev;
                        refreshscreen();
                    }
                    break;
            case KEY_RIGHT:
            case 6: // ctrl-f
                    if (browsingdirectory != NULL
                            && browsingdirectory->selection != NULL
                            && browsingdirectory->selection->next != NULL) {
                        browsingdirectory->selectionnum++;

                        browsingdirectory->selection =
                                browsingdirectory->selection->next;
                        refreshscreen();
                    }
                    break;
            case KEY_UP:
            case 16: // ctrl-p
                    if (browsingdirectory != NULL
                            && browsingdirectory->selection != NULL
                            && browsingdirectory->selectionnum >=
                                browsingdirectory->cols) {
                        int i = browsingdirectory->cols;
                        browsingdirectory->selectionnum -= i;
                        while (i-- > 0)
                            if ((browsingdirectory->selection =
                                browsingdirectory->selection->prev) == NULL)
                                break;
                        refreshscreen();
                    }
                    break;
            case KEY_DOWN:
            case 14: // ctrl-n
                    if (browsingdirectory != NULL
                            && browsingdirectory->selection != NULL
                            && browsingdirectory->selectionnum +
                                browsingdirectory->cols <
                                browsingdirectory->numchildren) {
                        int i = browsingdirectory->cols;
                        browsingdirectory->selectionnum += i;
                        while (i-- > 0)
                            if ((browsingdirectory->selection =
                                browsingdirectory->selection->next) == NULL)
                                break;
                        refreshscreen();
                    }
                    break;
            case '=':
            case '+': // more columns
                    if (browsingdirectory != NULL
                        && gi_scrx + 1 >= 4 * (browsingdirectory->cols + 1)) {
                        browsingdirectory->cols++;
                        browsingdirectory->firstvisiblenum = -1; // recompute
                        refreshscreen();
                    } else {
                        beep();
                    }
                    break;
            case '_':
            case '-': // fewer columns
                    if (browsingdirectory != NULL
                                && browsingdirectory->cols > 1) {
                        browsingdirectory->cols--;
                        browsingdirectory->firstvisiblenum = -1; // recompute
                        refreshscreen();
                    } else {
                        beep();
                    }
                    break;
            case ' ': // toggle detailed view
                    break;
            case KEY_ENTER: // go to parent dir
            case 13: // ctrl-m, all enter keys on mac send 13
                    if (browsingdirectory->up != NULL) {
                        browsingdirectory = browsingdirectory->up;
                        refreshscreen();
                    } else {
                        beep();
                    }
                    break;
            case '/': // go down into subdirectory
                    if (browsingdirectory->selection != NULL &&
                        browsingdirectory->selection->filetype != 2) {
                        browsingdirectory = browsingdirectory->selection;
                        refreshscreen();
                    } else {
                        beep();
                    }
                    break;
            case 's': // start scans
                    if (browsingdirectory->selection != NULL) {
                        // TODO: SEMAPHORE
                        sendscanvirtualdirrequest(&virtualroot, browsingdirectory->selection);
                    }
                    break;
            case 'v': // switch to mousing devices
                    // we leave the selection untouched while in other mode
                    gi_mode = 1; // 1 means mousing devices
                    refreshscreen();
                    break;
            default:
                    beep();
        }
    } else if (gi_mode == 3) { // 3 means editing

        // Poor mans case range expression, read as case 36 ... 126:
        if(36 <= ch && ch <= 126) {
            {
                int p, len = strlen(gi_newstring);
                gi_newstring = (char*) realloc(gi_newstring, len + 2);
                gi_newstring[len + 1] = 0;
                for (p = len; p > gi_editcursor; p--)
                    gi_newstring[p] = gi_newstring[p - 1];
                gi_newstring[gi_editcursor] = ch;
                gi_editcursor++;
                refreshscreen();
            }
        } else {
            switch (ch) {
                case KEY_LEFT:
                case 2: // ctrl-b, move cursor backward
                        if (gi_editcursor > 0)
                            gi_editcursor--;
                        refreshscreen();
                        break;
                case KEY_RIGHT:
                case 6: // ctrl-f, move cursor forward
                        if (gi_editcursor < strlen(gi_newstring))
                            gi_editcursor++;
                        refreshscreen();
                        break;
                case 1: // ctrl-a, move to beginning of line
                        gi_editcursor = 0;
                        refreshscreen();
                        break;
                case 5: // ctrl-e, move to end of line
                        gi_editcursor = strlen(gi_newstring);
                        refreshscreen();
                        break;
                case KEY_BACKSPACE: // ctrl-h turns into this
                case 8: // in case ctrl-h doesn't change
                case 127: // the delete key
                        if (gi_editcursor == 0)
                            break;
                        gi_editcursor--;
                        // fall through...
                case 4: // ctrl-d, forward-delete
                        {
                            int p, len=strlen(gi_newstring);
                            if (gi_editcursor == len)
                                break;
                            for (p = gi_editcursor; p < len; p++)
                                gi_newstring[p] = gi_newstring[p + 1];
                            gi_newstring[len] = 0;
                            refreshscreen();
                        }
                        break;
                case 11: // ctrl-k, delete to end of line
                        gi_newstring[gi_editcursor] = 0;
                        refreshscreen();
                        break;
                case 7: // ctrl-g, abort editing, revert
                        endediting(0);
                        refreshscreen();
                        break;
                case 9: // tab, KEY_TAB doesn't exist, commit edits and move to next
                        endediting(1);
                        gi_editselection++;
                        refreshscreen(); // find next object to edit
                        startediting();
                        refreshscreen();
                        break;
                case KEY_ENTER: // commit edits
                case 13: // ctrl-m, enter keys on mac all send 13
                        endediting(1);
                        refreshscreen();
                        break;
                default:
                        //move(10,10); printw("%d", ch); refresh();
                        beep();
            } // switch (ch)
        } // case range if
    } // gi_mode
    return 0;
} // TUIprocesschar

void raw_io(void)
{
    initscr(); // curs: get a curses screen (exit on failure)
    start_color(); // curs: we would like to be able to use color
    use_default_colors(); // curs: but we have no wish to force white on black
    setupcolors(); // our own routine
    //cbreak(); // curs: get our input when chars are typed, not waiting for enter
    raw(); // curs: like cbreak but sends ctrl-c, ctrl-z, etc. as chars not sigs
    noecho(); // curs: we'll do our own echoing if necessary
    nonl(); // curs: disables munging of return key, allows it to be detected
    intrflush(stdscr, FALSE); // curs: disable peculiar flushing on interrupt
    nodelay(stdscr, TRUE); // curs: make getch() be non-blocking, ERR == no key
} // raw_io

void TUIstart2D(void)
{
    raw_io();
    keypad(stdscr, TRUE); // curs: have escape sequences converted, e.g. KEY_LEFT
    // bkgdset(ch what is ch?); // curs: set the background to char ch & use attrs
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);

    refreshscreen();
} // TUIinit

void TUIstop2D(void)
{
    endwin(); // curs: get out of visual mode and back to scrolling terminal mode
    // nota bene: if you are in a terminal with a messed up mode, you can type
    // <enter> reset <enter> to get back to the normal scrolling terminal mode
} // TUIshutdown

int doUI = 1; // TODO: Can we remove this?
int password_pause = 0; // signals when input should be allowed to go to ssh

void TUImain(void)
{
    int32 msg_src;
    int64 msg_type;
    char* msg_data;
    int keepgoing = 1;

    if (doUI)
        TUIstart2D(); // configures the terminal for the interface

    browsingdirectory = &virtualroot;

    while (keepgoing) {
        int got_char, got_msg;
        int ch;

        got_char = 0;
        got_msg = 0;

        if (doUI && ! password_pause) {
            ch = getch();
            got_char = (ch != ERR);
            if (got_char) {
                keepgoing = ! TUIprocesschar(ch);
            }
        }

        got_msg = receivemessage(cmd_plug, &msg_src, &msg_type, &msg_data);
        if (got_msg) {
            switch (msg_type) {
                case msgtype_connected:
                {
                    device *d;
                    int devicenum = 1;

                    // device name's color will change with new status
                    printerr("TUI heard that \"%s\" is connected.\n",
                              msg_data);

                    for (d = devicelist; d != NULL; d = d->next) {
                        if (!strcmp(d->deviceid, msg_data)) {
                            break;
                        }
                        devicenum++;
                    }

                    if (d != NULL)
                        gi_selecteddevice = devicenum;

                    free(msg_data);

                    if (doUI)
                        refreshscreen();
                }
                break;
                case msgtype_disconnected:
                        // device name's color will change with new status
                        printerr("TUI heard that \"%s\" is disconnected.\n",
                                        msg_data);
                        free(msg_data);
                        if (doUI)
                            refreshscreen();
                    break;
                case msgtype_scandone:
                    if (doUI)
                        refreshscreen();
                break;
                case msgtype_scanupdate:
                    free(msg_data);
                    if (doUI)
                        refreshscreen();
                break;
                default:
                        printerr("TUI got unexpected message"
                                        " of type %lld from %d: \"%s\"\n",
                                        msg_type, msg_src, msg_data);
                        free(msg_data);
            }
        }

        if (!got_char && !got_msg)
            usleep(pollingrate);
    }

    if (doUI)
        TUIstop2D();
} // TUImain

// end of TUI

//                                             TTTTTT       UU  UU       IIII
//                                               TT         UU  UU        II
//                                               TT         UU  UU        II
//                                               TT          UUUU        IIII

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////// end of TUI //////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

