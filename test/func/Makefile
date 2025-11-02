
all: NasdaqITCHReader
TARGET=NasdaqITCHReader

CSRC=$(wildcard *.cpp)
#DEPS=library

COBJ= $(patsubst %.cpp,%.o,$(CSRC))
CFLAGS=-std=c++20 -g -O3 -DNDEBUG
CFLAGS+=-I../../src -I../../external/pfr/include
LDFLAGS=$(addprefix -l,$(DEPS))

$(TARGET): $(COBJ)
	$(CXX) -o $@ $(COBJ) $(LDFLAGS)

$(COBJ): %.o: %.cpp
	$(CXX) $(CFLAGS) -o $@ -c $< # -DTEST_CONFIG_IMPLEMENT_MAIN

clean:
	rm -f $(COBJ) $(TARGET)

test:
	./$(TARGET)
