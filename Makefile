CC = gcc

FLAGS =
#	-DDEBUG

INCLUDES = -I.

CFLAGS = -g -Wall -Werror -Wno-unused-variable $(FLAGS) $(INCLUDES)

LDFLAGS = -lpthread

MQSEND_SRCS = \
	mqsend.c \
	fileio.c

MQRECV_SRCS = \
	mqrecv.c \
	fileio.c

SHARED = \
	fileio.h

MQSEND_OBJS = $(MQSEND_SRCS:%.c=%.o)

MQRECV_OBJS = $(MQRECV_SRCS:%.c=%.o)

BINS = \
	mqsend \
	mqrecv

all:	$(BINS)

mqsend: $(MQSEND_OBJS)
	@echo "target: $@, dep: $(OBJS)"
	$(CC) -o $@ $^ $(LDFLAGS)

mqrecv: $(MQRECV_OBJS)
	@echo "target: $@, dep: $(OBJS)"
	$(CC) -o $@ $^ $(LDFLAGS)

%.o:	%.c $(SHARED)
	@echo "target: $@, dep: $^"
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(BINS) $(MQSEND_OBJS) $(MQRECV_OBJS)
