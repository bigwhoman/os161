#include <syscall.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <types.h>
#include <stat.h>
#include <kern/seek.h>
#include <vfs.h>

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
 *  Need to check fd and buf to be valid and stuff
 *	TODO : 
 *		1. Check for valid fd
 *		2. Fix offset in uio_kinit (lseek could actually change it !!!!)
 */
int sys_read(volatile int fd, void *buf, size_t buflen, int *retval)
{
	int err = 0;
    lock_acquire(curproc->fd_lock[fd]);
	struct vnode *v;
	/* TODO : Check Valid/inbounds fd */
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

	if (err)
	{
		// This is a dummy code to avoid not used buflen, needs to be changed after all
		lock_release(curproc->fd_lock[fd]);
		buflen = -1;
		return err;
	}
	lock_release(curproc->fd_lock[fd]);
	return 0;
}

/*
 * lseek alters the current seek position of 
 * the file handle identified by file descriptor fd, 
 * seeking to a new position based on pos and whence. 
 * 
 * 
 * 
 * Need checks for seek positions less than zero 
 */
off_t sys_lseek(int fd, off_t pos, int whence, int *retval)
{
	off_t err = 0;
	struct stat *stat;
	off_t eof;
	lock_acquire(curproc->fd_lock[fd]);
	struct vnode *v;
	/* TODO : Check Valid/inbounds fd */
	v = (curproc)->fd_table[fd];
	switch (whence) 
	{
	case SEEK_SET: 
		*curproc->fd_pos[fd] = pos;
		break;
	case SEEK_CUR:
		*curproc->fd_pos[fd] += pos;
		break;
	case SEEK_END:
		stat = kmalloc(sizeof(struct stat));
		VOP_STAT(v, stat);
		eof = stat -> st_size;
		*curproc->fd_pos[fd] += (eof + pos);
		break;
	default:
		err = -1;
		/* Need to set this accordingly */
		*retval = EINVAL;
		break;
	}
	

	*retval = *curproc->fd_pos[fd];
	lock_release(curproc->fd_lock[fd]);
	return err;
}


/*
 * The name of the file referred to by pathname is removed from the filesystem. 
 * The actual file itself is not removed until no further references to it exist,
 *  whether those references are on disk or in memory.
 */
int sys_remove(const char *pathname, int *retval){
	
}