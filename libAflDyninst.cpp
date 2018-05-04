#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <cstring>
#include <vector>
#include <algorithm>
#include "config.h"
#include <sys/types.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;

static u8 *trace_bits;
static s32 shm_id;
static int __afl_temp_data;
static pid_t __afl_fork_pid;
static unsigned short prev_id;
static long saved_di;
register long rdi asm("di");    // the warning is fine - we need the warning because of a bug in dyninst

void initAflForkServer() {

  char *shm_env_var = getenv(SHM_ENV_VAR);

  if (!shm_env_var) {
    perror("Error getting shm\n");
    exit(EXIT_FAILURE);
    return;
  }
  
  shm_id = atoi(shm_env_var);

  trace_bits = (u8 *) shmat(shm_id, NULL, 0);
  if (trace_bits == (u8 *) - 1) {
    perror("shmat");
    exit(EXIT_FAILURE);
    return;
  }
  
  // enter fork() server thyme!
  int n = write(FORKSRV_FD + 1, &__afl_temp_data, 4);

  if (n != 4) {
    perror("Error writing fork server\n");
    exit(EXIT_FAILURE);
    return;
  }
  while (1) {
    n = read(FORKSRV_FD, &__afl_temp_data, 4);
    if (n != 4) {
      printf("Error reading fork server %x\n", __afl_temp_data);
      exit(EXIT_FAILURE);
      return;
    }

    __afl_fork_pid = fork();
    if (__afl_fork_pid < 0) {
      perror("Error on fork()\n");
      exit(EXIT_FAILURE);
      return;
    }

    // Child/worker
    if (__afl_fork_pid == 0) {
      close(FORKSRV_FD);
      close(FORKSRV_FD + 1);
      return; // run benchmarkbreak;
    }
    
    // parrent stuff
    // Inform controller that we started a new run
    if (write(FORKSRV_FD + 1, &__afl_fork_pid, 4) != 4) {
      perror("Fork server write(pid) failed");
      exit(EXIT_FAILURE);
    }
    
    // Sleep until child/worker finishes
    if (waitpid(__afl_fork_pid, &__afl_temp_data, 0) < 0) {
      perror("Fork server waitpid() failed"); 
      exit(EXIT_FAILURE);
    }
    
    // Inform controller that run finished
    if (write(FORKSRV_FD + 1, &__afl_temp_data, 4) != 4) {
      perror("Fork server write(temp_data) failed");
      exit(EXIT_FAILURE);
    }
  } // while(1)
}

// Should be called on basic block entry
void bbCallback(unsigned short id) {
  if (trace_bits) {
    trace_bits[prev_id ^ id]++;
    prev_id = id >> 1;
  }
}

void save_rdi() {
  saved_di = rdi;
/*
  asm("pop %rax"); // take care of rip
  asm("push %rdi");
  asm("push %rax");
*/
}

void restore_rdi() {
  rdi = saved_di;
/*
  asm("pop %rax"); // take care of rip
  asm("pop %rdi");
  asm("push %rax");
*/
}
