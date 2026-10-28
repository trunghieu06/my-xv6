#ifndef _SYSINFO_H_
#define _SYSINFO_H_

#include "param.h"

#define SYSINFO_FD_NONE 0
#define SYSINFO_FD_PIPE 1
#define SYSINFO_FD_INODE 2
#define SYSINFO_FD_DEVICE 3

#define SYSINFO_READABLE 1
#define SYSINFO_WRITABLE 2

#define SYSINFO_PROC_UNUSED 0
#define SYSINFO_PROC_USED 1
#define SYSINFO_PROC_SLEEPING 2
#define SYSINFO_PROC_RUNNABLE 3
#define SYSINFO_PROC_RUNNING 4
#define SYSINFO_PROC_ZOMBIE 5

struct sysinfo_file {
  int type;
  int mode;
  int ref;
  int pid_count;
  int pids[NPROC];
};

struct sysinfo_proc {
  int pid;
  int state;
  int parent_pid;
  int sched_count;
  int open_files[NOFILE];
};

struct sysinfo {
  int file_count;
  struct sysinfo_file files[NFILE];
  int proc_count;
  struct sysinfo_proc procs[NPROC];
};

#endif
