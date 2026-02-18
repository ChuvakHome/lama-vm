CXX=clang++

INCLUDE_DIRS=deps/
CXXFLAGS=-std=c++20 -g -O2

LAMA_RUNTIME_DIR=deps/Lama/runtime
LAMA_RUNTIME=$(LAMA_RUNTIME_DIR)/runtime.a
LDFLAGS=

OS_NAME=$(shell uname -s)

ifneq ($(OS_NAME), Darwin)
LDFLAGS+=-Wl,--defsym=__start_custom_data=0 -Wl,--defsym=__stop_custom_data=0
endif

EXECUTABLE=lama-util

SOURCES=$(shell find src -type f -name "*.cpp")
OBJECTS=$(SOURCES:.cpp=.o)

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) $(LAMA_RUNTIME)
	$(CXX) $(LDFLAGS) -o $@ $^

.cpp.o:
	$(CXX) -I$(INCLUDE_DIRS) $(CXXFLAGS) -c -o $@ $<

$(LAMA_RUNTIME):
	make -C $(LAMA_RUNTIME_DIR)

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)
