CC = gcc

FLAGS =
#	-DDEBUG

INCLUDES = -I.

CFLAGS = -g -Wall -Werror -Wno-unused-variable $(FLAGS) $(INCLUDES)

LDFLAGS = -lpthread

MQSEND_SRCS = \
	mqsend.c \
	fileio.c \
	crc.c \
	queue.c \
	msq.c

MQRECV_SRCS = \
	mqrecv.c \
	fileio.c \
	crc.c \
	msq.c

SHARED = \
	msq.h \
	fileio.h \
	crc.h \
	queue.h

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

#bin:
#	if [ ! -d bin ]; then \
#		echo "Creating bin"; \
#		mkdir bin; \
#	fi

clean:
	rm -f $(BINS) $(MQRECV_OBJS) $(MQSEND_OBJS) RecevedFileA.bin RecevedFileB.bin RecevedFileC.bin

#distclean: clean
#rm -f bin/*
