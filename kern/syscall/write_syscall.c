#include <syscall.h>
#include <types.h>
#include <stdarg.h>
#include <kern/unistd.h>
#include <lib.h>
#include <copyinout.h>
#include <proc.h>
#include <uio.h>
#include <current.h>

int
printf(const char *fmt, ...);

static
inline
int
__printf(const char *fmt, va_list ap);

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


static
inline
int
__printf(const char *fmt, va_list ap)
{
	int chars;
	// bool dolock;

	// dolock = kprintf_lock != NULL
	// 	&& curthread->t_in_interrupt == false
	// 	&& curthread->t_curspl == 0
	// 	&& curcpu->c_spinlocks == 0;

	// if (dolock) {
	// 	lock_acquire(kprintf_lock);
	// }
	// else {
	// 	spinlock_acquire(&kprintf_spinlock);
	// }

	chars = __vprintf(console_send, NULL, fmt, ap);

	// if (dolock) {
	// 	lock_release(kprintf_lock);
	// }
	// else {
	// 	spinlock_release(&kprintf_spinlock);
	// }

	return chars;
}

int
printf(const char *fmt, ...)
{
	int chars;
	va_list ap;

	va_start(ap, fmt);
	chars = __printf(fmt, ap);
	va_end(ap);

	return chars;
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
	 *	This is probably not the right approach as dup2 could bind 
	 *	the standard output to sth else (Need to change this)
	 */
    if(fd == STDOUT_FILENO || fd == STDERR_FILENO){
        console_send(NULL, kern_buff, buflen);
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