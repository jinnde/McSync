McSync is a file synchronizer.

Read docs/Quick_Start.txt to start syncing right away,
or read docs/User_Guide.txt to understand what you can do with McSync.

The directory containing this file is McSync's private working directory.
It contains the full distribution of McSync.
It is NOT a place to put your own files.

To install McSync, copy this directory to somewhere on the drive
(computer hard drive, usb drive, etc.) containing the files you
want to keep synchronized.
For example, ~/.mcsync is a common place/name for this directory.
Once a copy of this directory is there, installation is complete.

McSync is launched by the executable "mcsync" in this directory,
so you might want that, or a link to it, to be on your path.

Each drive with files to synchronize needs a copy of this directory.
McSync can help set this up for you.

The contents of this directory (except for "docs") are all needed by McSync:

    bin/                compiled binaries

    config/             configuration files and preferences

    docs/               documentation files

    data/               metadata about files synced on this and other devices

    source/             sources needed for compiling mcsync
