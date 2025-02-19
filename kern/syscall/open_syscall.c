#include <syscall.h>
#include <types.h>
#include <vfs.h>
#include <current.h>
#include <proc.h>
#include <kern/errno.h>

/*
 *  Checks TODO : 
 *      1. Process has the file already opened
 *      2. Errors are right  
 *      3. Check for mode to be null
 *
*/

int sys_open(char *filename, int flags, mode_t mode, int *retval){ 
    struct vnode *v;
    int ret;
    ret = vfs_open(filename, flags, mode, &v);
    if (ret > 0){
        *retval = -1;
        return ret;
    }

    /* Try to find an empty entry (this is not optimal) */
    for (size_t i = 0; i < curproc->max_fd; i++)
    {
        if((int)i == curproc -> stdin ||
            (int)i == curproc -> stdout || 
             (int)i == curproc -> stderr )
                continue;
        if(curproc->fd_table[i] == NULL){
            curproc->fd_table[i] = v;
            *retval = i;
            break;
        }
    }
    return ret;
}


/* The file handle identified by file descriptor fd is closed.
 * The same file handle may then be returned again from
 *  open, dup2, pipe, or similar calls. 
 */
int sys_close(int fd, int *retval){
    if (fd == curproc -> stdin){
        curproc -> stdin = -1;
        curproc -> fd_table[fd] = NULL;
        return 0;
    }
    if (fd == curproc -> stdout){
        curproc -> stdout = -1;
        curproc -> fd_table[fd] = NULL;
        return 0;
    }
    if (fd == curproc -> stderr){
        curproc -> stderr = -1;
        curproc -> fd_table[fd] = NULL;
        return 0;
    }

    struct vnode *v;
    v = curproc -> fd_table[fd];
    if (v == NULL){
        *retval = -1;
        return EBADF;
    }
    curproc -> fd_table[fd] = NULL;
    vfs_close(v);
    return 0;
}