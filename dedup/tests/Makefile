CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

INCLUDES = -I../include/chunking -I../include/config -I../include/hashing
SOURCES = metadata_counting_test.cpp \
	../src/chunking/chunking_common.cpp \
	../src/chunking/mincdc_chunking.cpp \
	../src/config/config.cpp \
	../src/config/config_error.cpp \
	../src/config/parser.cpp \
	../src/hashing/hash.cpp
TARGET = metadata_counting_test

.PHONY: test clean

test: $(TARGET)
	./$(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o $@

clean:
	rm -f $(TARGET)
