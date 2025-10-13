
all: JzMatchingEngineMain JzMatchingEngineMain-Test
TARGET=JzMatchingEngineMain

CSRC=$(wildcard *.cpp)
#DEPS=library

COBJ= $(patsubst %.cpp,%.o,$(CSRC))
CFLAGS=-std=c++20 -O3
LDFLAGS=$(addprefix -l,$(DEPS))

$(TARGET): $(COBJ)
	$(CXX) -o $@ $(COBJ) $(LDFLAGS)

$(COBJ): %.o: %.cpp
	$(CXX) $(CFLAGS) -o $@ -c $<

JzMatchingEngineMain-Test: JzMatchingEngine-main.cpp
	$(CXX) -std=c++20 -o JzMatchingEngineMain-Test -g JzMatchingEngine-main.cpp -DTEST_CONFIG_IMPLEMENT_MAIN

clean:
	rm -f $(COBJ) $(TARGET) JzMatchingEngineMain-Test

test:
	./JzMatchingEngineMain-Test
