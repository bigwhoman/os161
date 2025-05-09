#include <syscall.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <types.h>
#include <stat.h>
#include <kern/stattypes.h>
#include <kern/seek.h>
#include <vfs.h>
#include <copyinout.h>

/*
 *
 * 
 * TODO: Change the fd system to be lazy !!!!!
 */

/*
 * 
 * 
 * open opens the file, device, or other kernel object named by the pathname filename.
 * The flags argument specifies how to open the file. 
 * The optional mode argument provides the file permissions
 * 
 *  Checks TODO : 
 *      1. Process has the file already opened
 *      2. Errors are right  
 *      3. Check for mode to be null
 *
*/

int sys_open(char *filename, int flags, mode_t mode, int *retval){ 
    struct vnode *v;
    int err;
	char *fname;
	size_t got;
	fname = kmalloc(PATH_MAX);
	err = copyinstr((const_userptr_t)filename, fname, PATH_MAX, &got);
	if(err){
		kfree(fname);
		*retval = -1;
		return err;
	}
	if (filename == NULL || got <= 0){
		err = ENOENT;
		*retval = -1;
		return err;
	}
    err = vfs_open(fname, flags, mode, &v);
	kfree(fname);
    if (err > 0){
        *retval = -1;
        return err;
    }

    /* Try to find an empty entry (this is not optimal) */
    for (size_t i = 0; i < curproc->max_fd; i++)
    {
        lock_acquire(curproc -> fd_lock[i]);
        if((int)i == curproc -> stdin ||
            (int)i == curproc -> stdout || 
             (int)i == curproc -> stderr ){
            lock_release(curproc->fd_lock[i]);
            continue;
        }
        if(curproc->fd_table[i] == NULL){
            curproc->fd_table[i] = v;
            /* This needs to be changed according to mode !!*/
            *curproc->fd_pos[i] = 0;
            *retval = i;
            curproc -> fd_path[i] = kstrdup(filename);
            curproc -> fd_flags[i] = flags;
            curproc -> fd_mode[i] = mode;
            lock_release(curproc->fd_lock[i]);
			return err;
        }
        lock_release(curproc->fd_lock[i]);
    }

	/* The processes file table was full */
	*retval = -1;
	err = EMFILE;	
    return err;
}


/* 
 * The file handle identified by file descriptor fd is closed.
 * The same file handle may then be returned again from
 *  open, dup2, pipe, or similar calls. 
 */
int sys_close(int fd, int *retval){
	int err;
	err = 0;
	if (fd < 0 || fd >= MAX_FD)
	{
		err = EBADF;
		*retval = -1;
		return err;
	}
	lock_acquire(curproc->fd_lock[fd]);
    *retval = 0;

    if (fd == curproc -> stdin){
        curproc -> stdin = -1;
        curproc -> fd_table[fd] = NULL;
        lock_release(curproc->fd_lock[fd]);
        return 0;
    }
    if (fd == curproc -> stdout){
        curproc -> stdout = -1;
        curproc -> fd_table[fd] = NULL;
        lock_release(curproc->fd_lock[fd]);
        return 0;
    }
    if (fd == curproc -> stderr){
        curproc -> stderr = -1;
        curproc -> fd_table[fd] = NULL;
        lock_release(curproc->fd_lock[fd]);
        return 0;
    }

    struct vnode *v;
    v = curproc -> fd_table[fd];
    if (v == NULL){
        *retval = -1;
        lock_release(curproc->fd_lock[fd]);
        return EBADF;
    }
    vfs_close(v);
    curproc -> fd_count[fd] -= 1;
    curproc -> fd_table[fd] = NULL; 
    lock_release(curproc->fd_lock[fd]);
    return err;
}



/*
 *
 * write up to buflen bytes to the file specified by fd, at the location in the 
 * file specified by the current seek position of the file,
 * taking the data from the space pointed to by buf.
 * !! The file must be open for writing. !!
 * 
 */
int sys_write(volatile int fd, const void *buf, size_t buflen, int *retval){
    int err;

	if (fd < 0 || fd >= MAX_FD)
	{
		err = EBADF;
		*retval = -1;
		return err;
	}
	/* Could change this lock with rwlock maybe */
	lock_acquire(curproc->fd_lock[fd]);

	struct vnode *v;
	v = (curproc)->fd_table[fd];
	struct iovec iov;
	struct uio u;

	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = buflen; // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = buflen; // amount to read from the file
	u.uio_offset = *curproc->fd_pos[fd];
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_WRITE;
	u.uio_space = curproc->p_addrspace;

	err = VOP_WRITE(v, &u);

	if (err)
	{
		lock_release(curproc->fd_lock[fd]);
		return err;
	}

	*curproc->fd_pos[fd] += (buflen - u.uio_resid);
	*retval = buflen - u.uio_resid;

	lock_release(curproc->fd_lock[fd]);
    return err;
}




/*
 * read reads up to buflen bytes from the file specified by fd, at the location in the file specified 
 * by the current seek position of the file, and stores them in the 
 * space pointed to by buf. The file must be open for reading.

 * The current seek position of the file is advanced by the number of bytes read.

 * Each read (or write) operation is atomic relative to other I/O to the same file. 
 * Note that the kernel is not obliged to (and generally cannot) make the read atomic with respect to other threads
 *  in the same process accessing the I/O buffer during the read.
 * 
 * 
 */
int sys_read(volatile int fd, void *buf, size_t buflen, int *retval)
{
	int err = 0;
	if (fd < 0 || fd >= MAX_FD)
	{
		err = EBADF;
		*retval = -1;
		return err;
	}
	lock_acquire(curproc->fd_lock[fd]);
	struct vnode *v;
	v = (curproc)->fd_table[fd];
	struct iovec iov;
	struct uio u;

	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = buflen; // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = buflen; // amount to read from the file
	u.uio_offset = *curproc->fd_pos[fd];
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = curproc->p_addrspace;

	err = VOP_READ(v, &u);

	if (err)
	{
		lock_release(curproc->fd_lock[fd]);
		return err;
	}


	*curproc->fd_pos[fd] += (buflen - u.uio_resid);
	*retval = buflen - u.uio_resid;
	
	lock_release(curproc->fd_lock[fd]);
	return 0;
}

/*
 * lseek alters the current seek position of 
 * the file handle identified by file descriptor fd, 
 * seeking to a new position based on pos and whence. 
 * 
 */
int sys_lseek(int fd, off_t pos, int whence, int *retval1, int *retval)
{
	int err = 0;
	struct stat *stat;
	off_t eof, new_pos;
	mode_t file_type;
	if (fd < 0 || fd >= MAX_FD) 
	{
		err = EBADF;
		*retval = -1;
		return err;
	}
	lock_acquire(curproc->fd_lock[fd]);

	struct vnode *v;
	v = (curproc)->fd_table[fd];
	new_pos = *curproc->fd_pos[fd];
	/* Check for valid file*/
	if (v == NULL){
		*retval = -1;
		err = EBADF;
		lock_release(curproc->fd_lock[fd]);
		return err;
	}

	/* Get file stats */
	stat = kmalloc(sizeof(struct stat));
	VOP_STAT(v, stat);
	file_type = (stat->st_mode & S_IFMT) >> 12;

	/* If the file is not regular or block device, 
	 * we consider it cannot support seeking
	 */
	if (file_type != 1 && file_type != 7){
		*retval = -1;
		err = ESPIPE;
		lock_release(curproc->fd_lock[fd]);
		return err;
	}

	switch (whence) 
	{
	case SEEK_SET: 
		new_pos = pos;
		break;
	case SEEK_CUR:
		new_pos += pos;
		break;
	case SEEK_END:	
		eof = stat -> st_size;
		new_pos = (eof + pos);
		break;
	default:
		*retval = -1;
		err = EINVAL;
		lock_release(curproc->fd_lock[fd]);
		return err;
	}
	
	if(new_pos < 0){
		*retval = -1;
		err = EINVAL;
		lock_release(curproc->fd_lock[fd]);
		return err;
	}
	*curproc -> fd_pos[fd] = new_pos;


	*retval = (int)(*curproc->fd_pos[fd] & 0xffffffff);
	*retval1 = (int)((*curproc->fd_pos[fd]) >> 32);
	lock_release(curproc->fd_lock[fd]);
	return err;
}


/*
 * The name of the file referred to by pathname is removed from the filesystem. 
 * The actual file itself is not removed until no further references to it exist,
 *  whether those references are on disk or in memory.
 * 
 * TODO : Complete when handling the filesystem
 */
int sys_remove(const char *pathname, int *retval){
	int err;
	err = vfs_remove((char *)pathname);
	*retval = 0;
	return err;
}

/*
 *  clones the file handle identifed by file descriptor oldfd
 *  onto the file handle identified by newfd. If newfd
 *  names an already-open file, that file is closed.
 *
 *  TODO: EMFILE - ENFILE errors (?!!) 
 */
int sys_dup2(int oldfd, int newfd, int *retval){
  struct vnode* v;
  int err, close_err, close_ret;
  err = 0;
  if (oldfd < 0 || oldfd >= MAX_FD ||
		 newfd < 0 || newfd >= MAX_FD)
  {
	  err = EBADF;
	  *retval = -1;
	  return err;
  }
  lock_acquire(curproc->fd_lock[oldfd]);
  v = curproc->fd_table[oldfd];
  if(v == NULL){
    err = EBADF; 
    *retval = -1; 
    lock_release(curproc->fd_lock[oldfd]);
    return err;
  }
  if(curproc->fd_table[newfd] != NULL){
    close_err = sys_close(newfd, &close_ret);
    if (close_err){
      kprintf("Error in closing fd number : %d\n", newfd);
    } 
  }
  curproc->fd_table[newfd] = curproc->fd_table[oldfd];
  curproc->fd_lock[newfd] = curproc->fd_lock[oldfd];
  curproc->fd_pos[newfd] = curproc->fd_pos[oldfd];
  VOP_INCREF(curproc -> fd_table[newfd]);
  *retval = newfd;
  lock_release(curproc->fd_lock[oldfd]);
  return err;
}
