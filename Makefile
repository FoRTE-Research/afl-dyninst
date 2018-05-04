DYN_ROOT = /home/mdhicks2/Desktop/dynInstall
CXX = g++
CXXFLAGS = -g -Wall -O3 -std=c++11
LIBFLAGS = -fpic -shared
LDFLAGS = -I/usr/include -I$(DYN_ROOT)/include -L$(DYN_ROOT)/lib -lcommon -liberty -ldyninstAPI 

all:  afl-dyninst libAflDyninst.so instrument

afl-dyninst: afl-dyninst.cpp
	$(CXX) $(CXXFLAGS) -o afl-dyninst afl-dyninst.cpp $(LDFLAGS)

libAflDyninst.so: libAflDyninst.cpp
	$(CXX) $(CXXFLAGS) -o libAflDyninst.so libAflDyninst.cpp $(LDFLAGS) $(LIBFLAGS)

instrument:
	./afl-dyninst -i djpeg -o djpegInst -v

clean:
	rm -f afl-dyninst libAflDyninst.so *.o 
