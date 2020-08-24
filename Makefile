# AFLDyninst vars - edit DYN_ROOT accordingly

DYN_ROOT 	= /home/osboxes/Desktop/dynBuildDir10
CC 			= gcc 
CXX 		= g++
CXXFLAGS 	= -g -Wall -O3 -std=c++11
LIBFLAGS 	= -fpic -shared
LDFLAGS 	= -I/usr/include -I$(DYN_ROOT)/include -L$(DYN_ROOT)/lib -lcommon -liberty -ldyninstAPI -lboost_system

all: AFLDyninst libAflDyninst

libAflDyninst: libAflDyninst.cpp
	$(CXX) $(CXXFLAGS) -o libAflDyninst.so libAflDyninst.cpp $(LDFLAGS) $(LIBFLAGS)

AFLDyninst: AFLDyninst.cpp
	$(CXX) -Wl,-rpath-link,$(DYN_ROOT)/lib -Wl,-rpath-link,$(DYN_ROOT)/include $(CXXFLAGS) -o AFLDyninst AFLDyninst.cpp $(LDFLAGS)

clean:
	rm -rf AFLDyninst libAflDyninst.so *.o 
