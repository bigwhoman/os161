#include <syscall.h>
#include <types.h>
#include <stdarg.h>
#include <kern/unistd.h>
#include <lib.h>
#include <copyinout.h>

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
ssize_t sys_write(int fd, const void *buf, size_t buflen, int *retval){
    int err;
    char kern_buff[buflen+1];

    kern_buff[buflen] = '\0';
    err = copyinstr(buf, kern_buff, buflen+1, (size_t *)retval);
    if(err){
        *retval = -1;
        return err;
    }
    if(fd == STDOUT_FILENO){
        console_send(NULL, kern_buff, *(size_t *)retval);
    } else {
        err = 0;
    }
    return err;
}