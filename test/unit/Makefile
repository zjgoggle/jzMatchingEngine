
all: JzMatchingEngine-UnitTest
TARGET=JzMatchingEngine-UnitTest

CSRC=$(wildcard *.cpp)
#DEPS=library

COBJ= $(patsubst %.cpp,%.o,$(CSRC))
CFLAGS=-std=c++20 -g
CFLAGS+=-I../../src
LDFLAGS=$(addprefix -l,$(DEPS))

$(TARGET): $(COBJ)
	$(CXX) -o $@ $(COBJ) $(LDFLAGS)

$(COBJ): %.o: %.cpp
	$(CXX) $(CFLAGS) -o $@ -c $< -DTEST_CONFIG_IMPLEMENT_MAIN

clean:
	rm -f $(COBJ) $(TARGET)

test:
	./$(TARGET)
