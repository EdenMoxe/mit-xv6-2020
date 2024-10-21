//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "memlayout.h"

#define errlog(error) do{printf(error);return -1;}while(0);

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    dp->nlink++;  // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if(dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  if((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if((argstr(0, path, MAXPATH)) < 0 ||
     argint(1, &major) < 0 ||
     argint(2, &minor) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

//Quick Sort for vma start_addr
int getpivot(struct vma vma[], int low, int high){
  struct vma pivot = vma[low];
  while(low<high){
    while(low<high&&vma[high].vma_start<=pivot.vma_start)
      high--;
    vma[low] = vma[high];
    while(low<high&&vma[low].vma_start>=pivot.vma_start)
      low++;
    vma[high] = vma[low];
  }
  vma[low] = pivot;
  return low;
}

void QuickSort(struct vma  vma[], int low, int high){
  if(low<high){
    int pivot = getpivot(vma,low,high);
    QuickSort(vma,low,pivot-1);
    QuickSort(vma,pivot+1,high);    
  }
}

struct vma* get_gapstaddr(struct vma vma[],uint64 length){
  QuickSort(vma,0,VMA_MAXNUM-1);  //按起始地址大到小排序
  uint64 end = TRAPFRAME;
  uint64 endtemp = TRAPFRAME;
  int found = 0;
  struct vma *free_vm;
  for(int i=0; i<VMA_MAXNUM; i++){
    if(vma[i].vma_used==1){
      endtemp = PGROUNDDOWN(vma[i].vma_start);
    }
    else if(!found&&vma[i].vma_used==0){
        found = 1;
        free_vm = &vma[i]; 
    }
  }
  if(!found)
    return 0;

  end = endtemp < end ? endtemp : end;
  printf("endtemp=%x  end=%x\n",endtemp,end);
  int gap_flag = 0;
  if(vma[0].vma_used==0&&TRAPFRAME-(vma[1].vma_start+vma[1].vma_length)>=length){
    free_vm->vma_start = TRAPFRAME-length;
    free_vm->vma_used = 1;
    return free_vm;
  }
  for (int i = 1; i < VMA_MAXNUM; i++) {
    if (vma[i].vma_used==0) {
      uint64 gap = vma[i-1].vma_start - vma[i].vma_start;
      if (gap >= length) {
        free_vm->vma_start = PGROUNDDOWN(vma[i-1].vma_start) - length;
        free_vm->vma_used = 1;
        gap_flag = 1;
        break;
      }
    }
    if(vma[i].vma_used==1&&vma[i-1].vma_used==1){
      uint64 gap = vma[i-1].vma_start - (vma[i].vma_start + vma[i].vma_length);
      if(gap>=length){
        free_vm->vma_start = PGROUNDDOWN(vma[i-1].vma_start) - length;
        free_vm->vma_used = 1;
        gap_flag = 1;
        break;  
      }
    } 
  }
  if(gap_flag==0){
    free_vm ->vma_start = end - length;
    free_vm->vma_used = 1;
  }
  return free_vm;
}


//for mmap: void *mmap(void *addr, uint64 length, int prot,int flags,int fd, uint64 offset);
uint64 sys_mmap(void){
  uint64 va,length,offset;
  int prot,flags,fd;
  struct file* file;
  
  if(argaddr(0,&va)<0||argaddr(1,&length)<0||argint(2,&prot)<0 \
    ||argint(3,&flags)<0||argfd(4,&fd,&file)<0||argaddr(5,&offset)<0)
    errlog("parameter fault\n");

  //va and offset should be 0
  if(va!=0||offset!=0)
    errlog("va/offset non-zero\n");

  struct proc *p = myproc(); //current process 
  
  if(file->ref<1||(!file->readable&&(prot&PROT_READ))|| \
    (!file->writable&&((prot&PROT_WRITE)&&(flags&MAP_SHARED))))
      errlog("mmap file flags wrong\n");

  length = PGROUNDUP(length);  //mmap length aligned
    
  struct vma* free_vm = get_gapstaddr(p->vma,length);
  
  if(free_vm==0)
    return -1;

  free_vm->vma_length = length;
  free_vm->vma_prot = prot;
  free_vm->vma_flags = flags;
  free_vm->vma_file = file;
  filedup(free_vm->vma_file);

  //For MY Sort Test
  for(int i=0; i<5; i++){
    printf("vma%d: start:%x , size:%x, flag:%d\n",i,p->vma[i].vma_start,p->vma[i].vma_length,p->vma[i].vma_used); 
  }

  return free_vm->vma_start;
}

int vma_unmap(pagetable_t pagetable,uint64 va,uint64 va_end,struct vma*vma){
  pte_t *pte;
  uint64 pa;
  uint64 va1;
  //int i =0;
  for(va1 = PGROUNDDOWN(va);va1< PGROUNDUP(va_end); va1 += PGSIZE){
    //i++;
    //printf("%d\n",i);
    //just for test
    printf("%p\t",va1);
    printf("%p\n",va_end);
  
    pte = walk(pagetable,va1,0);
    if(pte==0)
      errlog("vma_unmap No pte\n");
    if(PTE_FLAGS(*pte) == PTE_V)  //非叶子结点
      errlog("Non-leaf pte\n");
    if(((*pte)&PTE_V)==0) 
      continue;
    pa = PTE2PA(*pte);

    if((*pte&PTE_Dirty)&&(vma->vma_flags&MAP_SHARED)){ //PTE_Dirty
    begin_op();
    ilock(vma->vma_file->ip);
    uint64 offset = va1 - va;
     
    if(offset < 0)
      writei(vma->vma_file->ip, 0, pa+(-offset),0 ,PGSIZE-(-offset));

    else if(offset + PGSIZE > vma->vma_length)
      writei(vma->vma_file->ip,0 ,pa ,offset ,PGSIZE-offset);

    else
      writei(vma->vma_file->ip,0 ,pa ,offset ,PGSIZE);

    iunlock(vma->vma_file->ip);
    end_op();
    }                                                         
    kfree((void*)pa);
    *pte = 0;
  }
  return 0;
}

uint64 sys_munmap(void){// int munmap(void *addr, uint64 length);
  uint64 va,length;
  if(argaddr(0,&va)<0||argaddr(1,&length)<0)
    errlog("munmap parameter fault\n");
  //printf("begin!\n"); 
  struct proc *p=myproc();
  struct vma *vma = 0;
  for(int i = 0; i<VMA_MAXNUM; i++){
    struct vma*vmai = &p->vma[i];
    if(vmai->vma_used&& va >= vmai->vma_start&& va < (vmai->vma_start+vmai->vma_length)){
      vma = vmai;
      break;
    }
  }

  if(!vma)
    errlog("munmap No VMA\n");
  
  if(va>vma->vma_start&& va+length < vma->vma_start+vma->vma_length)
    errlog("Hole unmap");
 
  //printf("begi2!\n"); 
  if(vma_unmap(p->pagetable, va, va+length, vma)!=0)
    errlog("unmap fault\n");
  //printf("begi3!\n"); 


  if(PGROUNDDOWN(va)==vma->vma_start){
    vma->vma_start += length;
    //printf("caller!\n"); //for test
  } 
  vma->vma_length -= length;

  if(vma->vma_length <= 0){
    fileclose(vma->vma_file);
    vma->vma_used = 0;
  }
  //printf("begin!\n"); 
  return 0;
}

