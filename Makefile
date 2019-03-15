# AFLDyninst vars - edit DYN_ROOT accordingly

DYN_ROOT 	= /home/osboxes/Desktop/dynBuildDir
CC 			= gcc 
CXX 		= g++
CXXFLAGS 	= -g -Wall -O3 -std=c++11
LIBFLAGS 	= -fpic -shared
LDFLAGS 	= -I/usr/include -I$(DYN_ROOT)/include -L$(DYN_ROOT)/lib -lcommon -liberty -ldyninstAPI -lboost_system

all: AFLDyninst libAFLDyninst

libAFLDyninst: libAFLDyninst.cpp
	$(CXX) $(CXXFLAGS) -o libAFLDyninst.so libAFLDyninst.cpp $(LDFLAGS) $(LIBFLAGS) MurmurHash3.cpp

AFLDyninst: AFLDyninst.cpp
	$(CXX) -Wl,-rpath-link,$(DYN_ROOT)/lib -Wl,-rpath-link,$(DYN_ROOT)/include $(CXXFLAGS) -o AFLDyninst AFLDyninst.cpp $(LDFLAGS)

clean:
	rm -rf AFLDyninst libAFLDyninst.so *.o 