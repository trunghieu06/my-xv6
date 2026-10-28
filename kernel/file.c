//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "sysinfo.h"

struct devsw devsw[NDEV];
extern struct proc proc[NPROC];
extern struct spinlock wait_lock;
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;
static struct spinlock sysinfo_lock;
static struct sysinfo sysinfo_data;
static struct file *sysinfo_files[NFILE];
static int sysinfo_file_refs[NFILE];

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
  initlock(&sysinfo_lock, "sysinfo");
}

static int
sysinfo_file_index(struct file *f)
{
  int i;

  for(i = 0; i < sysinfo_data.file_count; i++)
    if(sysinfo_files[i] == f)
      return i;
  return -1;
}

uint64
sys_sysinfo(void)
{
  int i, j, fd, index, parent_pid, release_count;
  uint64 addr;
  struct proc *p, *parent;
  struct file *f;
  struct file *release_files[NFILE];
  struct sysinfo_proc *pi;
  struct sysinfo_file *fi;
  int result;

  argaddr(0, &addr);
  acquire(&sysinfo_lock);
  memset(&sysinfo_data, 0, sizeof(sysinfo_data));
  memset(sysinfo_files, 0, sizeof(sysinfo_files));
  memset(sysinfo_file_refs, 0, sizeof(sysinfo_file_refs));

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&wait_lock);
    acquire(&p->lock);
    if(p->state == UNUSED){
      release(&p->lock);
      release(&wait_lock);
      continue;
    }

    pi = &sysinfo_data.procs[sysinfo_data.proc_count++];
    pi->pid = p->pid;
    pi->state = p->state;
    pi->sched_count = p->sched_count;
    parent = p->parent;
    for(fd = 0; fd < NOFILE; fd++){
      pi->open_files[fd] = -1;
      f = p->ofile[fd];
      if(f == 0)
        continue;
      index = sysinfo_file_index(f);
      if(index < 0){
        index = sysinfo_data.file_count++;
        sysinfo_files[index] = filedup(f);
        sysinfo_file_refs[index] = 1;
      }
      pi->open_files[fd] = index;
      fi = &sysinfo_data.files[index];
      for(j = 0; j < fi->pid_count; j++)
        if(fi->pids[j] == p->pid)
          break;
      if(j == fi->pid_count)
        fi->pids[fi->pid_count++] = p->pid;
    }
    release(&p->lock);
    parent_pid = 0;
    if(parent){
      acquire(&parent->lock);
      parent_pid = parent->pid;
      release(&parent->lock);
    }
    pi->parent_pid = parent_pid;
    release(&wait_lock);
  }

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0)
      continue;
    index = sysinfo_file_index(f);
    if(index < 0){
      index = sysinfo_data.file_count++;
      sysinfo_files[index] = f;
      f->ref++;
      sysinfo_file_refs[index] = 1;
    }
    fi = &sysinfo_data.files[index];
    fi->type = f->type;
    fi->mode = (f->readable ? SYSINFO_READABLE : 0) |
               (f->writable ? SYSINFO_WRITABLE : 0);
    fi->ref = f->ref - sysinfo_file_refs[index];
  }
  release(&ftable.lock);

  release_count = sysinfo_data.file_count;
  for(i = 0; i < release_count; i++)
    release_files[i] = sysinfo_files[i];

  result = copyout(myproc()->pagetable, addr, (char *)&sysinfo_data,
                   sizeof(sysinfo_data));
  release(&sysinfo_lock);
  for(i = 0; i < release_count; i++)
    fileclose(release_files[i]);
  return result < 0 ? -1 : 0;
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}
