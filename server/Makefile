ifeq ($(CROSS_COMPILE),aarch64-none-linux-gnu-)
	CC = $(CROSS_COMPILE)gcc
else
	CC =gcc
endif

CFLAGS = -Wall -g

DEPS =

OBJ = aesdsocket.o

all: aesdsocket

aesdsocket: $(OBJ)
	$(CC) $(CFLAGS) -o aesdsocket $(OBJ) -lrt -lpthread -pthread

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f aesdsocket *.o
