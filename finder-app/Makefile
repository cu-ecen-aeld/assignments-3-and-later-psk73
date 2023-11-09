ifeq ($(CROSS_COMPILE),aarch64-none-linux-gnu-)
	CC = $(CROSS_COMPILE)gcc
else
	CC =gcc
endif

CFLAGS = -Wall -g

DEPS =

OBJ = writer.o

all: writer

writer: $(OBJ)
	$(CC) $(CFLAGS) -o writer $(OBJ)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f writer *.o
