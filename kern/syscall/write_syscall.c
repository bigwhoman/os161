#include <syscall.h>
#include <types.h>
#include <stdarg.h>
#include <kern/unistd.h>
#include <lib.h>
#include <copyinout.h>
#include <proc.h>
#include <uio.h>
#include <current.h>
#include <synch.h>

struct lock *console_lock;

static
void
console_send(void *junk, const char *data, size_t len)
{
	size_t i;

	(void)junk;

	for (i=0; i<len; i++) {
		putch(data[i]);
	}
}

/* Bootstrap for write syscall */
void write_bootstrap(){
	console_lock = lock_create("Console Lock");
}

/*
 *
 *
 * We could check the validity of the user's given address 
 */
ssize_t sys_write(volatile int fd, const void *buf, size_t buflen, int *retval){
    int err;
    char kern_buff[buflen+1];

    kern_buff[buflen] = '\0';
    err = copyin(buf, kern_buff, buflen+1);
	
    if(err){
        *retval = -1;
        return err;
    }


	/*
	 *	Explicitly check for special file descriptors
	 */
    if(fd == curproc -> stdout || fd == curproc -> stderr){
		lock_acquire(console_lock);
        console_send(NULL, kern_buff, buflen);
		*retval = buflen;
		lock_release(console_lock);
    } else {
        struct vnode *v;
		/* TODO : Check Valid/inbounds fd */
		v = (curproc)->fd_table[fd];
		struct iovec iov;
		struct uio u;

		iov.iov_ubase = (userptr_t)buf;
		iov.iov_len = buflen;		 // length of the memory space
		u.uio_iov = &iov;
		u.uio_iovcnt = 1;
		u.uio_resid = buflen;          // amount to read from the file
		u.uio_offset = 0;
		u.uio_segflg = UIO_USERSPACE;
		u.uio_rw = UIO_WRITE;
		u.uio_space = curproc->p_addrspace;

		err = VOP_WRITE(v, &u);

		if (err) {
			return err;
		}

		if (u.uio_resid != 0) {
			/* short read; problem? */
			KASSERT(false);
		}
		*retval=buflen-u.uio_resid;
    }
    return err;
}