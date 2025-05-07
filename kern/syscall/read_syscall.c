#include <syscall.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <types.h>
#include <stat.h>
#include <kern/seek.h>
#include <vfs.h>
#include <copyinout.h>

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
 * TODO: ESPIPE error 
 */
int sys_lseek(int fd, off_t pos, int whence, int *retval1, int *retval)
{
	int err = 0;
	struct stat *stat;
	off_t eof, new_pos;
	lock_acquire(curproc->fd_lock[fd]);

	struct vnode *v;
	/* TODO : Check Valid/inbounds fd */
	v = (curproc)->fd_table[fd];
	new_pos = *curproc->fd_pos[fd];
	if (v == NULL){
		*retval = -1;
		err = EBADF;
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
		stat = kmalloc(sizeof(struct stat));
		VOP_STAT(v, stat);
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