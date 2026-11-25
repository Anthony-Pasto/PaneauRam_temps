CXX = g++
CXXFLAGS = -std=c++11 -pthread -MMD -MP
LDLIBS = -lmosquitto

SOURCES := $(wildcard *.cpp)
OBJETS := $(SOURCES:.cpp=.o)

powermeteur: $(OBJETS)
	$(CXX) -pthread -o $@ $^ $(LDLIBS)

-include $(OBJETS:.o=.d)

.PHONY: clean
clean:
	rm -f powermeteur $(OBJETS) $(OBJETS:.o=.d)
