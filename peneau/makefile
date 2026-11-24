CXX = g++
CXXFLAGS = -std=c++11 -pthread -MMD -MP
LDLIBS = -lmosquitto

SOURCES := $(wildcard *.cpp)
OBJETS := $(SOURCES:.cpp=.o)

panneau: $(OBJETS)
	$(CXX) -pthread -o $@ $^ $(LDLIBS)

-include $(OBJETS:.o=.d)

.PHONY: clean
clean:
	rm -f panneau $(OBJETS) $(OBJETS:.o=.d)