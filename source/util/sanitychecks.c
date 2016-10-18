#include "definitions.h"


//////////////////////////////////////////////////////////////////////////////////
///////////////////////////////// sanity  checks /////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////


int checktypes(void)
{
    int ret = 1;
    {
        int32 j = 1;
        int i = 0;
        while (j) {
            i++;
            j <<= 1;
        }
        if (i != 32) {
            printf("Error: type int32 has %d bits instead of 32\n", i);
            ret = 0;
        }
    }
    {
        int64 j = 1;
        int i = 0;
        while (j) {
            i++;
            j <<= 1;
        }
        if (i != 64) {
            printf("Error: type int64 has %d bits instead of 64\n", i);
            ret = 0;
        }
    }
    return ret;
} // checktypes
