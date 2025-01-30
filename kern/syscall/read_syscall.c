#include <syscall.h>
#include <kern/unistd.h>
#include <types.h>
#include <lib.h>
#include <copyinout.h>



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
 */
int sys_read(int fd, void *buf, size_t buflen, int *retval){
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
    }

    if(err){
        // This is a dummy code to avoid not used buflen, needs to be changed after all
        *retval = buflen;
        return err;
    }
    return 0;
}