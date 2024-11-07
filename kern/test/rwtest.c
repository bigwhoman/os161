/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/test161.h>
#include <spinlock.h>


#define CREATELOOPS		8
#define NSEMLOOPS     63
#define NLOCKLOOPS    120
#define NCVLOOPS      5
#define NTHREADS      32
#define SYNCHTEST_YIELDER_MAX 16
#define SYNCHTEST_YIELDER_MAX 16
#define READER_COUNT 3
#define WRITER_COUNT 1
#define MAX_READER 2

static volatile int testval;
static volatile unsigned readers;
static volatile unsigned readers_before_writer;
static volatile unsigned writers;
static volatile unsigned writers_in_system;

static struct lock *testlock = NULL;
static struct cv *readcv = NULL;
static struct rwlock *testrw = NULL;

struct spinlock consistancy_lock;
struct spinlock status_lock;
static bool test_status = TEST161_FAIL;
static bool reading = false;
static bool test_done = false;

/*
 * Use these stubs to test your reader-writer locks.
 */
#define NCVS 15
#define NLOOPS 40

static struct semaphore *exitsem;

/*
*	The evaluator thread checks the throughput of 
*	our reads and if its less than half of the max 
*   readers it would fail
*/

static
bool
failif(bool condition) {
	if (condition) {
		KASSERT(condition == !condition);
		spinlock_acquire(&status_lock);
		test_status = TEST161_FAIL;
		spinlock_release(&status_lock);
	}
	return condition;
}


static
void
evalthread(void *junk1, unsigned long junk2)
{

	(void)junk1;
	(void)junk2;
	/*
	*	See how many reader we have in system 
	*	in each epoch where read is happening
	*/
	unsigned read_epoch = 0;
	unsigned reader_in_epoch = 0; 
	unsigned int max_readers_in_time = 0;
	while(!test_done){
		while(reading){
			spinlock_acquire(&consistancy_lock);
			/*
			* This means that we have starvation
			*/
			failif((readers > MAX_READER));
			if (readers != 0){
				read_epoch ++;
				reader_in_epoch += readers;
				max_readers_in_time = max_readers_in_time > readers ? max_readers_in_time: readers;
			}
			spinlock_release(&consistancy_lock);
		}
		failif((readers != 0));
		/*
		*	Sleep while writers are writing
		*/
		lock_acquire(testlock);
		cv_wait(readcv, testlock);
		lock_release(testlock);
	}
	int throughput = reader_in_epoch/read_epoch;
	kprintf("Evaluation done...\n");
	kprintf("epochs : %d --- readers in epoch : %d --- max readers %d \n",read_epoch, reader_in_epoch, max_readers_in_time);
	kprintf("Throughput : %d\n", throughput);
	failif((throughput < MAX_READER/2));
	V(exitsem);
}

static
void
readthread(void *junk1, unsigned long junk2)
{
	(void)junk1;
	(void)junk2;

	unsigned i, j;

	random_yielder(4);
	for (j=0; j<NLOOPS; j++) {
		kprintf_t(".");
		rwlock_acquire_read(testrw);
		kprintf("Readers Started...\n");
		int startval = testval;
		reading = true;
		random_yielder(4);
		/*
		*	Wake up the evalthread to say we have 
		*	started reading
		*/
		lock_acquire(testlock);
		cv_signal(readcv, testlock);
		lock_release(testlock);
		spinlock_acquire(&consistancy_lock);
		readers++;
		/*
		*	We only count seen readers if 
		*	there is a writer waiting in the system
		*/
		if(writers_in_system > 0)
			readers_before_writer ++;
			
		spinlock_release(&consistancy_lock);

		for (i=0; i<NCVS; i++) {
			random_yielder(4);
			
			/*
			*	Make sure the values dont change in here 
			*   and also no writers are writing
			*/

			failif((testval != startval));
			failif((writers != 0));

		}
		spinlock_acquire(&consistancy_lock);
		readers--;
		spinlock_release(&consistancy_lock);
		rwlock_release_read(testrw);
	}

	V(exitsem);
}

static
void
writethread(void *junk1, unsigned long junk2)
{
	(void)junk1;
	(void)junk2;

	unsigned i, j;

	random_yielder(4);
	
	for (j=0; j<NLOOPS; j++) {
		kprintf_t(".");
		/*
		*	Count the number of writers currently in system
		*/
		spinlock_acquire(&consistancy_lock);
		writers_in_system ++;
		spinlock_release(&consistancy_lock);
		random_yielder(4);
		rwlock_acquire_write(testrw);
		kprintf("Writer Started...\n");
		/*
		*	See how many readers we have seen while
		*	a writer exists in the system.
		*	If we have seen a total of MAX_READERs before 
		*	seeing a writer then we would probably have starvation
		*/
		failif((readers_before_writer > MAX_READER));
		readers_before_writer = 0;
		reading = false;
		spinlock_acquire(&consistancy_lock);
		writers++;
		spinlock_release(&consistancy_lock);
		for (i=0; i<NCVS; i++) {
			random_yielder(4);
			testval ++;
			/*
			*	Make sure no readers are reading
			*/
			failif((readers != 0));
			random_yielder(4);
			testval --;
			failif((testval != 0));
		}


		spinlock_acquire(&consistancy_lock);
		writers--;
		spinlock_release(&consistancy_lock);
		failif((testval != 0));
		spinlock_acquire(&consistancy_lock);
		writers_in_system --;
		spinlock_release(&consistancy_lock);
		rwlock_release_write(testrw);
	}

	
	V(exitsem);
}


int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;
	unsigned i;
	int result;
	kprintf_n("Starting rwt1...\n");
	for (i=0; i<CREATELOOPS; i++) {
		kprintf_t(".");
		testlock = lock_create("testlock");
		if (testlock == NULL) {
			panic("rwt1: lock_create failed\n");
		}
		testrw = rwlock_create("testrw");
		if (testrw == NULL) {
			panic("rwt1: rw_create failed\n");
		}
		readcv = cv_create("readcv");
		if (readcv == NULL) {
			panic("rwt1: cv_create failed\n");
		}
		exitsem = sem_create("exitsem", 0);
		if (exitsem == NULL) {
			panic("rwt1: sem_create failed\n");
		}
		if (i != CREATELOOPS - 1) {
			sem_destroy(exitsem);
			lock_destroy(testlock);
			rwlock_destroy(testrw);
			cv_destroy(readcv);
		}
	}

	testval = 0;
    readers = 0;
    readers_before_writer = 0;
    writers = 0;
    writers_in_system = 0;
    reading = false;
    test_done = false;

	spinlock_init(&consistancy_lock);
	spinlock_init(&status_lock);
	test_status = TEST161_SUCCESS;

	result = thread_fork("rw1", NULL, evalthread, NULL, 0);
	if (result) {
		panic("rwt1: thread_fork failed\n");
	}

	for (i = 0; i < READER_COUNT; i++)
	{
		result = thread_fork("rw1", NULL, readthread, NULL, 0);
		if (result) {
			panic("rwt1: thread_fork failed\n");
		}
	}
	
	for (i = 0; i < WRITER_COUNT; i++)
	{
		result = thread_fork("rw1", NULL, writethread, NULL, 0);
		if (result) {
			panic("rwt1: thread_fork failed\n");
		}
	}

	


	for (i = 0; i < WRITER_COUNT + READER_COUNT; i++)
	{
		P(exitsem);
	}

	kprintf("Read Write Done...\n");
	
	test_done = true;
	lock_acquire(testlock);
	cv_signal(readcv, testlock);
	lock_release(testlock);
	P(exitsem);
	sem_destroy(exitsem);
	lock_destroy(testlock);
	rwlock_destroy(testrw);
	cv_destroy(readcv);

	kprintf_t("\n");
	success(test_status, SECRET, "rwt1");

	return 0;
}

int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt2 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt2");

	return 0;
}

int rwtest3(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt3 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt3");

	return 0;
}

int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt4 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt4");

	return 0;
}

int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("rwt5 unimplemented\n");
	success(TEST161_FAIL, SECRET, "rwt5");

	return 0;
}
