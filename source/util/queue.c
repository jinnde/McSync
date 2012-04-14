#include "definitions.h"

queue initqueue() // free when done
{
    queue q = (queue) malloc(sizeof(struct queue_struct));
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    return q;
} // initqueue

void freequeue(queue q) // does not free the data!!
{
    queuenode current = q->head;
    queuenode temp;

    while (current != NULL) {
        temp = current;
        current = current->next;
        free(temp);
    }
    free(q);
    q = NULL;
} // freeserializable

void *queueremove(queue q, queuenode qn) // does not free the data!!
{
    void *data;

    if (qn->prev == NULL)
        q->head = qn->next;
    else
        qn->prev->next = qn->next;

    if (qn->next == NULL)
        q->tail = qn->prev;
    else
        qn->next->prev = qn->prev;

    data = qn->data;
    free(qn);
    q->size--;
    return data;
} // queueremove

void queueinsertafter(queue q, queuenode qn, void *data)
{
    queuenode nqn = (queuenode) malloc(sizeof(struct queuenode_struct));
    nqn->data = data;
    q->size++;

    nqn->prev = qn;
    nqn->next = qn->next;
    if (qn->next == NULL)
        q->tail = nqn;
    else
        qn->next->prev = nqn;
    qn->next = nqn;
} // queueinsertafter

void queueinsertbefore(queue q, queuenode qn, void *data)
{
    queuenode nqn = (queuenode) malloc(sizeof(struct queuenode_struct));
    nqn->data = data;
    q->size++;

    nqn->prev = qn->prev;
    nqn->next = qn;
    if (qn->prev == NULL)
        q->head = nqn;
    else
        qn->prev->next = nqn;
    qn->prev = nqn;
} // queueinsertbefore

void queueinserthead(queue q, void *data)
{
    if (q->head == NULL) {
        queuenode nqn = (queuenode) malloc(sizeof(struct queuenode_struct));
        nqn->data = data;
        q->size++;
        q->head = nqn;
        q->tail = nqn;
        nqn->prev = NULL;
        nqn->next = NULL;
    } else {
        queueinsertbefore(q, q->head, data);
    }
} // queueinserthead

void queueinserttail(queue q, void *data)
{
    if (q->tail == NULL)
        queueinserthead(q, data);
    else
        queueinsertafter(q, q->tail, data);
} // queueinsertend
