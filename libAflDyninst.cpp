#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <vector>
#include <algorithm>
#include <sys/types.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "MurmurHash3.h"

#define FORKSRV_FD 198	

using namespace std;
static long savedDi;
register long rdi asm("di");		// the warning is fine - we need the warning because of a bug in dyninst

/* AFL bitmap tracing vars. */
static char * trace_bits;
static long shm_id;
#define SHM_ENV_VAR "__AFL_SHM_ID"

/* Prev block ID for edge tracing. */
static unsigned int prevBlkID;

/* Optional trace path. */
static char* tracePath;

/* Forkserver variant with tracer file descriptors + SHM. */
void forkServer() {
	int temp_data;
	pid_t fork_pid;

	/* Trace path (optional) */
	if (getenv("TRACE_PATH")) {
		tracePath = getenv("TRACE_PATH");
		static FILE *traceFile = fopen(tracePath, "w"); 
		fclose(traceFile);
	}

	/* Skip forkserver (optional) */
	if (getenv("SKIP_FSRV"))
		return;

	/* Set up the SHM bitmap. */
	char *shm_env_var = getenv(SHM_ENV_VAR);
	if (!shm_env_var) {
		perror("Error getenv() SHM\n");
		return;
	}
	shm_id = atoi(shm_env_var);
	trace_bits = (char *) shmat(shm_id, NULL, 0);
	if (trace_bits == (char *) - 1) {
		perror("Error shmat()");
		return;
	}

	/* Tell the parent that we're alive. If the parent doesn't want
		 to talk, assume that we're not running in forkserver mode. */
	if (write(FORKSRV_FD + 1, &temp_data, 4) != 4) {
		perror("ERROR: fork server not running");
		return;
	}
	
	/* All right, let's await orders... */
	while (1) {
		/* Parent - Verify status message length. */
		int stMsgLen = read(FORKSRV_FD, &temp_data, 4);
		if (stMsgLen != 4) {
			/* we use a status message length 2 to terminate the fork server. */
			if(stMsgLen == 2)
				exit(EXIT_SUCCESS);
			perror("Error reading fork server");
			exit(EXIT_FAILURE);
		}

    	/* Parent - Fork off worker process that actually runs the benchmark. */
		fork_pid = fork();
		if (fork_pid < 0) {
      		perror("Fork server fork() failed");
			exit(EXIT_FAILURE);
    	}
		
		/* Child worker - Close descriptors and return (runs the benchmark). */
		if (fork_pid == 0) {
			close(FORKSRV_FD);
			close(FORKSRV_FD + 1);
			return; 
		} 

		/* Parent - Inform controller that we started a new run. */
		if (write(FORKSRV_FD + 1, &fork_pid, 4) != 4) {
    		perror("Fork server write(pid) failed");
			exit(EXIT_FAILURE);
  		}
  		/* Parent - Sleep until child/worker finishes. */
		if (waitpid(fork_pid, &temp_data, 2) < 0) {
    		perror("Fork server waitpid() failed"); 
			exit(EXIT_FAILURE);
  		}
		
		/* Parent - Inform controller that run finished. */
		if (write(FORKSRV_FD + 1, &temp_data, 4) != 4) {
    		perror("Fork server write(temp_data) failed");
			exit(EXIT_FAILURE);
  		}
  		/* Jump back to beginning of this loop and repeat. */
	}
}

/* Basic block trace. */
void traceBlocks(unsigned int curBlkID)
{	
	/* As all bbID's range from 0->infty, we simply
	 * write to bitmap using the bbID as the position. */

	if (trace_bits)
		trace_bits[curBlkID]++;
}

/* Edge trace with modified hash (for hopefully fewer collisions). */
void traceEdges(unsigned int curBlkID){

	/* Given prev and cur integers, we first concatenate 
	 * both integers and then take their 128-bit MurmurHash.
	 * We then obtain the hash's 16 least-significant bits,  
	 * and xor them with cur to calculate the SHM position. 
	 * Finally, we right-shift prev to further reduce the
	 * hash collision likelihood. */

	char str[100];
	sprintf(str, "%d%d", curBlkID, prevBlkID);
	int key = strtol(str, NULL, 10);
	uint64_t hash_otpt[2]= {0};
	MurmurHash3_x64_128(&key, 1, 100, hash_otpt); 
	int pos = (*hash_otpt & ((1L<<16)-1)) ^ curBlkID;
	
	prevBlkID = curBlkID >> 1;

	if (trace_bits)
		trace_bits[pos]++;
}

/* Original AFL edge trace. */
void traceEdgesOrig(unsigned int curBlkID){

	if (trace_bits) {
		trace_bits[prevBlkID ^ curBlkID]++;
		prevBlkID = curBlkID >> 1;
	}
}

void saveRdi() {
	savedDi = rdi;
	/*
	asm("pop %rax"); // take care of rip
	asm("push %rdi");
	asm("push %rax");
	*/
}

void restoreRdi() {
	rdi = savedDi;
	/*
	asm("pop %rax"); // take care of rip
	asm("pop %rdi");
	asm("push %rax");
	*/
}