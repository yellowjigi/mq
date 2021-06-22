#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

struct node {
	long		value;
	struct node	*next;
};

struct queue {
	struct node	*head;
	struct node	*tail;
	int		count;
	pthread_mutex_t	lock;
};

extern struct queue	*init_queue();
extern void		enqueue(struct queue *q, long value);
extern long		dequeue(struct queue *q);
extern void		print_queue(struct queue *q);

#endif // _QUEUE_H_
