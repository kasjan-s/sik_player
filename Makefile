TARGET: player

CC	= g++
CFLAGS	= -Wall -O2 -std=c++11
LFLAGS	= -Wall -pthread


player: player.o
	$(CC) $(LFLAGS) $^ -o $@

.PHONY: clean TARGET
clean:
	rm -f player *.o *~ *.bak
