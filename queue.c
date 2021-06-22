#include "queue.h"

struct queue *init_queue()
{
	struct queue	*q;

	q = malloc(sizeof *q);

	q->head = NULL;
	q->tail = NULL;
	q->count = 0;

	if (pthread_mutex_init(&q->lock, NULL) != 0)
	{
		free(q);
		return NULL;
	}

	return q;
}

static int is_empty(struct queue *q)
{
	return q == NULL || q->count == 0;
}
	
void enqueue(struct queue *q, long value)
{
	struct node	*n;

	pthread_mutex_lock(&q->lock);

	n = malloc(sizeof *n);
	n->value = value;
	n->next = NULL;

	if (is_empty(q))
	{
		q->head = n;
		q->tail = q->head;
	}
	else
	{
		q->tail->next = n;
		q->tail = n;
	}

	q->count++;

	pthread_mutex_unlock(&q->lock);
}

long dequeue(struct queue *q)
{
	struct node	*n;
	long		val;

	pthread_mutex_lock(&q->lock);

	if (is_empty(q))
	{
		pthread_mutex_unlock(&q->lock);
		return -1;
	}
	else
	{
		n = q->head;
		val = n->value;
		q->head = n->next;
		free(n);
	}

	q->count--;

	pthread_mutex_unlock(&q->lock);

	return val;
}

void destroy_queue(struct queue *q)
{
	struct node	*curr;
	struct node	*prev;

	pthread_mutex_lock(&q->lock);

	curr = q->head;

	while (curr)
	{
		prev = curr;
		curr = curr->next;
		free(prev);
	}

	pthread_mutex_unlock(&q->lock);
	pthread_mutex_destroy(&q->lock);

	free(q);
}

void print_queue(struct queue *q)
{
	struct node	*curr;
	long		val;
	unsigned short	file_id;
	unsigned short	ctl_flag;
	unsigned short	msg_id;
	unsigned short	crc16;

	pthread_mutex_lock(&q->lock);

	curr = q->head;

	printf("===========================\n");
	while (curr)
	{
		val = curr->value;
		file_id = (unsigned short)(val >> 48);
		ctl_flag = (unsigned short)(val >> 32);
		msg_id = (unsigned short)(val >> 16);
		crc16 = (unsigned short)(val);
		
		printf("file_id: %hu.\n", file_id);
		printf("ctl_flag: %hu.\n", ctl_flag);
		printf("msg_id: %hu.\n", msg_id);
		printf("crc16: %hu.\n", crc16);

		printf("---------------------------\n");

		curr = curr->next;
	}
	printf("===========================\n");

	pthread_mutex_unlock(&q->lock);
}
