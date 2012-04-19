#include "definitions.h"

void freestringlist(stringlist *skunk)
{
    stringlist *temp;

    while (skunk) {
        temp = skunk;
        skunk = skunk->next;
        free(temp->string);
        free(temp);
    }
} // freestringlist
