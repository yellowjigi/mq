CC = gcc

FLAGS = \
	-DDEBUG

INCLUDES = -I.

CFLAGS = -g -Wall -Werror -Wno-unused-variable $(FLAGS) $(INCLUDES)

LDFLAGS = -lpthread

MQSEND_SRCS = \
	mqsend.c \
	fileio.c

SHARED = \
	fileio.h

OBJS = $(MQSEND_SRCS:%.c=%.o)

BINS = \
	mqsend
#mqrecv

all:	$(BINS)

#gcc -I${INCLUDE_DIR} -o 
#(cd src; $(MAKE))

mqsend: $(OBJS)
	@echo "target: $@, dep: $(OBJS)"
	$(CC) -o $@ $^ $(LDFLAGS)
#$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(MQSEND_SRCS)
#$(CC) $(CFLAGS) 
#cp mqsend bin

%.o:	%.c $(SHARED)
	@echo "target: $@, dep: $^"
	$(CC) $(CFLAGS) -c -o $@ $<

#mqsend: $(MQSEND_SRCS) bin
#$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $(MQSEND_SRCS)
#$(CC) $(CFLAGS) 
#cp mqsend bin

#bin:
#	if [ ! -d bin ]; then \
#		echo "Creating bin"; \
#		mkdir bin; \
#	fi

clean:
	rm -f $(BINS) $(OBJS)
#(cd src; $(MAKE) clean)

#distclean: clean
#rm -f bin/*
