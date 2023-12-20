ifeq ($(CROSS_COMPILE),aarch64-none-linux-gnu-)
	CC ?=$(CROSS_COMPILE)gcc
else
	CC ?=gcc
endif
LDFLAGS ?= -lpthread -lrt

CFLAGS = -Wall -g

DEPS =

OBJ = aesdsocket.o

all: aesdsocket

aesdsocket: $(OBJ)
	$(CC) $(CFLAGS) -o aesdsocket $(OBJ) $(LDFLAGS)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f aesdsocket *.o
