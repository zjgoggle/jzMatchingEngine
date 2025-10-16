
all: SimpleMatchingEngineMain SimpleMatchingEngineMain-Test
TARGET=SimpleMatchingEngineMain

CSRC=$(wildcard *.cpp)
#DEPS=library

COBJ= $(patsubst %.cpp,%.o,$(CSRC))
CFLAGS=-std=c++20 -O3
LDFLAGS=$(addprefix -l,$(DEPS))

$(TARGET): $(COBJ)
	$(CXX) -o $@ $(COBJ) $(LDFLAGS)

$(COBJ): %.o: %.cpp
	$(CXX) $(CFLAGS) -o $@ -c $<

SimpleMatchingEngineMain-Test: SimpleMatchingEngine-main.cpp
	$(CXX) -std=c++20 -o SimpleMatchingEngineMain-Test -g SimpleMatchingEngine-main.cpp -DTEST_CONFIG_IMPLEMENT_MAIN

clean:
	rm -f $(COBJ) $(TARGET) SimpleMatchingEngineMain-Test

test:
	./SimpleMatchingEngineMain-Test
