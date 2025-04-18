#include <syscall.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <types.h>
#include <lib.h>
#include <copyinout.h>
#include <current.h>
#include <proc.h>
#include <uio.h>

void
roo(char *buf, size_t maxlen);


/*
 * Read a string off the console. Support a few of the more useful
 * common control characters. Do not include the terminating newline
 * in the buffer passed back.
 */
void
roo(char *buf, size_t maxlen)
{
	size_t pos = 0;
	char ch;

	while (pos < maxlen) {
		ch = getch();
		int res = copyout(&ch, (userptr_t)(buf+pos),1);
		if(res != 0)
			kprintf("whattttttttttttt!!!");
		pos++;
	}
	// char too[] = "a";
	// int res;
	// res = copyout(too, (userptr_t)buf, maxlen);
	// kprintf("goo : %c\n",*buf);
	// if(res != 0)
	// 	kprintf("whattttttttttttt!!!");
}



/*
 *  Need to check fd and buf to be valid and stuff
 *	TODO : 
 *		1. Check for valid fd
 *		2. Fix offset in uio_kinit (lseek could actually change it !!!!)
 */
int sys_read(volatile int fd, void *buf, size_t buflen, int *retval){
    int err = 0;
    /*
     *
     * First, we would copy a dummy string to src to see wether the 
     * user has access to that address or not ( This approach was apparently wrong!!)
	 * We need to do some checking for write !!!
    */
    const char *dummy_buffer = "dummy\0";
	(void) dummy_buffer;
    // err = copyoutstr(dummy_buffer, buf, sizeof(dummy_buffer)+2, (size_t *)retval);
    

    if (fd == curproc -> stdin){
        roo( buf, buflen);
		*retval = buflen;
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
		u.uio_rw = UIO_READ;
		u.uio_space = curproc->p_addrspace;

		err = VOP_READ(v, &u);

		if (err) {
			return err;
		}

		if (u.uio_resid != 0) {
				/* short read; problem with executable? */
				err = ENOEXEC;
		}
		*retval = buflen - u.uio_resid;
	}

    if(err){
        // This is a dummy code to avoid not used buflen, needs to be changed after all
		buflen = -1;
        *retval = buflen;
        return err;
    }
    return 0;
}