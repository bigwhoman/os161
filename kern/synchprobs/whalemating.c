/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>


struct cv * malecv;
struct cv * femalecv;
struct cv * matchmakercv;
struct lock * malelock;
struct lock * femalelock;
struct lock * matchmakerlock;
struct lock * datalock;
struct semaphore * male_done;
struct semaphore * female_done;
struct semaphore * female_can_end;
struct semaphore * male_can_end;
unsigned male_count;
unsigned female_count;
unsigned matchmaker_count;
/*
 * Called by the driver during initialization.
 */

void whalemating_init() {
	malecv = cv_create("Male Wait");
	femalecv = cv_create("Female Wait");
	matchmakercv = cv_create("Matchmaker Wait");
	malelock = lock_create("Male Lock");
	femalelock = lock_create("Female Lock");
	matchmakerlock = lock_create("Matchmaker Lock");
	datalock = lock_create("data Lock");
	male_done = sem_create("Male Done", 0);
	female_done = sem_create("Female Done", 0);
	male_can_end = sem_create("Male ed", 0);
	female_can_end = sem_create("Female ed", 0);
	male_count = 0;
	female_count = 0;
	matchmaker_count = 0;
	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	return;
}

void
male(uint32_t index)
{
	(void)index;
	/*
	 * Implement this function by calling male_start and male_end when
	 * appropriate.
	 */
	
	lock_acquire(malelock);
	male_start(index);
	lock_acquire(datalock);
	male_count ++;
	lock_release(datalock);
	lock_acquire(matchmakerlock);
	cv_signal(matchmakercv, matchmakerlock);
	lock_release(matchmakerlock);
	lock_acquire(datalock);
	while( female_count == 0 || matchmaker_count == 0){
		lock_release(datalock);
		cv_wait(malecv, malelock);
		lock_acquire(datalock);
	}
	kprintf("loopa\n");
	lock_release(datalock);
	V(male_done);
	P(male_can_end);
	lock_acquire(datalock);
	male_count --;
	lock_release(datalock);
	male_end(index);
	lock_release(malelock);
	
	return;
}

void
female(uint32_t index)
{
	(void)index;
	/*
	 * Implement this function by calling female_start and female_end when
	 * appropriate.
	 */
	
	lock_acquire(femalelock);
	female_start(index);
	lock_acquire(datalock);
	female_count ++;
	lock_release(datalock);
	lock_acquire(matchmakerlock);
	cv_signal(matchmakercv, matchmakerlock);
	lock_release(matchmakerlock);
	lock_acquire(datalock);
	while(male_count == 0 || matchmaker_count == 0){
		lock_release(datalock);
		cv_wait(femalecv, femalelock);
		lock_acquire(datalock);
	}
	kprintf("doopa\n");
	lock_release(datalock);
	V(female_done);
	P(female_can_end);
	lock_acquire(datalock);
	female_count --;
	lock_release(datalock);
	female_end(index);
	lock_release(femalelock);
	
	return;
}

void
matchmaker(uint32_t index)
{
	(void)index;
	/*
	 * Implement this function by calling matchmaker_start and matchmaker_end
	 * when appropriate.
	 */
	matchmaker_start(index);
	lock_acquire(matchmakerlock);
	
	lock_acquire(datalock);
	while(male_count == 0 || female_count == 0){
		lock_release(datalock);
		cv_wait(matchmakercv, matchmakerlock);
		lock_acquire(datalock);
	}
	kprintf("koopa\n");
	matchmaker_count ++;
	lock_release(datalock);
	lock_acquire(malelock);
	cv_signal(malecv, malelock);
	lock_release(malelock);
	lock_acquire(femalelock);
	cv_signal(femalecv, femalelock);
	lock_release(femalelock);
	P(female_done);
	P(male_done);
	lock_acquire(datalock);
	matchmaker_count --;
	lock_release(datalock);
	V(male_can_end);
	V(female_can_end);
	lock_release(matchmakerlock);
	matchmaker_end(index);
	
	return;
}
