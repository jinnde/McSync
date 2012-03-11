# McSync

** McSync is a file synchronizer based on distributed version lists. **

Read `docs/QuickStart.txt` to start syncing right away, or read
`docs/UserGuide.txt` to understand what you can do with McSync.

The directory containing this file is McSync's private working directory. It is NOT a place to put your own files.
It contains the full distribution of McSync.

The directory containing this file can have any name and go anywhere, for example ~/.mcsync is the most common place and name.
McSync is launched by the executable "mcsync" in the "source" subdirectory of this directory, so you may want that, or a link to it, to be on your path.

There must be a copy of this directory on each machine or storage device containing files to be synced.  McSync will set this up for you.

It has the following contents, which (except for "docs") are all needed by McSync:

    bin/                compiled binaries

    config/             configuration files

    docs/               documentation files

    data/               where mcsync stores its latest info about all the user files
                        it syncs on this and other devices

    source/             the sources and scripts with which to compile and run mcsync