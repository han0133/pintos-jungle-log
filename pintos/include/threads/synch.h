#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/*
 * [2] struct semaphore - 세마포어
 * ┌─────────────────────────────┐
 * │ struct semaphore            │
 * ├─────────────────────────────┤
 * │ value: unsigned             │ ← 사용 가능한 자원 수
 * │ waiters: list               │ ← 대기 중인 스레드들의 list
 * └─────────────────────────────┘
 *         │
 *         │ waiters는 thread->elem을 통해 연결
 *         ↓
 *   [thread1->elem] → [thread2->elem] → [thread3->elem]
 */

/* A counting semaphore. */
struct semaphore
{
	unsigned value;			 /* Current value. */
	struct list waiters; /* 세마포어를 기다리는 스레드 줄 */
};

struct semaphore_elem
{
	struct list_elem elem;
	struct semaphore semaphore;
};

void sema_init(struct semaphore *, unsigned value);
void sema_down(struct semaphore *);
bool sema_try_down(struct semaphore *);
void sema_up(struct semaphore *);
void sema_self_test(void);

bool compare_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux);

/* Lock. */
struct lock
{
	struct thread *holder;			/* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
};

void lock_init(struct lock *);
void lock_acquire(struct lock *);
bool lock_try_acquire(struct lock *);
void lock_release(struct lock *);
bool lock_held_by_current_thread(const struct lock *);
void recaculate_priority(void);

/* Condition variable. */
struct condition
{
	struct list waiters; /* List of waiting threads. */
};

void cond_init(struct condition *);
void cond_wait(struct condition *, struct lock *);
void cond_signal(struct condition *, struct lock *);
void cond_broadcast(struct condition *, struct lock *);

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile("" : : : "memory")

#endif /* threads/synch.h */
