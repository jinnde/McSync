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

stringlist* stringlistcontains(stringlist *s, char *str)
{
    stringlist* listitem = s;

    while (listitem != NULL) {
        if (! strcmp(listitem->string, str))
            return listitem;
        listitem = listitem->next;
    }
    return NULL;
} // stringlistcontains
