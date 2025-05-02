#include <syscall.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <types.h>

/*
 *  Need to check fd and buf to be valid and stuff
 *	TODO : 
 *		1. Check for valid fd
 *		2. Fix offset in uio_kinit (lseek could actually change it !!!!)
 */
int sys_read(volatile int fd, void *buf, size_t buflen, int *retval)
{
	int err = 0;

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
	u.uio_offset = 0;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = curproc->p_addrspace;

	err = VOP_READ(v, &u);

	if (err)
	{
		return err;
	}

	if (u.uio_resid != 0)
	{
		/* short read; problem with executable? */
		err = ENOEXEC;
	}
	*retval = buflen - u.uio_resid;

	if (err)
	{
		// This is a dummy code to avoid not used buflen, needs to be changed after all
		buflen = -1;
		*retval = buflen;
		return err;
	}
	return 0;
}