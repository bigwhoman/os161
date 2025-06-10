#include <syscall.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <types.h>
#include <stat.h>
#include <kern/stattypes.h>
#include <kern/seek.h>
#include <vfs.h>
#include <copyinout.h>
#include <kern/fcntl.h>
#include <proc.h>

/*
 *
 * 
 * TODO: Change the fd system to be lazy !!!!!
 */

/*
 * 
 * 
 * open opens the file, device, or other kernel object named by the pathname filename.
 * The flags argument specifies how to open the file. 
 * The optional mode argument provides the file permissions
 * 
 *  Checks TODO : 
 *      1. Process has the file already opened
 *      2. Errors are right  
 *      3. Check for mode to be null
 *
*/

int sys_open(char *filename, int flags, mode_t mode, int *retval) {
    struct vnode *v;
    int err;
    char *fname;
    size_t got;
    
    fname = kmalloc(PATH_MAX);
    if (fname == NULL) {
        *retval = -1;
        return ENOMEM;
    }
    
    err = copyinstr((const_userptr_t)filename, fname, PATH_MAX, &got);
    if (err) {
        kfree(fname);
        *retval = -1;
        return err;
    }
    
    if (filename == NULL || got <= 0) {
        kfree(fname);
        *retval = -1;
        return ENOENT;
    }
    
    err = vfs_open(fname, flags, mode, &v);
    if (err) {
        kfree(fname);
        *retval = -1;
        return err;
    }
    
    /* Create new fd_entry */
    struct fd_entry *fde = kmalloc(sizeof(struct fd_entry));
    if (fde == NULL) {
        vfs_close(v);
        kfree(fname);
        *retval = -1;
        return ENOMEM;
    }
    
    fde->vnode = v;
    fde->pos = 0;
    fde->lock = lock_create("fd_lock");
    if (fde->lock == NULL) {
        vfs_close(v);
        kfree(fde);
        kfree(fname);
        *retval = -1;
        return ENOMEM;
    }
    fde->count = 1;
    fde->mode = mode;
    fde->flags = flags;
    fde->path = fname;  /* Transfer ownership of fname */
    
    /* Find free fd slot and add entry */
    lock_acquire(curproc->fd_table->lock);
    
    unsigned int fd;
    err = bitmap_alloc(curproc->fd_table->bitmap, &fd);
    if (err == ENOSPC || fd >= MAX_FD) {
        lock_release(curproc->fd_table->lock);
        lock_destroy(fde->lock);
        vfs_close(v);
        kfree(fde->path);
        kfree(fde);
        *retval = -1;
        return EMFILE;
    }
    
    /* Ensure array is large enough
	 * Might need to changed this to use preallocate
	*/
    while (array_num(curproc->fd_table->entries) <= fd) {
        array_add(curproc->fd_table->entries, NULL, NULL);
    }
    
    array_set(curproc->fd_table->entries, fd, fde);
    
    lock_release(curproc->fd_table->lock);
    
    *retval = fd;
    return 0;
}


/* 
 * The file handle identified by file descriptor fd is closed.
 * The same file handle may then be returned again from
 *  open, dup2, pipe, or similar calls. 
 */
int sys_close(int fd, int *retval) {
    if (fd < 0 || fd >= MAX_FD) {
        *retval = -1;
        return EBADF;
    }
    
    lock_acquire(curproc->fd_table->lock);
    
    /* Check if fd is valid */
    if (!bitmap_isset(curproc->fd_table->bitmap, fd)) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    struct fd_entry *fde = array_get(curproc->fd_table->entries, fd);
    if (fde == NULL) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    /* Clear the fd slot */
    bitmap_unmark(curproc->fd_table->bitmap, fd);
    array_set(curproc->fd_table->entries, fd, NULL);
    
    lock_release(curproc->fd_table->lock);
    
    /* Clean up fd_entry outside of table lock */
    fde->count--;
    if (fde->count == 0) {
        if (fde->vnode != NULL) {
            vfs_close(fde->vnode);
        }
        if (fde->path != NULL) {
            kfree(fde->path);
        }
        if (fde->lock != NULL && fde->lock != console_lock) {
            lock_destroy(fde->lock);
        }
        kfree(fde);
    }
    
    *retval = 0;
    return 0;
}



/*
 *
 * write up to buflen bytes to the file specified by fd, at the location in the 
 * file specified by the current seek position of the file,
 * taking the data from the space pointed to by buf.
 * !! The file must be open for writing. !!
 * 
 */
int sys_write(int fd, const void *buf, size_t buflen, int *retval) {
    int err;
    
    if (fd < 0 || fd >= MAX_FD) {
        *retval = -1;
        return EBADF;
    }
    
    lock_acquire(curproc->fd_table->lock);
    
    if (!bitmap_isset(curproc->fd_table->bitmap, fd)) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    struct fd_entry *fde = array_get(curproc->fd_table->entries, fd);
    if (fde == NULL) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    /* Check write permission */
    if (!(fde->flags & (O_RDWR | O_WRONLY))) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    /* Hold the fd lock while writing */
    lock_acquire(fde->lock);
    lock_release(curproc->fd_table->lock);
    
    struct iovec iov;
    struct uio u;
    
    iov.iov_ubase = (userptr_t)buf;
    iov.iov_len = buflen;
    u.uio_iov = &iov;
    u.uio_iovcnt = 1;
    u.uio_resid = buflen;
    u.uio_offset = fde->pos;
    u.uio_segflg = UIO_USERSPACE;
    u.uio_rw = UIO_WRITE;
    u.uio_space = curproc->p_addrspace;
    
    err = VOP_WRITE(fde->vnode, &u);
    
    if (err) {
        lock_release(fde->lock);
        *retval = -1;
        return err;
    }
    
    fde->pos += (buflen - u.uio_resid);
    *retval = buflen - u.uio_resid;
    
    lock_release(fde->lock);
    return 0;
}




/*
 * read reads up to buflen bytes from the file specified by fd, at the location in the file specified 
 * by the current seek position of the file, and stores them in the 
 * space pointed to by buf. The file must be open for reading.

 * The current seek position of the file is advanced by the number of bytes read.

 * Each read (or write) operation is atomic relative to other I/O to the same file. 
 * Note that the kernel is not obliged to (and generally cannot) make the read atomic with respect to other threads
 *  in the same process accessing the I/O buffer during the read.
 * 
 * 
 */
int sys_read(int fd, void *buf, size_t buflen, int *retval) {
    int err = 0;
    
    if (fd < 0 || fd >= MAX_FD) {
        *retval = -1;
        return EBADF;
    }
    
    lock_acquire(curproc->fd_table->lock);
    
    if (!bitmap_isset(curproc->fd_table->bitmap, fd)) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    struct fd_entry *fde = array_get(curproc->fd_table->entries, fd);
    if (fde == NULL) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    /* Check read permission */
    if (fde->flags & O_WRONLY) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    /* Hold the fd lock while reading */
    lock_acquire(fde->lock);
    lock_release(curproc->fd_table->lock);
    
    struct iovec iov;
    struct uio u;
    
    iov.iov_ubase = (userptr_t)buf;
    iov.iov_len = buflen;
    u.uio_iov = &iov;
    u.uio_iovcnt = 1;
    u.uio_resid = buflen;
    u.uio_offset = fde->pos;
    u.uio_segflg = UIO_USERSPACE;
    u.uio_rw = UIO_READ;
    u.uio_space = curproc->p_addrspace;
    
    err = VOP_READ(fde->vnode, &u);
    
    if (err) {
        lock_release(fde->lock);
        *retval = -1;
        return err;
    }
    
    fde->pos += (buflen - u.uio_resid);
    *retval = buflen - u.uio_resid;
    
    lock_release(fde->lock);
    return 0;
}

/*
 * lseek alters the current seek position of 
 * the file handle identified by file descriptor fd, 
 * seeking to a new position based on pos and whence. 
 * 
 */
int sys_lseek(int fd, off_t pos, int whence, int *retval1, int *retval) {
    struct stat stat;
    off_t new_pos;
    
    if (fd < 0 || fd >= MAX_FD) {
        *retval = -1;
        return EBADF;
    }
    
    lock_acquire(curproc->fd_table->lock);
    
    if (!bitmap_isset(curproc->fd_table->bitmap, fd)) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    struct fd_entry *fde = array_get(curproc->fd_table->entries, fd);
    if (fde == NULL) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    lock_acquire(fde->lock);
    lock_release(curproc->fd_table->lock);
    
    /* Get file stats */
    VOP_STAT(fde->vnode, &stat);
    mode_t file_type = (stat.st_mode & S_IFMT) >> 12;
    
    /* Check if seekable */
    if (file_type != 1 && file_type != 7) {
        lock_release(fde->lock);
        *retval = -1;
        return ESPIPE;
    }
    
    switch (whence) {
        case SEEK_SET:
            new_pos = pos;
            break;
        case SEEK_CUR:
            new_pos = fde->pos + pos;
            break;
        case SEEK_END:
            new_pos = stat.st_size + pos;
            break;
        default:
            lock_release(fde->lock);
            *retval = -1;
            return EINVAL;
    }
    
    if (new_pos < 0) {
        lock_release(fde->lock);
        *retval = -1;
        return EINVAL;
    }
    
    fde->pos = new_pos;
    
    *retval = (int)(new_pos & 0xffffffff);
    *retval1 = (int)(new_pos >> 32);
    
    lock_release(fde->lock);
    return 0;
}


/*
 * The name of the file referred to by pathname is removed from the filesystem. 
 * The actual file itself is not removed until no further references to it exist,
 *  whether those references are on disk or in memory.
 * 
 * TODO : Complete when handling the filesystem
 */
int sys_remove(const char *pathname, int *retval){
	int err;
	err = 0;
	(void) pathname;
	// err = vfs_remove((char *)pathname);
	*retval = 0;
	return err;
}

/*
 *  clones the file handle identifed by file descriptor oldfd
 *  onto the file handle identified by newfd. If newfd
 *  names an already-open file, that file is closed.
 *
 *  TODO: EMFILE - ENFILE errors (?!!) 
 */
int sys_dup2(int oldfd, int newfd, int *retval) {
    
    if (oldfd < 0 || oldfd >= MAX_FD || newfd < 0 || newfd >= MAX_FD) {
        *retval = -1;
        return EBADF;
    }
    
    if (oldfd == newfd) {
        *retval = newfd;
        return 0;
    }
    
    lock_acquire(curproc->fd_table->lock);
    
    if (!bitmap_isset(curproc->fd_table->bitmap, oldfd)) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    struct fd_entry *old_fde = array_get(curproc->fd_table->entries, oldfd);
    if (old_fde == NULL) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    /* Close newfd if it's open */
    if (bitmap_isset(curproc->fd_table->bitmap, newfd)) {
        struct fd_entry *new_fde = array_get(curproc->fd_table->entries, newfd);
        if (new_fde != NULL) {
            /* Mark as closed but don't clean up yet */
            bitmap_unmark(curproc->fd_table->bitmap, newfd);
            array_set(curproc->fd_table->entries, newfd, NULL);
            
            /* Clean up after releasing table lock */
            lock_release(curproc->fd_table->lock);
            
            new_fde->count--;
            if (new_fde->count == 0) {
                if (new_fde->vnode != NULL) vfs_close(new_fde->vnode);
                if (new_fde->path != NULL) kfree(new_fde->path);
                if (new_fde->lock != NULL && new_fde->lock != console_lock) {
                    lock_destroy(new_fde->lock);
                }
                kfree(new_fde);
            }
            
            lock_acquire(curproc->fd_table->lock);
        }
    }
    
    /* Ensure array is large enough */
    while (array_num(curproc->fd_table->entries) <= (unsigned)newfd) {
        array_add(curproc->fd_table->entries, NULL, NULL);
    }
    
    /* Increment reference count and copy */
    old_fde->count++;
    if (old_fde->vnode != NULL) {
        VOP_INCREF(old_fde->vnode);
    }
    
    array_set(curproc->fd_table->entries, newfd, old_fde);
    bitmap_mark(curproc->fd_table->bitmap, newfd);
    
    lock_release(curproc->fd_table->lock);
    
    *retval = newfd;
    return 0;
}



/*
 * Retreives status information about the file referred to by the
 * file descriptor fd and stores it in the stat structure pointed
 * to by statbuf. 
 * 
 * TODO: Not sure about stability tests
 */
int sys_fstat(int fd, struct stat *statbuf, int *retval) {
    int err = 0;
    struct stat stat;
    
    if (fd < 0 || fd >= MAX_FD) {
        *retval = -1;
        return EBADF;
    }
    
    lock_acquire(curproc->fd_table->lock);
    
    if (!bitmap_isset(curproc->fd_table->bitmap, fd)) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    struct fd_entry *fde = array_get(curproc->fd_table->entries, fd);
    if (fde == NULL) {
        lock_release(curproc->fd_table->lock);
        *retval = -1;
        return EBADF;
    }
    
    lock_acquire(fde->lock);
    lock_release(curproc->fd_table->lock);
    
    VOP_STAT(fde->vnode, &stat);
    
    lock_release(fde->lock);
    
    err = copyout((void *)&stat, (userptr_t)statbuf, sizeof(struct stat));
    if (err) {
        *retval = -1;
        return EFAULT;
    }
    
    *retval = 0;
    return 0;
}