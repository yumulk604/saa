CXX=g++
CXXFLAGS=- std=c++11 -Wall -O2

all: simple_server

simple_server: simple_server.o
    $(CXX) $(CXXFLAGS) -o simple_server simple_server.o

simple_server.o: simple_server.cpp packet.h
    $(CXX) $(CXXFLAGS) -c simple_server.cpp

clean:
    rm -f simple_server.o simple_server

.PHONY: all clean