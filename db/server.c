#define _POSIX_C_SOURCE 200112L // nanosleep(2)

#include "db.h"
#include "window.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

/*
 * TODO (Part 2): This function should be implemented and called by client
 * threads to wait until progress is permitted. Ensure that the mutex is not
 * left locked if the thread is cancelled while waiting on a condition
 * variable.
 */

//a global flag indicating whether clients can handle input..
int clientBlock = 0;//1 if block
unsigned int clientsCount = 0;

pthread_mutex_t clientBlockLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t clientBlockCond = PTHREAD_COND_INITIALIZER;
pthread_barrier_t* clientsBarrier = NULL;
pthread_mutex_t clientsCountLock = PTHREAD_MUTEX_INITIALIZER;
pthread_barrier_t* releaseBarrier = NULL;

//if there is a barrier pointer as a parameter, which means the signal thread
//is waiting it to go into the barrier
static void
ClientControl_wait() {
    /* TODO (Part 2): Not yet implemented. */
    pthread_mutex_lock(&clientBlockLock);
    while(clientBlock == 1){
        //here we should waiting
        pthread_cond_wait(&clientBlockCond, &clientBlockLock);
    }
    pthread_mutex_unlock(&clientBlockLock);
    
}

/*
 * TODO (Part 2): This function should be implemented and called by the main
 * thread to stop the client threads.
 */
static void
ClientControl_stop(void) {
     //just set the bit to 1
     pthread_mutex_lock(&clientBlockLock);
     clientBlock = 1;
     pthread_mutex_unlock(&clientBlockLock);
}

/*
 * TODO (Part 2): This function should be implemented and called by the main
 * thread to resume client threads.
 */

static void
ClientControl_release() {
    /* TODO (Part 2): Not yet implemented. */
    pthread_mutex_lock(&clientBlockLock);
    clientBlock = 0;
    pthread_mutex_unlock(&clientBlockLock);
    //notify all
    pthread_cond_broadcast(&clientBlockCond);
}

/*
 * the encapsulation of a client thread, i.e., the thread that handles commands
 * from clients
 */

typedef struct Client {
#ifdef _PTHREAD_H
	pthread_t thread;
#endif
	window_t *win;
	/*
	 * The thread list (Part 3):
	 * Client threads put themselves in the list and take themselves out,
	 * thus guaranteeing that a client's being in the list means that its
	 * thread is active and thus may be safely cancelled.
	 *
	 * See also ThreadListHead and ThreadListMutex, below.
	 */
	struct Client *prev;
	struct Client *next;
        //for barrier
        pthread_barrier_t barrier;
        //also record Timeout_t
        void* timeout;
} Client_t;

/*
 * This struct helps keep track of how long it's been since a command has been
 * received by a client thread. The client thread is cancelled if it's been too
 * long. For each client thread an associated "watchdog thread" is created
 * which loops, sleeping wait_secs seconds since the last time the client
 * thread received input. At each wakeup, it checks to see if the client thread
 * has received input since the watchdog went to sleep. If not, the watchdog
 * cancels the client thread, then terminates itself. Otherwise it loops again,
 * sleeping wait_secs seconds since the last time the client thread received
 * input ...
 */
typedef struct Timeout {
	/* time when client thread "times out" */
	struct timeval timeout;

	/* set when client thread receives input */
	/* reset each time watchdog thread wakes up */
	int active;

	Client_t *client;

#ifdef _PTHREAD_H
	pthread_mutex_t timeout_lock;
	pthread_t WatchDogThread;
#endif

} Timeout_t;

//create mutex to protect the linkedlist
pthread_mutex_t listMutex;
Client_t *ThreadListHead = NULL;
Client_t *ThreadListTail = NULL;


#ifdef _PTHREAD_H
pthread_mutex_t ThreadListMutex = PTHREAD_MUTEX_INITIALIZER;
#endif

static void *RunClient(void *);
static int handle_command(const char *, char *, size_t);
static void ThreadCleanup(void *);
static void DeleteAll(void);
static void Client_destructor(Client_t* client);


//print all clients
void PrintAllThreads(){
    Client_t* traverser = ThreadListHead;
    while(traverser != NULL){
        printf("%d\n", (int)traverser -> thread);
        traverser = traverser -> next;
    }
}

//helper function, add to linkedlist of Client_t
void AddThreadList(Client_t* client){

    if(ThreadListHead == NULL){
        ThreadListHead = ThreadListTail = client;
        return;
    }
    else{
        ThreadListTail -> next = client;
        client->prev = ThreadListTail;
        ThreadListTail = client;
    }
    return;
}

//return 0 if success
//return -1 if target not found
int DeleteThreadFromList(Client_t* client){

    //special case happens in the head
    if(ThreadListHead == NULL){
        printf("No Client Thread now\n");
        return -1;
    }
    else if(ThreadListHead == ThreadListTail){
        if(ThreadListTail == client){
            Client_destructor(client);
            ThreadListTail = ThreadListHead = NULL;
            return 0;
        }
    }
    else{
        Client_t* slow = ThreadListHead;
        Client_t* fast = ThreadListHead -> next;
        //take care of the first
        if(slow == client){
            fast ->prev = NULL;
            ThreadListHead = fast;
            Client_destructor(client);
            return 0;
        }

        while(fast != NULL){
            if(fast == client){
                slow->next = fast->next;
                if(fast != ThreadListTail){
                    fast->next->prev = slow;
                }
                else{
                    ThreadListTail = slow;
                }

                Client_destructor(client);
                return 0;
            }
            slow = fast;
            fast = fast->next;
        }
    }

    return -1;
}

/*
 * TODO (Part 1): Modify this function such that a new thread is created and
 * detached that runs RunClient.
 *
 * TODO (Part 3): Note that window_constructor must be executed atomically:
 * cancellation must be disabled within it.
 *
 * TODO (Part 3): See the comment in RunClient about a barrier.
 */
int client_counter = 0;
static Client_t *
Client_constructor()
{

        char title[16];
	Client_t *new_Client = malloc(sizeof (Client_t));
	if (new_Client == NULL)
		return (NULL);
        memset(new_Client, 0, sizeof (Client_t));
	sprintf(title, "Client %d", client_counter);
        //barrier setup
        pthread_barrier_init(&new_Client->barrier, NULL, 3);
       
	/*
	 * This constructor creates a window and sets up a communication
	 * channel with it.
	 */
        //ban cancel here
	new_Client->win = window_constructor(title);
	//return (new_Client);
        int res = pthread_create(&new_Client->thread, NULL, RunClient, new_Client);
        if(res != 0){
            printf("Error creating client thread: %s\n", strerror(errno));
            //clean the client structure
            Client_destructor(new_Client);
            return NULL;
        }
        pthread_mutex_lock(&clientsCountLock);
        client_counter ++;
        clientsCount ++;
        pthread_mutex_unlock(&clientsCountLock);
        
        res = pthread_detach(new_Client->thread);
        if(res != 0){
            printf("detach error\n");
        }
        return new_Client;
}

/*
 * TODO (Part 3): Destroy this timeout instance. Be sure to cancel the watchdog
 * thread and join with it.
 */
static void
Timeout_destructor(Timeout_t *timeout)
{
	/* TODO (Part 3): Not yet implemented. */
        if(timeout == NULL)
            return;

        pthread_t tid = timeout->WatchDogThread;
        pthread_cancel(tid);
        pthread_join(tid, NULL);
 //       free(timeout);
        
        return;
}


static void
Client_destructor(Client_t *client)
{
        //printf("in destructor!\n");
	window_destructor(client->win);
        //delete watchdog
        Timeout_t* tc = (Timeout_t*)client->timeout;
        
        pthread_mutex_lock(&clientsCountLock);
        clientsCount --;
        pthread_mutex_unlock(&clientsCountLock);

        if(clientsBarrier != NULL){
            pthread_barrier_wait(clientsBarrier);
        }
        //printf("client count:%d\n", clientsCount);
        Timeout_destructor(tc);
        client->next = NULL;
        client->prev = NULL;

        pthread_mutex_unlock(&clientBlockLock);
        pthread_barrier_destroy(&client->barrier);
	//free(client);

        return;
}

/*
 * TODO (Part 3): This function cancels every thread in the list (see
 * ThreadListHead). Each thread must remove itself from the thread list.
 */
static void
DeleteAll()
{
	/* TODO (Part 3): Not yet implemented. */
        //traverse the list, call cancel.
        Client_t* traverser = ThreadListHead;
        clientsBarrier = malloc(sizeof (pthread_barrier_t));
        pthread_mutex_lock(&clientsCountLock);
        printf("Client number in delete all: %d\n", clientsCount);
        pthread_barrier_init(clientsBarrier, NULL, clientsCount + 1);
        pthread_mutex_unlock(&clientsCountLock);
        while(traverser != NULL){
            Client_t* todelete = traverser;
            pthread_t tid = todelete->thread;
            traverser = traverser -> next;
            pthread_cancel(tid);
            free(todelete->timeout);
            free(todelete);
        }
        pthread_barrier_wait(clientsBarrier);
        //printf("exit barrier");
        ThreadListHead = ThreadListTail = NULL;
        pthread_barrier_destroy(clientsBarrier);
        free(clientsBarrier);
        clientsBarrier = NULL;
        return;
}

/*
 * This is the cleanup routine for client threads, called on cancel and exit.
 *
 * TODO (Part 1): This function should free the client instance by calling
 * Client_destructor.
 *
 * TODO (Part 3): This function pulls the client thread from the thread list
 * (see ThreadListHead) and deletes it. Thus the client thread must be in the
 * thread list before the thread acts on a cancel.
 */
static void
ThreadCleanup(void *arg)
{
	//printf("Inside Cleanup\n");
        Client_t *client = arg;
        //pthread_mutex_lock(&listMutex);
        int res = DeleteThreadFromList(client);
        //pthread_mutex_unlock(&listMutex);
	if(res == -1){
            printf("Not found target client struct in ThreadCleanup()\n");
        }
}

static time_t Timeout_wait_secs = 5; /* timeout interval */

static void Timeout_reset(Timeout_t *);
static void *Timeout_WatchDog(void *); /* watchdog thread */

/*
 * TODO (Part 3): Allocate a new timeout instance (described by a Timeout_t)
 * and initialize its members. Set the initial timeout to Timeout_wait_secs
 * from the current time; see gettimeofday(2) and friends. Finally, create a
 * watchdog thread for this timeout.
 */
static Timeout_t *
Timeout_constructor(Client_t *a_client)
{
	/* TODO (Part 3): Not yet implemented. */
        Timeout_t* tc = malloc(sizeof (Timeout_t));
        memset(tc, 0, sizeof (Timeout_t));
        gettimeofday(&tc->timeout, NULL);
        tc->timeout.tv_sec = tc->timeout.tv_sec + Timeout_wait_secs;
        a_client->timeout = tc;
        tc->active = 0;
        tc->client = a_client;
        pthread_mutex_init(&tc->timeout_lock, NULL);
        //initialize a thread
        int res = pthread_create(&tc->WatchDogThread, NULL, Timeout_WatchDog, (void*) tc);
        if(res != 0){
            printf("Error creating Watchdog thread: %s\n", strerror(errno));
            //clean the client structure
            free(tc);
            return NULL;
        }
	return tc;
}

/*
 * This function contains code executed by the client.
 *
 * TODO (Part 1): Make sure that the client instance is properly freed when the
 * thread terminates by calling ThreadCleanup. We strongly suggest you use
 * pthread_cleanup_push(3) and pthread_cleanup_pop(3) for this purpose.
 *
 * TODO (Part 3): Make sure that the timeout instance (and its watchdog thread)
 * is properly freed when the thread terminates by calling Timeout_destructor.
 * We strongly suggest you use pthread_cleanup_push(3) and
 * pthread_cleanup_pop(3) for this purpose.
 */
static void *
RunClient(void *arg)
{

	Client_t *client = arg;
	/*
	 * Establish our watchdog thread to deal with timeouts. Timeout's
	 * destructor cancels the watchdog thread and doesn't return until
	 * after joining with it.
	 */
	Timeout_t *timeout = Timeout_constructor(client);
        
        /* set the cancel function, and cancel machanism */
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
	pthread_cleanup_push(ThreadCleanup, (void*)client);
 
        pthread_mutex_lock(&listMutex);
        AddThreadList(client);
        pthread_mutex_unlock(&listMutex);

	/* The client thread is now fully started. */
	/*
	 * TODO (Part 3): Other threads waiting for this thread need to be
	 * notified somehow. (Hint: Consider the watchdog thread, which
	 * shouldn't worry about timeouts till now, and the main thread, which
	 * must not call DeleteAll until this thread is fully started. A
	 * barrier might be useful.)
	 */
        pthread_barrier_wait(&client->barrier);
	/*
	 * main loop of the client: fetch commands from window, interpret and
	 * handle them, return results to window
	 */
	{

                char command[256];
		/* response must be NULL for the first call to serve */
		char response[256] = {0};
                serve(client->win, response, command);
                /* we've received input: reset timer */
		Timeout_reset(timeout);
		while (handle_command(command, response, sizeof (response))) {
			/* we've processed a command: reset timer */
                        pthread_testcancel();
                        ClientControl_wait();
                        pthread_testcancel();
                        serve(client->win, response, command);
			/* we've received input: reset timer */
                        Timeout_reset(timeout);
                }
        }
        pthread_cleanup_pop(1);

	return (NULL);
}

static int
handle_command(const char *command, char *response, size_t len)
{
	if (command[0] == EOF) {
		strncpy(response, "all done", len - 1);
		return (0);
	} else {
		interpret_command(command, response, len);
        }
	return (1);
}

/*
 * TODO (Part 3): This function is called by client threads each time they
 * receive a command. It stores the current time + Timeout_wait_secs in
 * timeout; see gettimeofday(2)_and friends. This function also indicates that
 * new input has been received. If the timeout expires and no further input has
 * been received, then the watchdog thread cancels us. Note: be sure to lock
 * the appropriate mutex!
 */
void
Timeout_reset(Timeout_t *timeout)
{
	/* TODO (Part 3): Not yet implemented. */
        pthread_mutex_lock(&timeout->timeout_lock);
        gettimeofday(&timeout->timeout, NULL);
        timeout->timeout.tv_sec = timeout->timeout.tv_sec + Timeout_wait_secs;
        timeout->active = 1;
        pthread_mutex_unlock(&timeout->timeout_lock);
}

/*
 * There is one watchdog thread per client thread. It cancels the client if it
 * hasn't received a command within the specified time period (stored in
 * wait_secs).
 */
void *
Timeout_WatchDog(void *arg)
{

	Timeout_t *timeout = arg;
        pthread_barrier_wait(&timeout->client->barrier);
	struct timespec to;

	to.tv_sec = Timeout_wait_secs;
	to.tv_nsec = 0;

	while (1) {
                ClientControl_wait();
		struct timeval now;
                /*
		 * sleep for the current timeout interval
		 * (since thread was started or last input)
		 */
		if (nanosleep(&to, NULL) == -1) {
			perror("nanosleep");
			exit(EXIT_FAILURE);
		}

		if (timeout->active == 0) {
			/*
			 * TODO (Part 3): Our client thread hasn't received
			 * input in a while, so cancel it (and self.
			 */
                        pthread_t tid = timeout->client->thread;
                        pthread_cancel(tid);        
                        pthread_join(tid, NULL);
                        Timeout_destructor(timeout);
                        return 0;

		} else {
			timeout->active = 0;
		}

		/*
		 * Set the next timeout to be the requested number of seconds
		 * since the last client input; convert an absolute timeout to
		 * an interval.
		 */
#ifdef _PTHREAD_H
		pthread_mutex_lock(&timeout->timeout_lock);
#endif
		gettimeofday(&now, NULL);
		to.tv_sec = timeout->timeout.tv_sec - now.tv_sec;
		to.tv_nsec = 1000 * (timeout->timeout.tv_usec - now.tv_usec);
		if (to.tv_nsec < 0) {
			/* "borrow" from seconds */
			to.tv_sec--;
			to.tv_nsec += 1000000000;
		}
#ifdef _PTHREAD_H
		pthread_mutex_unlock(&timeout->timeout_lock);
#endif
	}

	return (NULL);
}

typedef struct SigHandler {
	sigset_t set;
#ifdef _PTHREAD_H
	pthread_t SigThread;
#endif
} SigHandler_t;

static void *SigMon(void *);

/*
 * TODO (Part 3): Allocate a new instance of a signal handler (SigHandler_t),
 * mask off Ctrl-C (SIGINT, see sigsetops(3)), and create the signal-handling
 * thread (SigMon).
 */
static SigHandler_t *
SigHandler_constructor()
{
	/* TODO (Part 3): Not yet implemented. */
        SigHandler_t* sigHandler = malloc(sizeof (SigHandler_t));
        memset(sigHandler, 0, sizeof (SigHandler_t));
        sigemptyset(&sigHandler->set);
        sigaddset(&sigHandler->set, SIGINT);
        pthread_sigmask(SIG_BLOCK, &sigHandler->set, 0);
        pthread_create(&sigHandler->SigThread, 0, SigMon, (void*)&sigHandler->set);
	return sigHandler;
}

/*
 * TODO (Part 3): Destroy an instance of a signal handler. Be sure to cancel
 * the signal-handling thread and join with it.
 */
static void
SigHandler_destructor(SigHandler_t *sighandler)
{
	/* TODO (Part 3): Not yet implemented. */
        pthread_t tid = sighandler->SigThread;
        pthread_cancel(tid);
        pthread_join(tid,NULL);
        //delete the set
        free(sighandler);
}

/*
 * This function contains the code for the signal-handling thread.
 *
 * TODO (Part 3): When a ctrl-C occurs, delete all current client
 * threads with DeleteAll.
 */
static void *
SigMon(void *arg) {
	sigset_t *set = arg;
        int sig;
        while(1){
            sigwait(set, &sig);
            int isBlocking = 0;
            if(clientBlock == 1){
                isBlocking = 1;
                ClientControl_release();
                sleep(1);//sleep for one second to wait for all clients to start continue working
            }
            DeleteAll();
            if(isBlocking == 1)
                ClientControl_stop();
        }
	return (NULL);
}

/*
 * TODO (Part 1): Modify this function so that no window is created
 * automatically. Instead, every time you type enter (in the window in which
 * you're running server), a new window is created along with a new thread to
 * handle it.
 */
#define MAX_LENGTH 255

int
main(int argc, char *argv[])
{
	//Client_t *c;
	SigHandler_t *sig_handler;

	if (argc == 2) {
		Timeout_wait_secs = atoi(argv[1]);
	} else if (argc > 2) {
		fprintf(stderr, "Usage: server [timeout_secs]\n");
		exit(EXIT_FAILURE);
	}

	sig_handler = SigHandler_constructor();
        pthread_rwlock_init(&coarseDBLock, NULL);
        pthread_mutex_init(&listMutex, NULL);
        //initialize the lock for head
        char* command = (char*) malloc(MAX_LENGTH);
        
        while(1){
            //capture user action(pressing enter)
            int byte_read = read(STDIN_FILENO, command, MAX_LENGTH);
            if(byte_read == 0){
                break;
            }
            else if(command[0] == 's' && command[1] == '\n'){
                printf("stopped\n");
                ClientControl_stop();
            }
            else if(command[0] == 'g' && command[1] == '\n'){
                printf("released\n");
                ClientControl_release();
            }
            else if(command[0] == 'p' && command[1] == '\n'){
                PrintAllThreads();
            }
            else if(command[byte_read - 1 ] == 10){
                Client_t* c = Client_constructor();
                pthread_barrier_wait(&c->barrier);
            }
            else{
                write(STDOUT_FILENO, "\n", 2);
            }
            memset(command, MAX_LENGTH, 0);
        }
        free(command);
        ClientControl_release();
        pthread_mutex_destroy(&clientBlockLock);
        pthread_mutex_destroy(&clientsCountLock);
        pthread_cond_destroy(&clientBlockCond);
	pthread_rwlock_destroy(&coarseDBLock);
        DeleteAll();
	cleanup_db();
        SigHandler_destructor(sig_handler);
        pthread_mutex_destroy(&listMutex);

	return (EXIT_SUCCESS);
}


