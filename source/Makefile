
#VPATH = src/projects:src/common:src/guilib:bin

filename = kmeans

# instead of putting the command on the next line, starting with a tab,
# we put it on the same line, separated by a semicolon,
# and we use an @ to keep it from echoing itself which it does without cleaning up the \'s

#  target : prerequisites ; @ command to do if any prerequisite is newer than target

#run : bin/${filename}                   \
#    ; @                                 \
#    echo running ${filename} ;          \
#    killall ${filename} ;               \
#    ( bin/${filename} & ) ;             \

mcsync : definitions.h engine/*.c tui/*.c ; @ echo compiling mcsync ; cc -O -g -o ../bin/mcsync -lncurses -Wall -I. engine/*.c tui/*.c
