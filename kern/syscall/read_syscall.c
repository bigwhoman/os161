#include <syscall.h>
#include <kern/unistd.h>
#include <types.h>
#include <lib.h>
#include <copyinout.h>
#include <current.h>
#include <proc.h>
#include <uio.h>

void
roo(char *buf, size_t maxlen);

static
void
backsp(void)
{
	putch('\b');
	putch(' ');
	putch('\b');
}

/*
 * Read a string off the console. Support a few of the more useful
 * common control characters. Do not include the terminating newline
 * in the buffer passed back.
 */
void
roo(char *buf, size_t maxlen)
{
	size_t pos = 0;
	int ch;

	while (pos < maxlen - 1) {
		ch = getch();
		if (ch=='\n' || ch=='\r') {
			putch('\n');
			break;
		}

		/* Only allow the normal 7-bit ascii */
		if (ch>=32 && ch<127 && pos < maxlen-1) {
			putch(ch);
			buf[pos++] = ch;
		}
		else if ((ch=='\b' || ch==127) && pos>0) {
			/* backspace */
			backsp();
			pos--;
		}
		else if (ch==3) {
			/* ^C - return empty string */
			putch('^');
			putch('C');
			putch('\n');
			pos = 0;
			break;
		}
		else if (ch==18) {
			/* ^R - reprint input */
			buf[pos] = 0;
			kprintf("^R\n%s", buf);
		}
		else if (ch==21) {
			/* ^U - erase line */
			while (pos > 0) {
				backsp();
				pos--;
			}
		}
		else if (ch==23) {
			/* ^W - erase word */
			while (pos > 0 && buf[pos-1]==' ') {
				backsp();
				pos--;
			}
			while (pos > 0 && buf[pos-1]!=' ') {
				backsp();
				pos--;
			}
		}
		else {
			beep();
		}
	}

	buf[pos] = 0;
}



/*
 *  Need to check fd and buf to be valid and stuff
 *	TODO : 
 *		1. Check for valid fd
 *		2. Fix offset in uio_kinit (lseek could actually change it !!!!)
 */
int sys_read(volatile int fd, void *buf, size_t buflen, int *retval){
    int err;
    /*
     *
     * First, we would copy a dummy string to src to see wether the 
     * user has access to that address or not
    */
    const char *dummy_buffer = "dummy\0";
    err = copyoutstr(dummy_buffer, buf, sizeof(dummy_buffer)+2, (size_t *)retval);
    

    if (fd == STDIN_FILENO){
        roo( buf, 2);
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
			KASSERT(false);
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