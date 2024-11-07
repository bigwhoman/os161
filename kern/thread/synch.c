/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}

	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

	lock ->mutex_wchan = wchan_create(lock->lk_name);
	if (lock->mutex_wchan == NULL){
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}
	lock -> holder = NULL;
	spinlock_init(&lock->mutex_lock);
	return lock;
}

void
lock_destroy(struct lock *lock)
{
	KASSERT(lock != NULL);
	KASSERT(lock->holder != curthread);
	spinlock_cleanup(&lock->mutex_lock);
	wchan_destroy(lock->mutex_wchan);
	// add stuff here as needed
	lock -> holder = NULL;
	kfree(lock->holder);
	kfree(lock->lk_name);
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	
	KASSERT(lock != NULL);
	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the lock aquire without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);
	/* Call this (atomically) before waiting for a lock */
	HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);
	/*
	 *	This spinlock is to ensure that no two threads would 
	 *  actually get the lock 
	*/
	spinlock_acquire(&lock->mutex_lock);
	while(lock -> holder != NULL) {
		/*
			Make sure we have the lock, and if not just make the thread sleep
		*/
		wchan_sleep(lock->mutex_wchan, &lock->mutex_lock);
	}
	lock -> holder = curthread;
	
	KASSERT(lock_do_i_hold(lock));
	spinlock_release(&lock->mutex_lock);

	
	/* Call this (atomically) once the lock is acquired */
	HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
}

void
lock_release(struct lock *lock)
{
	/* Call this (atomically) when the lock is released */
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock));
	HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);
	spinlock_acquire(&lock->mutex_lock);
	lock -> holder = NULL;
	wchan_wakeone(lock->mutex_wchan, &lock->mutex_lock);
	spinlock_release(&lock->mutex_lock);		
}

bool
lock_do_i_hold(struct lock *lock)
{
	// Write this
	// kprintf("hello world\n");
	KASSERT(lock != NULL);
	

	return lock->holder == curthread; 
}

////////////////////////////////////////////////////////////
//
// CV


struct cv *
cv_create(const char *name)
{
	KASSERT(name != NULL);
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}

	// add stuff here as needed
	cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL){
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	spinlock_init(&cv->cv_lock);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

	// add stuff here as needed
	wchan_destroy(cv->cv_wchan);
	spinlock_cleanup(&cv->cv_lock);
	kfree(cv->cv_name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	/*
	 * Apparently thread MUST hold the 
	 * mutex before calling cv_wait
	*/
	KASSERT(lock_do_i_hold(lock)); // I think it is not needed
	spinlock_acquire(&cv->cv_lock);
	lock_release(lock);
	wchan_sleep(cv->cv_wchan, &cv->cv_lock);
	spinlock_release(&cv->cv_lock);
	lock_acquire(lock);
	
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&cv->cv_lock);
	if (!wchan_isempty(cv->cv_wchan, &cv->cv_lock))
		wchan_wakeone(cv->cv_wchan, &cv->cv_lock);
	spinlock_release(&cv->cv_lock);

}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	// Write this
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&cv->cv_lock);
	wchan_wakeall(cv->cv_wchan, &cv->cv_lock);
	spinlock_release(&cv->cv_lock);
}

////////////////////////////////////////////////////////////
//
// RW

struct rwlock * rwlock_create(const char *name){
	KASSERT(name != NULL);
	struct rwlock *rwlock;

	rwlock = kmalloc(sizeof(*rwlock));
	if (rwlock == NULL) {
		return NULL;
	}

	rwlock->rwlock_name = kstrdup(name);
	if (rwlock->rwlock_name == NULL) {
		kfree(rwlock);
		return NULL;
	}

	rwlock->reader_sem = sem_create(rwlock->rwlock_name, RW_MAX_READER);

	// rwlock->read_wchan = wchan_create(rwlock->rwlock_name);
	

	if (rwlock->reader_sem == NULL){
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	rwlock->read_write_lock = lock_create(rwlock->rwlock_name);
	if (rwlock->read_write_lock == NULL){
		sem_destroy(rwlock->reader_sem);
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	spinlock_init(&rwlock->read_lock);
	if (&rwlock->read_lock == NULL){
		lock_destroy(rwlock->read_write_lock);
		sem_destroy(rwlock->reader_sem);
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	rwlock->writer_condvar = cv_create(rwlock->rwlock_name);

	if (rwlock->writer_condvar == NULL){
		spinlock_cleanup(&rwlock->read_lock);
		lock_destroy(rwlock->read_write_lock);
		sem_destroy(rwlock->reader_sem);
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	rwlock->cv_lock = lock_create(rwlock->rwlock_name);
	if (rwlock->cv_lock == NULL){
		cv_destroy(rwlock->writer_condvar);
		spinlock_cleanup(&rwlock->read_lock);
		lock_destroy(rwlock->read_write_lock);
		sem_destroy(rwlock->reader_sem);
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	rwlock->reader_condvar = cv_create(rwlock->rwlock_name);

	if (rwlock->reader_condvar == NULL){
		lock_destroy(rwlock->cv_lock);
		cv_destroy(rwlock->writer_condvar);
		spinlock_cleanup(&rwlock->read_lock);
		lock_destroy(rwlock->read_write_lock);
		sem_destroy(rwlock->reader_sem);
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}

	spinlock_init(&rwlock->write_lock);
	if (&rwlock->write_lock == NULL){
		cv_destroy(rwlock->reader_condvar);
		lock_destroy(rwlock->cv_lock);
		cv_destroy(rwlock->writer_condvar);
		spinlock_cleanup(&rwlock->read_lock);
		lock_destroy(rwlock->read_write_lock);
		sem_destroy(rwlock->reader_sem);
		kfree(rwlock->rwlock_name);
		kfree(rwlock);
		return NULL;
	}
	

	rwlock->readers = 0;
	rwlock->writers = 0;
	rwlock->seen_readers = 0;
	rwlock->reader_has_lock = false;
	return rwlock;
};
void rwlock_destroy(struct rwlock * lock){
	KASSERT(lock != NULL);
	KASSERT(lock->readers == 0);
	KASSERT(lock->writers == 0);

	lock_destroy(lock->cv_lock);
	cv_destroy(lock->writer_condvar);
	cv_destroy(lock->reader_condvar);
	spinlock_cleanup(&lock->read_lock);
	lock_destroy(lock->read_write_lock);
	sem_destroy(lock->reader_sem);
	kfree(lock->rwlock_name);
	kfree(lock);
};

void rwlock_acquire_read(struct rwlock *lock){
	KASSERT(lock != NULL);
	P(lock->reader_sem);
	spinlock_acquire(&lock->read_lock);
	lock->readers ++;
	lock->seen_readers ++;
	/*
     *		If we have seen a certain number of 
	 *		readers and there were writers waiting, 
	 *		we could use a condvar to make readers
	 *	    sleep so that we would ensure no starvation
	*/ 
	while (lock->writers > 0 && lock->seen_readers >= RW_MAX_READER) {
		lock_acquire(lock->cv_lock);
		cv_wait(lock->writer_condvar, lock->cv_lock);
		lock_release(lock->cv_lock);
		lock->seen_readers = 0;
	}
	/*
	 * 		If a reader has gained the lock, 
	 *		then it means that other readers won't have to 
	 *		aquire the lock
	*/
	while (!lock->reader_has_lock){
		spinlock_release(&lock->read_lock);
		lock_acquire(lock->read_write_lock);
		spinlock_acquire(&lock->read_lock);
		lock->reader_has_lock = true;
	}
	spinlock_release(&lock->read_lock);
};

void rwlock_release_read(struct rwlock *lock){
	/*
	*	We should be carefull here as only one reader thread 
	*	has the mutex lock and we want that thread to sleep until all of 
	*	the other reader threads have finished reading and 
	*	then we could actully release the mutex lock	
	*/
	spinlock_acquire(&lock->read_lock);
	V(lock->reader_sem);
	lock->readers --;
	
	
	while(lock_do_i_hold(lock->read_write_lock) && lock->readers >= 1){
		lock_acquire(lock->cv_lock);
		spinlock_release(&lock->read_lock);
		cv_wait(lock->reader_condvar, lock->cv_lock);
		spinlock_acquire(&lock->read_lock);
		lock_release(lock->cv_lock);
	}
	
	/*
	*	This is the last non mutex holder reader thread 
	*	which should wake up the mutex holder reader
	*/
	if (!lock_do_i_hold(lock->read_write_lock) && lock->readers == 0){
		lock_acquire(lock->cv_lock);
		cv_signal(lock->reader_condvar, lock->cv_lock);
		lock_release(lock->cv_lock);
	}

	/*
	*	If we are the last mutex holder reader thread
	*	we should clean up everything left, mark 
	*	that reader does not have the lock and release the 
	*	read/write lock
	*/
	if (lock_do_i_hold(lock->read_write_lock) && lock->readers == 0){
		lock->reader_has_lock = false;
		lock_release(lock->read_write_lock);
	}
	spinlock_release(&lock->read_lock);
};

void rwlock_acquire_write(struct rwlock *lock){
	spinlock_acquire(&lock->write_lock);
	lock->writers ++;
	spinlock_release(&lock->write_lock);
	lock_acquire(lock->read_write_lock);
	spinlock_acquire(&lock->write_lock);
	KASSERT(!lock->reader_has_lock);
	spinlock_release(&lock->write_lock);
};

void rwlock_release_write(struct rwlock *lock){

	lock_acquire(lock->cv_lock);
	cv_broadcast(lock->writer_condvar, lock->cv_lock);
	lock_release(lock->cv_lock);
	spinlock_acquire(&lock->write_lock);
	KASSERT(!lock->reader_has_lock);
	lock->writers --;	
	lock_release(lock->read_write_lock);
	spinlock_release(&lock->write_lock);
};