# McSync Configuration

## Format of the specs file

The first line gives the file format version number: "version 1"
Then come specifications of machines and grafts:

    machine <nickname>
    <mcsync's working directory>
    <ipaddress> (must not include ':' or ' ')
    <ipaddress> (include as many as you like; only the first will be tried automatically)
    <ipaddress>,<ipaddress> (a line like this will ssh first to one machine and then from there to the second machine)

    <machine>:<directory or file>
    <virtual directory or file to "mount" it at>
    [ignore <virtual directory or file>]*

The same real file can exist in multiple places in the virtual tree.
Multiple real files can exist in the same place in the virtual tree.
However, such multiple files or places cannot be descendants of each other.
Portions of the virtual tree are what you can choose to sync.

"ignore" lines say that a correspondence should be pruned at the specified point.

The virtual root directory cannot have anything mounted at it.
(This is just to prevent naively mounting something there, which would make it awkward to ever sync anything besides what is mounted there.)

The final line just says "end of specs".  All lines after that are ignored.

## The device file

There is a hidden .device file that contains the unique id of the device.
Do not touch it.