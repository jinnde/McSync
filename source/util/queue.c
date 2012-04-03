#include "definitions.h"

queue initqueue() // free when done
{
    queue q = (queue) malloc(sizeof(struct queue_struct));
    q->head = NULL;
    q->tail = NULL;
    q->len = 0;
    return q;
} // initqueue

void freequeue(queue q)
{
    queuenode current = q->head;
    queuenode temp;

    while (current != NULL) {
        temp = current;
        current = current->next;
        free(temp);
    }
    free(q);
} // freeserializable

void queueinsertafter(queue q, queuenode qn, void *data, int32 len)
{
    queuenode nqn = (queuenode) malloc(sizeof(struct queuenode_struct));
    nqn->data = data;
    nqn->len = len;
    q->len += len;

    nqn->prev = qn;
    nqn->next = qn->next;
    if (qn->next == NULL)
        q->tail = nqn;
    else
        qn->next->prev = nqn;
    qn->next = nqn;
} // queueinsertafter

void queueinsertbefore(queue q, queuenode qn, void *data, int32 len)
{
    queuenode nqn = (queuenode) malloc(sizeof(struct queuenode_struct));
    nqn->data = data;
    nqn->len = len;
    q->len += len;

    nqn->prev = qn->prev;
    nqn->next = qn;
    if (qn->prev == NULL)
        q->head = nqn;
    else
        qn->prev->next = nqn;
    qn->prev = nqn;
} // queueinsertbefore

void queueinserthead(queue q, void *data, int32 len)
{
    if (q->head == NULL) {
        queuenode nqn = (queuenode) malloc(sizeof(struct queuenode_struct));
        nqn->data = data;
        nqn->len = len;
        q->len += len;
        q->head = nqn;
        q->tail = nqn;
        nqn->prev = NULL;
        nqn->next = NULL;
    } else {
        queueinsertbefore(q, q->head, data, len);
    }
} // queueinserthead

void queueinserttail(queue q, void *data, int32 len)
{
    if (q->tail == NULL)
        queueinserthead(q, data, len);
    else
        queueinsertafter(q, q->tail, data, len);
} // queueinsertend