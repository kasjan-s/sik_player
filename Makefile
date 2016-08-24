TARGET: player master

CXX		:= g++
CXXFLAGS	:= -Wall -O2 -pthread -lboost_regex -std=c++11


player: player.o
	$(CXX) $^ -o $@ $(CXXFLAGS) 

master: master.o
	$(CXX) $^ -o $@ $(CXXFLAGS) 

.PHONY: clean TARGET
clean:
	rm -f player master *.o *~ *.bak
