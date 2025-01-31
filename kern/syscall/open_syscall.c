#include <syscall.h>
#include <types.h>
#include <vfs.h>
#include <current.h>
#include <proc.h>

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
    for (size_t i = 3; i < curproc->max_fd; i++)
    {
        if(curproc->fd_table[i] == NULL){
            curproc->fd_table[i] = v;
            *retval = i;
            break;
        }
    }
    return ret;
}