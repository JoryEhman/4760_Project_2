CC = gcc
CFLAGS = -Wall -g
TARGETS = oss worker

all: $(TARGETS)

oss: oss.o
	$(CC) $(CFLAGS) -o oss oss.o

worker: worker.o
	$(CC) $(CFLAGS) -o worker worker.o

%.o: %.c shared.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o *.txt $(TARGETS)
