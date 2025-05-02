#include <syscall.h>
#include <types.h>
#include <stdarg.h>
#include <kern/unistd.h>
#include <lib.h>



/*
 *
 *
 * We could check the validity of the user's given address 
 */
ssize_t sys_write(volatile int fd, const void *buf, size_t buflen, int *retval){
    int err;
	
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
