TARGET: player

CXX		:= g++
CXXFLAGS	:= -Wall -O2 -pthread -std=c++11


player: player.o
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: clean TARGET
clean:
	rm -f player *.o *~ *.bak
