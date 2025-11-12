#include <stdio.h>
#include <string.h>
#include "threads/synch.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#define MAX_DONATION_DEPTH 8

/**
 * @brief 세마포어를 초기화하는 함수
 *
 * @param sema 초기화할 세마포어 구조체 포인터
 * @param value 세마포어의 초기값 (사용 가능한 리소스 수)
 *
 * @details 세마포어 값을 주어진 value로 설정하고,
 *          대기 중인 스레드를 저장할 빈 waiters 리스트를 초기화한다.
 *          이 함수는 세마포어 사용 전 반드시 호출되어야 한다.
 *
 * @note value가 0 또는 1이 이진 세마포어(Binary Semaphore)로 동작하며,
 *       하나의 자원에 대한 상호 배제를 구현한다.
 */
void sema_init(struct semaphore *sema, unsigned value)
{
	ASSERT(sema != NULL);

	sema->value = value;
	list_init(&sema->waiters);
}

/**
 * @brief 세마포어의 down 연산을 수행하는 함수
 *
 * @param sema 대기 및 값을 감소시킬 세마포어의 포인터
 *
 * @details 세마포어의 값이 0이면 자원을 획득할 수 없으므로, 현재 스레드는
 *          waiters 리스트에 추가되고 thread_block()을 호출해 BLOCK된다.
 *          값이 1 이상이 되면 깨어나서 값을 1 줄인다.
 *
 * @note 동작 순서:
 *       1. 인터럽트 비활성화 (원자성 보장)
 *       2. 값이 0이면:
 *          a. waiters 리스트에 현재 스레드 추가
 *             - 기존: list_push_back()
 *             - 개선: 우선순위 순 정렬 삽입 (list_insert_ordered)
 *          b. thread_block()으로 스레드를 블록 (대기 상태 전환)
 *          c. 다른 스레드가 sema_up()으로 신호를 보내야만 깨어날 수 있음
 *       3. 값이 1 이상이면 바로 1 감소하고 자원 획득
 *       4. 인터럽트 복원
 *
 * @note Priority Scheduling 구현:
 *       - waiters 리스트를 우선순위 순서대로 삽입하여 우선순위가 높은 대기자가 먼저 깨게 함
 *       - compare_ready_priority()로 비교
 *
 * @note 인터럽트와 sleep:
 *       - 인터럽트가 꺼진 상태에서도 호출 가능 (원자성)
 *       - 단, thread_block()으로 잠들면 다음 스케줄된 스레드가 인터럽트를 켜줌
 *       - 따라서 잠들 때 현재 스레드가 인터럽트 상태를 복구하는 것이 아님
 *       - 인터럽트 핸들러에서는 사용 금지 (블로킹 불가)
 *
 * @warning 다음 조건들이 만족되지 않으면 ASSERT 실패:
 *          - sema != NULL
 *          - 인터럽트 컨텍스트 아님 (!intr_context())
 *
 * @see sema_up()
 * @see thread_block()
 * @see list_insert_ordered()
 * @see compare_ready_priority()
 */
void sema_down(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);
	ASSERT(!intr_context());

	old_level = intr_disable();

	while (sema->value == 0)
	{
		list_insert_ordered(&sema->waiters, &thread_current()->elem, compare_ready_priority, NULL);
		thread_block();
	}

	sema->value--;

	intr_set_level(old_level);
}

bool sema_try_down(struct semaphore *sema)
{
	enum intr_level old_level;
	bool success;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level(old_level);

	return success;
}

/**
 * @brief 세마포어 값을 1 증가시키고, 대기 중인 스레드가 있으면 가장 높은 우선순위의 스레드를 깨우는 함수
 *
 * @param sema 값을 증가시킬 세마포어의 포인터
 *
 * @details sema->value를 1 증가시키고, waiters 리스트가 비어 있지 않으면
 *          우선순위가 가장 높은 스레드부터 깨운다.
 *          대기 중인 스레드는 thread_unblock()을 통해 ready_list에 추가된다.
 *
 * @note 동작 순서:
 *       1. 인터럽트 비활성화로 원자성 보장
 *       2. waiters 리스트가 비어 있지 않으면 compare_ready_priority로 정렬
 *       3. 가장 높은 우선순위의 스레드(맨 앞)를 pop하여 thread_unblock() 호출
 *       4. value를 1 증가
 *       5. preemption_by_priority()로 즉시 스케줄링 우선순위 확인
 *       6. 인터럽트 복원
 *
 * @note Priority Scheduling:
 *       - waiters 리스트를 우선순위 순으로 정렬 (list_sort)
 *       - 그림자 대기 중인 스레드가 여러 명 있을 때 우선순위 역전을 방지
 *
 * @note 인터럽트 핸들러 지원:
 *       - 블로킹 없이 동작하므로 인터럽트 핸들러 내에서 호출 가능
 *
 * @see sema_down()
 * @see compare_ready_priority()
 * @see thread_unblock()
 */
void sema_up(struct semaphore *sema)
{
	enum intr_level old_level;

	ASSERT(sema != NULL);

	old_level = intr_disable();
	// 대기자(waiters) 중 가장 높은 우선순위 스레드 깨우기

	if (!list_empty(&sema->waiters))
	{
		// 가장 높은 순위의 스레드를 깨우기 위해 정렬
		list_sort(&sema->waiters, compare_ready_priority, NULL);
		// 자고 있던 스레드 깨워서 ready_list에 넣는다.
		thread_unblock(list_entry(list_pop_front(&sema->waiters), struct thread, elem));
	}

	sema->value++;

	// 우선순위 기반 선점 스케줄링 체크
	preemption_by_priority();

	intr_set_level(old_level);
}

/**
 * @brief 조건 변수 waiters 리스트에서 우선순위 비교 함수
 *
 * @param a 비교 대상 첫 번째 요소의 list_elem 포인터
 * @param b 비교 대상 두 번째 요소의 list_elem 포인터
 * @param aux 부가 정보(NULL, 미사용)
 * @return true a가 b보다 우선순위가 높으면 true, 아니면 false
 *
 * @details semaphore_elem 구조체를 기반으로, 각 대기 semaphore의 첫 waiters(대기 스레드) 우선순위를 비교합니다.
 *          내부적으로 compare_ready_priority()를 호출하여 우선순위를 판정.
 *
 * @note 사용 위치:
 *       - condition variable의 waiters 리스트를 정렬할 때 사용 (list_insert_ordered, list_sort)
 *       - 신호/방출(cond_signal, cond_broadcast) 시 우선순위 높은 스레드부터 처리하도록 함
 *
 * @see semaphore_elem
 * @see compare_ready_priority()
 */
bool compare_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux)
{
	struct semaphore_elem *sema_a = list_entry(a, struct semaphore_elem, elem);
	struct semaphore_elem *sema_b = list_entry(b, struct semaphore_elem, elem);

	// 각 대기 semaphore에서 대기 중인 첫 스레드의 우선순위 비교
	struct list_elem *thread_a = list_begin(&sema_a->semaphore.waiters);
	struct list_elem *thread_b = list_begin(&sema_b->semaphore.waiters);
	return compare_ready_priority(thread_a, thread_b, NULL);
}

static void sema_test_helper(void *sema_);

void sema_self_test(void)
{
	struct semaphore sema[2];
	int i;

	printf("Testing semaphores...");
	sema_init(&sema[0], 0);
	sema_init(&sema[1], 0);
	thread_create("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up(&sema[0]);
		sema_down(&sema[1]);
	}
	printf("done.\n");
}

static void
sema_test_helper(void *sema_)
{
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down(&sema[0]);
		sema_up(&sema[1]);
	}
}

/**
 * @brief 락을 초기화하는 함수
 *
 * @param lock 초기화할 락의 포인터
 *
 * @details 락을 사용 가능한 상태로 초기화합니다. 초기 상태에서는 어떤 스레드도
 *          락을 소유하지 않으며(holder = NULL), 내부 세마포어의 값은 1로 설정된다.
 *
 * @note 락(Lock)의 특징:
 *       - 한 번에 하나의 스레드만 락을 소유 가능
 *       - 재귀적 사용 불가: 같은 스레드가 이미 보유한 락을 다시 획득할 수 없음
 *       - 소유자 개념: 락을 획득한 스레드만 해제 가능
 *       - 세마포어(초기값 1)를 기반으로 구현됨
 *
 * @note 락과 세마포어의 차이점:
 * 				 락은 상호 배제(mutual exclusion),
 *         세마포어는 동기화 및 리소스 카운팅
 *
 * @note 재귀적 사용 불가:
 *       - Pintos의 락은 "non-recursive"입니다.
 *       - 현재 락을 보유한 스레드가 같은 락을 다시 획득하려 하면 오류 발생
 *       - 재귀적 사용이 필요하다면 세마포어를 사용해야 함
 *
 * @warning lock 포인터가 NULL이면 ASSERT 실패로 커널 패닉이 발생
 *
 * @see lock_acquire()
 * @see lock_release()
 * @see sema_init()
 */
void lock_init(struct lock *lock)
{
	ASSERT(lock != NULL);

	lock->holder = NULL;

	// 내부 세마포어를 1로 초기화
	sema_init(&lock->semaphore, 1);
}

/**
 * @brief 락을 획득하는 함수 (Priority Donation 지원)
 *
 * @param lock 획득할 락의 포인터
 *
 * @details 현재 스레드가 락을 획득합니다. 락이 이미 다른 스레드에 의해 보유되어
 *          있다면, 우선순위 기부를 수행한 후 락이 해제될 때까지 블로킹한다.
 *          락 획득에 성공하면 현재 스레드가 락의 소유자가 된다.
 *
 * @note 동작 순서:
 *       1. 락이 사용 중인지 확인 (lock->holder != NULL)
 *       2. 사용 중이면:
 *          a. 현재 스레드의 waiting_lock에 이 락을 기록
 *          b. 락 소유자의 donators 리스트에 자신을 추가
 *          c. donate_priority()로 재귀적 우선순위 기부
 *       3. sema_down()으로 락이 해제될 때까지 대기
 *       4. 락 획득 후:
 *          a. lock->holder를 현재 스레드로 설정
 *          b. waiting_lock을 NULL로 초기화 (더 이상 대기 중 아님)
 *
 * @note Priority Donation:
 *       - 락 소유자의 우선순위가 현재 스레드보다 낮으면 기부
 *       - donators 리스트는 우선순위 순으로 정렬되어 관리됨
 *       - 중첩 기부(nested donation) 지원: 최대 8단계까지 연쇄 전파
 *
 * @note waiting_lock의 역할:
 *       - 현재 스레드가 어떤 락을 기다리고 있는지 추적
 *       - lock_release() 시 해당 락 관련 기부만 선택적으로 제거하기 위함
 *       - donate_priority()의 중첩 기부 체인 구성에 사용
 *
 * @note 블로킹 특성:
 *       - 이 함수는 스레드를 블로킹시킬 수 있으므로 인터럽트 핸들러에서 호출 금지
 *       - 인터럽트가 비활성화된 상태에서 호출 가능 (블로킹 시 자동 활성화)
 *
 * @warning 다음 조건들이 만족되지 않으면 ASSERT 실패:
 *          - lock != NULL
 *          - 인터럽트 컨텍스트가 아님 (!intr_context())
 *          - 현재 스레드가 이미 이 lock을 보유하지 않음 (재진입 불가)
 *
 * @see lock_release()
 * @see lock_try_acquire()
 * @see donate_priority()
 * @see remove_donations()
 */
void lock_acquire(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(!lock_held_by_current_thread(lock));

	// 1. 락이 현재 다른 스레드에 의해 사용 중인가?
	if (lock->holder != NULL)
	{
		// 현재 스레드가 어떤 락을 기다리는지 기록
		// (lock_release() 시 해당 락 관련 기부만 제거하기 위함)
		thread_current()->waiting_lock = lock;

		// 락 소유자의 기부자 명단(donators)에 현재 스레드를 우선순위 순으로 추가
		list_insert_ordered(&lock->holder->donators, &thread_current()->donation_elem, compare_donation_priority, NULL);

		// 재귀적 우선순위 기부 수행 (중첩 기부 지원)
		donate_priority(lock->holder);
	}

	// 2. 락이 해제될 때까지 대기 (블로킹)
	sema_down(&lock->semaphore);

	// 3. 락 획득 성공: 현재 스레드가 새 소유자가 됨
	lock->holder = thread_current();

	// 4. 더 이상 락을 기다리지 않으므로 waiting_lock 초기화
	thread_current()->waiting_lock = NULL;
}

/**
 * @brief 락 소유자에게 우선순위를 기부하는 함수 (nested donation)
 *
 * @param holder 우선순위를 기부받을 스레드 (락을 보유한 스레드)
 *
 * @details 현재 스레드가 락을 기다리는 동안, 해당 락을 보유한 스레드(holder)에게
 *          자신의 우선순위를 기부한다. 만약 holder도 다른 락을 기다리고 있다면
 *          연쇄적으로(재귀적으로) 우선순위를 전파
 *
 * @note Priority Donation 동작:
 *       1. 현재 스레드의 우선순위가 holder보다 높으면 기부
 *       2. holder의 priority를 현재 스레드의 priority로 상향 조정
 *       3. holder가 다른 락을 기다리고 있으면 그 락의 소유자에게도 기부
 *       4. 최대 MAX_DONATION_DEPTH(8)까지 연쇄 기부 허용
 *
 * @note MAX_DONATION_DEPTH 제한: 8단계
 *
 * @note 종료 조건:
 *       - holder == NULL: 더 이상 기부할 대상이 없음
 *       - depth >= MAX_DONATION_DEPTH: 최대 깊이 도달
 *       - holder->waiting_lock == NULL: holder가 대기 중이 아님
 *
 * @warning 이 함수는 lock_acquire() 내에서 호출되며, 인터럽트가 비활성화된
 *          상태에서 실행되어야한다.
 *
 * @see lock_acquire()
 * @see remove_donations()
 * @see recalculate_priority()
 */
void donate_priority(struct thread *holder)
{
	struct thread *curr = thread_current();
	int depth = 0;

	// holder가 존재하고 최대 깊이에 도달하지 않았을 때까지 반복
	while (holder != NULL && depth < MAX_DONATION_DEPTH)
	{
		// 내 우선순위가 holder의 우선순위보다 높으면 기부
		if (curr->priority > holder->priority)
		{
			holder->priority = curr->priority;

			// ready_list에 holder thread가 있으면 재정렬
			if (holder->status == THREAD_READY)
			{
				list_remove(&holder->elem);
				list_insert_ordered(&ready_list, &holder->elem, compare_ready_priority, NULL);
			}
		}
		// 중첩 기부: holder가 다른 락을 기다리고 있으면 재귀적 기부
		if (holder->waiting_lock != NULL)
		{
			// 그 락의 소유자에게도 우선순위를 전파
			holder = holder->waiting_lock->holder;
			depth++; // 깊이 증가
		}
		else
		{
			// holder가 대기 중이 아니면 종료
			break;
		}
	}
}

/**
 * @brief 락 획득을 시도하되 블로킹하지 않는 함수
 *
 * @param lock 획득을 시도할 락의 포인터
 *
 * @return true  락 획득에 성공한 경우
 * @return false 락이 이미 다른 스레드에 의해 보유된 경우 (획득 실패)
 *
 * @details 이 함수는 락 획득을 시도하지만, 실패해도 대기(블로킹)하지 않고
 *          즉시 false를 반환한다다. 성공 시 현재 스레드가 락의 소유자가 된다.
 *
 * @note lock_acquire()와의 차이:
 *       - lock_acquire(): 락을 얻을 때까지 블로킹 (대기)
 *       - lock_try_acquire(): 즉시 성공/실패 반환, 블로킹하지 않음
 *
 * @note 동작 원리:
 *       1. sema_try_down()으로 세마포어 획득 시도 (non-blocking)
 *       2. 성공하면 lock->holder를 현재 스레드로 설정
 *       3. 실패하면 아무 작업도 하지 않고 false 반환
 *
 * @note 사용 시나리오:
 *       - 락 획득 실패 시 대체 작업을 수행하고 싶을 때
 *       - 교착 상태(deadlock) 회피를 위해 비블로킹 시도가 필요할 때
 *       - 인터럽트 핸들러에서 락 획득이 필요할 때 (블로킹 불가)
 *
 * @note 인터럽트 핸들러 안전:
 *       - 이 함수는 블로킹하지 않으므로 인터럽트 핸들러에서 호출 가능
 *       - lock_acquire()는 블로킹하므로 인터럽트 핸들러에서 사용 불가
 *
 * @warning 다음 조건들이 만족되지 않으면 ASSERT 실패:
 *          - lock != NULL
 *          - 현재 스레드가 이미 이 lock을 보유하고 있지 않음
 *            (재진입 불가: 같은 스레드가 같은 lock을 두 번 획득할 수 없음)
 *
 * @warning	Priority Donation 미지원
 * 					- TODO: 우선순위 기부(priority donation) 로직이 적용되지 않았음!
 *
 * @see lock_acquire()
 * @see lock_release()
 * @see sema_try_down()
 */
bool lock_try_acquire(struct lock *lock)
{
	bool success;

	ASSERT(lock != NULL);
	ASSERT(!lock_held_by_current_thread(lock)); // 재진입 방지

	// 세마포어를 non-blocking 방식으로 획득 시도
	// value > 0이면 성공, value == 0이면 실패
	success = sema_try_down(&lock->semaphore);

	// 성공 시 현재 스레드를 락의 소유자로 설정
	if (success)
		lock->holder = thread_current();
	return success;
}

/**
 * @brief 현재 스레드가 소유한 락을 해제하는 함수
 *
 * @param lock 해제할 락의 포인터
 *
 * @details 이 함수는 다음 순서로 락을 해제합니다:
 *          1. 이 lock을 기다리던 스레드들의 우선순위 기부를 제거
 *          2. 남은 기부 중 최고 우선순위로 현재 스레드의 우선순위 재계산
 *          3. lock의 소유자를 NULL로 설정하고 세마포어를 up
 *
 * @note Priority Donation 해제 메커니즘:
 *       - lock을 해제하면 이 lock을 기다리며 우선순위를 기부했던 스레드들의
 *         기부가 더 이상 유효하지 않으므로 제거해야 합니다.
 *       - remove_donations()로 해당 lock 관련 기부만 선택적으로 제거
 *       - recalculate_priority()로 남은 기부들 중 최댓값으로 우선순위 갱신
 *
 * @note 해제 후 동작:
 *       - sema_up()으로 이 lock을 기다리던 스레드 중 하나가 깨어남
 *       - 깨어난 스레드는 lock_acquire()에서 lock의 새 소유자가 됨
 *       - 스케줄러가 우선순위에 따라 다음 실행 스레드를 결정
 *
 * @warning 다음 조건들이 만족되지 않으면 ASSERT 실패:
 *          - lock != NULL
 *          - 현재 스레드가 lock을 보유하고 있음 (lock_held_by_current_thread)
 *
 * @warning 인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러에서
 *          락을 해제하는 것도 의미가 없다.
 *
 * @see lock_acquire()
 * @see remove_donations()
 * @see recalculate_priority()
 */
void lock_release(struct lock *lock)
{
	ASSERT(lock != NULL);
	ASSERT(lock_held_by_current_thread(lock));

	// 1. 이 lock을 기다리던 스레드들의 우선순위 기부 제거
	remove_donations(lock);

	// 2. 남은 기부들 중 최고 우선순위로 현재 스레드의 우선순위 재계산
	recalculate_priority();

	// 3. lock의 소유자를 제거하고 세마포어 up (대기 스레드 중 하나 깨움)
	lock->holder = NULL;
	sema_up(&lock->semaphore);
}

/**
 * @brief 특정 락과 관련된 우선순위 기부를 제거하는 함수
 *
 * @param lock 기부를 제거할 락의 포인터
 *
 * @details 현재 스레드의 donators 리스트를 순회하며, 해제하려는 lock을
 *          기다리고 있던(waiting_lock == lock) 스레드들의 기부만 제거한다.
 *          같은 스레드가 여러 lock을 보유할 수 있으므로, 해제하는 lock과
 *          관련 없는 기부는 유지해야 한다.
 *
 * @note 선택적 기부 제거의 필요성:
 *       - 한 스레드가 여러 개의 lock을 동시에 보유 가능
 *       - 각 lock마다 다른 스레드들이 대기하며 우선순위를 기부
 *       - lock A를 해제할 때 lock B 관련 기부는 유지되어야 함
 *       - waiting_lock 필드로 어떤 lock 때문에 기부했는지 추적
 *
 * @note 순회 중 삭제 처리:
 *       - list_remove(e)는 삭제 후 다음 elem을 반환
 *       - 삭제하지 않을 때만 list_next(e) 호출
 *       - 이를 통해 안전하게 순회 중 삭제 가능
 *
 * @see lock_release()
 * @see lock_acquire()
 *
 * @since Week08, 2025-11-10, Project 1 - Priority Donation
 */
void remove_donations(struct lock *lock)
{
	struct thread *curr = thread_current();
	struct list_elem *e;

	// donators 리스트 순회 (이 스레드에게 우선순위를 기부한 스레드들)
	e = list_begin(&curr->donators);
	while (e != list_end(&curr->donators))
	{
		struct thread *donor = list_entry(e, struct thread, donation_elem);

		// 이 donor가 현재 해제하는 lock을 기다리고 있었다면 제거
		// (donators에 있다고 해서 모두 같은 lock을 기다리는 것은 아님)
		if (donor->waiting_lock == lock)
		{
			e = list_remove(e); // 리스트에서 제거하고 다음 elem 반환
		}
		else
		{
			e = list_next(e); // 다음 elem으로 이동
		}
	}
}

/**
 * @brief 우선순위 기부 상황을 반영하여 현재 스레드의 실제 우선순위를 재계산하는 함수
 *
 * @details 이 함수는 현재 스레드의 우선순위를 original_priority(본래 우선순위)로
 *          초기화한 후, 다른 스레드들로부터 기부받은 우선순위들을 확인하여
 *          그 중 가장 높은 값과 비교한다. 기부받은 우선순위가 더 높다면
 *          해당 값을 현재 스레드의 실행 우선순위로 설정한다.
 *
 * @note Priority Donation 메커니즘:
 *       - donators 리스트: 현재 스레드에게 우선순위를 기부한 스레드들의 목록
 *       - 이 리스트는 우선순위 내림차순으로 정렬되어 있어야 함 (front = 최고 우선순위)
 *       - 기부가 해제되거나 새로운 기부가 발생할 때마다 이 함수를 호출해야 함
 *
 * @warning 이 함수는 현재 스레드(thread_current())에만 적용된다.
 *          donators 리스트가 우선순위 순으로 정렬되어 있지 않으면
 *          올바른 우선순위를 계산할 수 없다.
 *
 * @see thread_set_priority()
 * @see lock_release()
 * @see lock_acquire()
 */
void recalculate_priority(void)
{
	struct thread *curr = thread_current();

	// 우선순위 재계산
	int old_priority = curr->priority;

	// 1단계: 스레드의 우선순위를 기본(original) 우선순위로 초기화
	// (기부받은 우선순위를 모두 제거하고 원래 값으로 복원)
	curr->priority = curr->original_priority;

	// 2단계: 기부자(donators) 리스트 확인
	// 다른 스레드들이 이 스레드에게 우선순위를 기부했는지 체크
	if (!list_empty(&curr->donators))
	{
		// donators 리스트의 맨 앞 요소 가져오기
		// (리스트는 우선순위 내림차순 정렬되어 있으므로 front = 최고 우선순위)
		struct thread *top_donator = list_entry(list_front(&curr->donators), struct thread, donation_elem);

		// 3단계: 기부받은 우선순위와 원래 우선순위 비교
		// 기부받은 우선순위가 더 높으면 그 값을 사용
		if (top_donator->priority > curr->priority)
		{
			curr->priority = top_donator->priority;
		}
	}
}

/**
 * @brief 현재 스레드가 특정 락을 보유하고 있는지 확인하는 함수
 *
 * @param lock 확인할 락의 포인터
 *
 * @note 주의사항:
 *       - 이 함수는 현재 스레드에 대해서만 정확한 결과를 보장한다.
 *       - 다른 스레드가 락을 보유하고 있는지 테스트하는 것은 race condition을
 *         발생시킬 수 있으므로 사용하지 않아야 한다.
 *       - 다른 스레드의 락 소유 상태는 테스트 시점과 결과 사용 시점 사이에
 *         변경될 수 있어 결과를 신뢰할 수 없다.
 *
 * @see lock_acquire()
 * @see lock_release()
 * @see lock_try_acquire()
 */
bool lock_held_by_current_thread(const struct lock *lock)
{
	ASSERT(lock != NULL);

	return lock->holder == thread_current();
}

/**
 * @brief Condition variable을 초기화하는 함수
 *
 * @param cond 초기화할 condition variable의 포인터
 *
 * @details 이 함수는 condition variable의 waiters 리스트를 초기화하여
 *          대기 중인 스레드들을 관리할 준비를 한다. Condition variable은
 *          한 코드 영역이 특정 조건을 신호(signal)하면, 협력하는 다른 코드가
 *          그 신호를 받아 적절한 동작을 수행할 수 있게 하는 동기화 메커니즘.
 *
 * @note Condition Variable 동작 원리:
 *       - 신호 송신: cond_signal() 또는 cond_broadcast()로 대기 스레드를 깨움
 *       - 신호 수신: cond_wait()로 조건이 만족될 때까지 대기
 *       - waiters 리스트: 대기 중인 semaphore_elem들을 관리하는 큐
 *
 * @note 사용 시나리오:
 *       - Producer-Consumer 패턴: 생산자가 데이터를 넣으면 소비자에게 신호
 *       - 리소스 대기: 특정 리소스가 사용 가능할 때까지 대기
 *       - 이벤트 알림: 특정 이벤트 발생 시 대기 중인 스레드들에게 통지
 *
 * @see cond_wait()
 * @see cond_signal()
 * @see cond_broadcast()
 */
void cond_init(struct condition *cond)
{
	ASSERT(cond != NULL);

	// 대기 중인 스레드들(semaphore_elem)을 관리할 리스트 초기화
	// 초기 상태에는 대기자가 없으므로 빈 리스트로 시작
	list_init(&cond->waiters);
}

/**
 * @brief Condition variable에서 조건 신호를 대기하는 함수
 *
 * @param cond 대기할 condition variable의 포인터
 * @param lock 현재 스레드가 보유한 락의 포인터
 *
 * @details 이 함수는 다음 순서로 동작합니다:
 *          1. 보유한 LOCK을 해제
 *          2. COND 신호가 올 때까지 대기 (블록)
 *          3. 신호를 받으면 깨어남
 *          4. LOCK을 다시 획득한 후 반환
 *
 * @note Mesa-style Semantics:
 *       - 신호(signal)와 대기(wait)가 원자적(atomic)이지 않다
 *       - 깨어난 후 다른 스레드가 먼저 조건을 변경할 수 있다.
 *       - 따라서 깨어난 뒤 반드시 while 루프로 조건을 다시 확인해야 한다
 *       - 권장 패턴: while (condition_not_met) { cond_wait(...); }
 *
 * @note Lock과 Condition Variable 관계:
 *       - 하나의 condition variable은 하나의 LOCK과만 연결된다.
 *       - 하나의 LOCK은 여러 개의 condition variable과 연결 가능.
 *       - LOCK은 공유 데이터를 보호하고, condition variable은 대기/신호 메커니즘을 제공.
 *
 * @note Priority Scheduling 구현:
 *       - list_push_back() 대신 list_insert_ordered() 사용
 *       - 우선순위가 높은 스레드가 먼저 깨어나도록 정렬된 순서로 삽입
 *       - compare_sema_priority 함수로 세마포어의 우선순위 비교
 *
 * @warning 이 함수는 스레드를 블록시키므로 인터럽트 핸들러에서 호출 금지!
 *
 * @see cond_signal()
 * @see cond_broadcast()
 * @see lock_acquire()
 * @see lock_release()
 */
void cond_wait(struct condition *cond, struct lock *lock)
{
	struct semaphore_elem waiter;

	// 기본 전제 조건 검증
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());									 // 인터럽트 컨텍스트 아님
	ASSERT(lock_held_by_current_thread(lock)); // 현재 스레드가 lock 보유

	// 이 대기자 전용 세마포어 초기화 (value=0으로 블록 상태)
	sema_init(&waiter.semaphore, 0);

	// waiters 리스트에 우선순위 순서로 삽입 (높은 우선순위가 앞에)
	list_insert_ordered(&cond->waiters, &waiter.elem, compare_sema_priority, NULL);

	// 1단계: 락 해제 (다른 스레드가 공유 데이터에 접근 가능)
	lock_release(lock);

	// 2단계: 세마포어에서 대기 (cond_signal()이 sema_up()할 때까지 블록)
	sema_down(&waiter.semaphore);

	// 3단계: 깨어난 후 락 재획득 (공유 데이터 보호)
	lock_acquire(lock);
}

/**
 * @brief Condition variable에서 대기 중인 스레드 하나를 깨우는 함수
 *
 * @param cond 신호를 보낼 condition variable의 포인터
 * @param lock 현재 스레드가 보유한 락의 포인터 (UNUSED 표시)
 *
 * @details 이 함수는 COND에서 대기 중인 스레드가 있다면, 그 중 하나를 선택하여
 *          깨운다. 대기 중인 스레드가 없으면 아무 동작도 하지 않는다.
 *          LOCK은 이 함수를 호출하기 전에 반드시 획득되어 있어야 한다.
 *
 * @note 동작 순서:
 *       1. waiters 리스트가 비어있는지 확인
 *       2. 비어있지 않으면 우선순위 순으로 정렬
 *       3. 맨 앞(최고 우선순위) 스레드의 semaphore_elem을 꺼냄
 *       4. 해당 스레드의 세마포어에 sema_up() 호출
 *       5. 그 스레드는 cond_wait()의 sema_down()에서 깨어남
 *
 * @note Priority Scheduling 구현:
 *       - 신호를 보내기 전에 list_sort()로 waiters를 우선순위 순으로 정렬
 *       - compare_sema_priority 함수로 각 대기자의 우선순위 비교
 *       - 가장 높은 우선순위를 가진 스레드가 먼저 깨어나도록 보장
 *       - 동적 우선순위 변경에도 대응 가능
 *
 * @note Mesa-style 의미:
 *       - 신호를 보내도 즉시 제어가 넘어가지 않습니다 (Hoare-style과 다름)
 *       - 신호를 받은 스레드는 ready_list에 추가되어 스케줄러에 의해 실행됨
 *       - 깨어난 스레드가 실행되기 전에 조건이 변경될 수 있음
 *
 * @warning 인터럽트 핸들러는 락을 획득할 수 없으므로, 인터럽트 핸들러 내에서
 *          condition variable에 신호를 보내는 것은 의미가 없다.
 *
 * @see cond_wait()
 * @see cond_broadcast()
 * @see sema_up()
 *
 */
void cond_signal(struct condition *cond, struct lock *lock UNUSED)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);
	ASSERT(!intr_context());
	ASSERT(lock_held_by_current_thread(lock));

	// 대기 중인 스레드가 있는지 확인
	if (!list_empty(&cond->waiters))
	{
		// waiters 리스트를 우선순위 순으로 정렬 (높은 우선순위가 앞에)
		// 동적 우선순위 변경 상황에 대응하기 위해 신호 직전 정렬
		list_sort(&cond->waiters, compare_sema_priority, NULL);

		// 맨 앞(최고 우선순위) semaphore_elem을 꺼내서
		// 해당 스레드의 세마포어에 sema_up() 호출 → 스레드 깨움
		sema_up(&list_entry(list_pop_front(&cond->waiters), struct semaphore_elem, elem)->semaphore);
	}
}

/**
 * @brief Condition variable에서 대기 중인 모든 스레드를 깨우는 함수
 *
 * @param cond 신호를 보낼 condition variable의 포인터
 * @param lock 현재 스레드가 보유한 락의 포인터
 *
 * @details 이 함수는 COND에서 대기 중인 모든 스레드를 깨운다
 *          내부적으로 waiters 리스트가 빌 때까지 cond_signal()을 반복 호출한다.
 *          LOCK은 이 함수를 호출하기 전에 반드시 획득되어 있어야 한다.
 *
 * @note cond_signal()과의 차이:
 *       - cond_signal(): 대기 중인 스레드 하나만 깨움
 *       - cond_broadcast(): 대기 중인 모든 스레드를 깨움
 *
 * @note 동작 순서:
 *       1. waiters 리스트가 빈 상태가 될 때까지 반복
 *       2. 매 반복마다 cond_signal() 호출
 *       3. cond_signal()은 우선순위가 가장 높은 스레드를 하나씩 깨움
 *       4. 깨어난 스레드들은 lock을 재획득하기 위해 lock->semaphore.waiters에서 대기
 *
 * @note 사용 시나리오:
 *       - 리소스가 대량으로 사용 가능해질 때
 *       - 공유 상태가 크게 변경되어 모든 대기자가 재평가해야 할 때
 *       - 프로그램 종료 시 모든 대기 스레드를 깨워야 할 때
 *
 * @note Mesa-style 의미:
 *       - 깨어난 모든 스레드가 즉시 실행되지 않음
 *       - 스케줄러에 의해 우선순위에 따라 순차적으로 실행됨
 *       - 각 스레드는 깨어난 후 조건을 다시 확인해야 함 (while 루프 필수)
 *
 * @warning 다음 조건들이 만족되지 않으면 ASSERT 실패:
 *          - cond != NULL
 *          - lock != NULL
 *          - 인터럽트 컨텍스트가 아님
 *          - 현재 스레드가 lock을 보유하고 있음
 *
 * @see cond_signal()
 * @see cond_wait()
 */
void cond_broadcast(struct condition *cond, struct lock *lock)
{
	ASSERT(cond != NULL);
	ASSERT(lock != NULL);

	// waiters 리스트가 빌 때까지 반복
	// 매 반복마다 우선순위가 가장 높은 대기 스레드를 하나씩 깨움
	while (!list_empty(&cond->waiters))
		cond_signal(cond, lock);
}
