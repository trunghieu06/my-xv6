#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "kernel/sysinfo.h"
#include "user/user.h"

static struct sysinfo info;

static void
check(int ok, char *message)
{
  if(!ok){
    printf("sysinfotest: %s\n", message);
    exit(1);
  }
}

static int
find_proc(int pid)
{
  int i;

  for(i = 0; i < info.proc_count; i++)
    if(info.procs[i].pid == pid)
      return i;
  return -1;
}

static int
file_has_pid(struct sysinfo_file *f, int pid)
{
  int i;

  for(i = 0; i < f->pid_count; i++)
    if(f->pids[i] == pid)
      return 1;
  return 0;
}

static int
read_info(void)
{
  return sysinfo(&info) == 0;
}

int
main(void)
{
  int pid, fd, dupfd, ready[2], release[2], child_index, parent_index;
  int file_index, token = 1, received = 0;
  int initial_file_count;
  struct sysinfo_file *f, *pipe_file;

  check(sysinfo((struct sysinfo *)0) < 0, "accepted an invalid destination");
  check(read_info(), "initial query failed");
  parent_index = find_proc(getpid());
  check(parent_index >= 0, "current process missing");
  check(info.proc_count > 0, "empty process list");
  check(info.procs[parent_index].state == SYSINFO_PROC_RUNNING,
        "incorrect current state");
  check(info.procs[parent_index].sched_count > 0, "missing schedule count");
  initial_file_count = info.file_count;

  fd = open("sysinfo.tmp", O_CREATE | O_RDWR);
  check(fd >= 0, "open failed");
  dupfd = dup(fd);
  check(dupfd >= 0, "dup failed");
  check(read_info(), "query after open failed");
  parent_index = find_proc(getpid());
  check(parent_index >= 0, "current process missing after open");
  file_index = info.procs[parent_index].open_files[fd];
  check(file_index >= 0 && file_index < info.file_count, "open file missing");
  check(info.procs[parent_index].open_files[dupfd] == file_index,
        "dup does not share the open file");
  f = &info.files[file_index];
  check(f->type == SYSINFO_FD_INODE, "incorrect file type");
  check(f->mode == (SYSINFO_READABLE | SYSINFO_WRITABLE), "incorrect file mode");
  check(f->ref == 2, "incorrect reference count after dup");
  check(f->pid_count == 1 && file_has_pid(f, getpid()),
        "incorrect process references after dup");

  check(pipe(ready) == 0, "ready pipe failed");
  check(pipe(release) == 0, "release pipe failed");
  pid = fork();
  check(pid >= 0, "fork failed");
  if(pid == 0){
    close(ready[0]);
    close(release[1]);
    write(ready[1], &token, sizeof(token));
    read(release[0], &received, sizeof(received));
    exit(0);
  }
  close(ready[1]);
  close(release[0]);
  check(read(ready[0], &received, sizeof(received)) == sizeof(received),
        "child synchronization failed");
  check(received == token, "incorrect child synchronization value");
  check(read_info(), "query after fork failed");
  parent_index = find_proc(getpid());
  child_index = find_proc(pid);
  check(parent_index >= 0 && child_index >= 0, "process missing after fork");
  check(info.procs[child_index].parent_pid == getpid(), "incorrect parent pid");
  check(info.procs[child_index].state == SYSINFO_PROC_SLEEPING,
        "incorrect child state");
  check(info.procs[child_index].sched_count > 0, "missing child schedule count");
  file_index = info.procs[parent_index].open_files[fd];
  check(file_index >= 0, "parent file missing after fork");
  f = &info.files[file_index];
  check(f->ref == 4, "incorrect reference count after fork");
  check(f->pid_count == 2 && file_has_pid(f, getpid()) && file_has_pid(f, pid),
        "incorrect process references after fork");
  check(info.procs[child_index].open_files[fd] == file_index,
        "child file mapping incorrect");
  file_index = info.procs[child_index].open_files[release[0]];
  check(file_index >= 0, "child read pipe missing");
  pipe_file = &info.files[file_index];
  check(pipe_file->type == SYSINFO_FD_PIPE &&
        pipe_file->mode == SYSINFO_READABLE,
        "incorrect read pipe information");
  check(pipe_file->ref == 1 && pipe_file->pid_count == 1 &&
        file_has_pid(pipe_file, pid),
        "incorrect read pipe references");
  file_index = info.procs[child_index].open_files[ready[1]];
  check(file_index >= 0, "child write pipe missing");
  pipe_file = &info.files[file_index];
  check(pipe_file->type == SYSINFO_FD_PIPE &&
        pipe_file->mode == SYSINFO_WRITABLE,
        "incorrect write pipe information");
  check(pipe_file->ref == 1 && pipe_file->pid_count == 1 &&
        file_has_pid(pipe_file, pid),
        "incorrect write pipe references");

  check(write(release[1], &token, sizeof(token)) == sizeof(token),
        "child release failed");
  close(ready[0]);
  close(release[1]);
  check(wait(0) == pid, "wait failed");
  check(read_info(), "query after child exit failed");
  parent_index = find_proc(getpid());
  check(parent_index >= 0 && find_proc(pid) < 0, "exited child still listed");
  file_index = info.procs[parent_index].open_files[fd];
  check(file_index >= 0 && info.files[file_index].ref == 2,
        "incorrect reference count after child exit");
  close(dupfd);
  close(fd);
  unlink("sysinfo.tmp");
  check(read_info(), "query after close failed");
  parent_index = find_proc(getpid());
  check(parent_index >= 0, "current process missing after close");
  check(info.procs[parent_index].open_files[fd] == -1 &&
        info.procs[parent_index].open_files[dupfd] == -1,
        "closed file descriptors still listed");
  check(info.file_count == initial_file_count, "file count did not return to baseline");
  printf("sysinfotest: OK (%d files, %d processes)\n",
         info.file_count, info.proc_count);
  exit(0);
}
