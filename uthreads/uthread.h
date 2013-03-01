/*
 *   FILE: uthread.h 
 * AUTHOR: Peter Demoreuille
 *  DESCR: userland threads
 *   DATE: Sat Sep  8 10:56:08 2001
 *
 */

#ifndef __uthread_h__
#define __uthread_h__

#include <sys/types.h>
#include "uthread_ctx.h"
#include "list.h"


/* -------------- defs -- */

#define UTH_MAXPRIO		7		/* max thread prio */
#define UTH_MAX_UTHREADS	64		/* max threads */
#define	UTH_STACK_SIZE		64*1024		/* stack size */

#define NOT_YET_IMPLEMENTED(msg) \
    do { \
        fprintf(stderr, "Not yet implemented at %s:%i -- %s\n", \
            __FILE__, __LINE__, (msg)); \
    } while(0);

#define	PANIC(err) \
	do { \
		fprintf(stderr, "PANIC at %s:%i -- %s\n", \
			__FILE__, __LINE__, err); \
		abort(); \
	} while(0);

#undef errno
#define	errno	(ut_curthr->ut_errno)


typedef int uthread_id_t;
typedef void(*uthread_func_t)(long, void*);




typedef enum
{
	UT_NO_STATE,		/* invalid thread state */
	UT_ON_CPU,		    /* thread is running */
	UT_RUNNABLE,		/* thread is runnable */
	UT_WAIT,		    /* thread is blocked */
	UT_ZOMBIE,		    /* zombie threads eat your brains! */

	UT_NUM_THREAD_STATES
} uthread_state_t;

/* --------------- thread structure -- */

typedef struct uthread {
    list_link_t		ut_link;	/* link on waitqueue / scheduler */

    uthread_ctx_t	ut_ctx;		/* context */
    char	*ut_stack;	        /* user stack */

    uthread_id_t	ut_id;		/* thread's id */
    uthread_state_t	ut_state;	    /* thread state */
    int			ut_prio;	    /* thread's priority */
    int			ut_errno;	    /* thread's errno */
    int			ut_has_exited;	/* thread exited? */
    int			ut_exit;	    /* thread's exit value */

    int			ut_detached;	/* thread is detached? */
    struct uthread	*ut_waiter;	/* thread waiting to join with me */
} uthread_t;





/* --------------- prototypes -- */

extern uthread_t uthreads[UTH_MAX_UTHREADS];
extern uthread_t *ut_curthr;
extern int uthread_id_bitmap[UTH_MAX_UTHREADS];

void uthread_init(void);

int uthread_create(uthread_id_t *id, uthread_func_t func, long arg1, 
		   void *arg2, int prio);
void uthread_exit(int status);
uthread_id_t uthread_self(void);

int uthread_join(uthread_id_t id, int *exit_value);
int uthread_detach(uthread_id_t id);

void uthread_setprio(uthread_id_t id, int prio);
void uthread_yield(void);
void uthread_block(void);
void uthread_wake(uthread_t *uthr);

//add by me
void uthread_add_to_runnable_queue(uthread_t* uthr);

#endif /* __uthread_h__ */
