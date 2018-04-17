DYN_ROOT = /usr/local
CXX = g++
CXXFLAGS = -g -Wall -O3 -std=c++11
LIBFLAGS = -fpic -shared
LDFLAGS = -I/usr/include -I$(DYN_ROOT)/include -L$(DYN_ROOT)/lib -lcommon -liberty -ldyninstAPI 

all:  afl-dyninst libAflDyninst.so

afl-dyninst: afl-dyninst.cpp
	$(CXX) $(CXXFLAGS) -o afl-dyninst afl-dyninst.cpp $(LDFLAGS)

libAflDyninst.so: libAflDyninst.cpp
	$(CXX) $(CXXFLAGS) -o libAflDyninst.so libAflDyninst.cpp $(LDFLAGS) $(LIBFLAGS)

install:
	install afl-dyninst $(DYN_ROOT)/bin
	install libAflDyninst.so $(DYN_ROOT)/lib

clean:
	rm -f afl-dyninst libAflDyninst.so *.o 