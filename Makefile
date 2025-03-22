CXX = g++
CXXFLAGS = -std=c++11 -Wall -g
#LDFLAGS = -lquickfix -lpthread -lxml2
LDFLAGS = -Wl,-rpath,/usr/local/lib -lquickfix -lpthread -lxml2

# Directories
SRCDIR = src
BINDIR = bin

# Source and object files
SOURCES = $(SRCDIR)/SimpleQuickFixApp.cpp
OBJECTS = $(SOURCES:.cpp=.o)
EXECUTABLE = $(BINDIR)/simpleclient

all: prepare $(EXECUTABLE)

prepare:
	mkdir -p $(BINDIR)
	mkdir -p store
	mkdir -p log

$(EXECUTABLE): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)
	rm -rf store/* log/*

.PHONY: all clean prepare
