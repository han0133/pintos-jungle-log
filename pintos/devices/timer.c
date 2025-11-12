#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>  /* int64_t, int32_t 등의 표준 정수 타입 정의 (C99 표준)
                          - int64_t: 64비트 부호 있는 정수형, 플랫폼 독립적으로 정확히 64비트 보장
                          - 범위: -9,223,372,036,854,775,808 ~ 9,223,372,036,854,775,807
                          - 직접 정의한 타입이 아니라 표준 C 라이브러리에서 제공 */
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* ====================================================================
   이 파일에서 사용되는 기본 함수/매크로 설명
   ====================================================================
   
   [최적화 배리어]
   barrier()
   - 정의: #define barrier() asm volatile ("" : : : "memory") (synch.h)
   - 기능: 컴파일러 최적화를 방지하는 배리어
   - 매개변수: 없음
   - 반환값: 없음
   - 사용 이유: 메모리 접근 순서를 보장하기 위해 사용
              컴파일러가 barrier() 앞뒤의 메모리 접근을 재정렬하지 않도록 함
   - 예시: ticks 변수를 읽은 후 barrier()를 호출하면, 
           그 이후의 코드가 ticks 값을 사용하기 전에 최신 값임을 보장
   
   [디버그 어설션]
   ASSERT(CONDITION)
   - 정의: #define ASSERT(CONDITION) ... (debug.h)
   - 기능: 조건이 거짓이면 시스템을 중단하고 오류 메시지 출력
   - 매개변수: CONDITION - 검사할 조건식 (예: intr_get_level() == INTR_ON)
   - 반환값: 없음 (조건이 참이면 아무것도 하지 않음, 거짓이면 PANIC 호출)
   - 사용 이유: 프로그래밍 오류를 조기에 발견하기 위해 사용
              디버그 모드(NDEBUG가 정의되지 않음)에서만 동작
              릴리스 모드에서는 ((void) 0)으로 치환되어 제거됨
   - 예시: ASSERT(intr_get_level() == INTR_ON) 
           → 인터럽트가 켜져 있어야 하는데 꺼져 있으면 오류
   
   [인터럽트 제어 함수들]
   enum intr_level intr_get_level(void)
   - 기능: 현재 인터럽트 상태를 반환
   - 매개변수: 없음
   - 반환값: enum intr_level (INTR_ON 또는 INTR_OFF)
   - 구현: CPU의 flags 레지스터에서 인터럽트 플래그(IF)를 읽어서 확인
   - 사용 이유: 현재 인터럽트가 활성화되어 있는지 확인할 때 사용
   
   enum intr_level intr_disable(void)
   - 기능: 인터럽트를 비활성화하고 이전 상태를 반환
   - 매개변수: 없음
   - 반환값: enum intr_level (비활성화하기 전의 상태)
   - 구현: CPU의 CLI 명령어를 실행하여 인터럽트 플래그를 클리어
   - 사용 이유: 원자적 연산을 보장하기 위해 인터럽트를 일시적으로 비활성화
              반환값을 저장해두었다가 나중에 intr_set_level()로 복원
   
   enum intr_level intr_set_level(enum intr_level level)
   - 기능: 인터럽트 상태를 설정하고 이전 상태를 반환
   - 매개변수: level - 설정할 인터럽트 상태 (INTR_ON 또는 INTR_OFF)
   - 반환값: enum intr_level (설정하기 전의 상태)
   - 구현: level에 따라 intr_enable() 또는 intr_disable() 호출
   - 사용 이유: 인터럽트 상태를 복원할 때 사용
              intr_disable()로 저장한 이전 상태를 복원하는 데 유용
   
   enum intr_level intr_enable(void)
   - 기능: 인터럽트를 활성화하고 이전 상태를 반환
   - 매개변수: 없음
   - 반환값: enum intr_level (활성화하기 전의 상태)
   - 구현: CPU의 STI 명령어를 실행하여 인터럽트 플래그를 설정
   - 사용 이유: 인터럽트를 다시 활성화할 때 사용
   
   [스레드 관련 함수들]
   void thread_yield(void)
   - 기능: 현재 실행 중인 스레드가 CPU를 양보하고 ready queue에 추가
   - 매개변수: 없음
   - 반환값: 없음
   - 사용 이유: 스레드가 다른 스레드에게 CPU를 양보하고 싶을 때 사용
              timer_sleep()에서 busy-wait 루프 중에 CPU를 양보하여
              다른 스레드가 실행될 수 있도록 함
   
   void thread_tick(void)
   - 기능: 타이머 틱마다 호출되어 스레드 스케줄링 관련 작업 수행
   - 매개변수: 없음
   - 반환값: 없음
   - 사용 이유: 시간 할당량(TIME_SLICE)을 확인하고, 
              시간이 지나면 스레드 전환을 요청
              timer_interrupt()에서 호출됨
   
   struct thread *thread_current(void)
   - 기능: 현재 실행 중인 스레드의 구조체 포인터를 반환
   - 매개변수: 없음
   - 반환값: struct thread * (현재 스레드의 포인터)
   - 사용 이유: 현재 스레드의 정보에 접근하거나 상태를 변경할 때 사용
   ==================================================================== */

/* 8254 타이머 칩의 하드웨어 상세 정보는 [8254]를 참조하세요. */

/* 전처리 오류검사 매크로 
전처리 단계에서 컴파일러에게 전달되어 컴파일 시간에 오류를 발생시키는 매크로
이 매크로는 컴파일 시간에 오류를 발생시키므로 런타임에 영향을 주지 않음 */
#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* OS 부팅 이후의 타이머 틱 수. */
/* int64_t 사용 이유: 타이머 틱은 계속 증가하므로 오버플로우를 방지하기 위해 큰 타입 필요 */
static int64_t ticks;

/* 타이머 틱당 루프 수.
   timer_calibrate()에 의해 초기화됩니다.
   사용 이유: 짧은 시간 대기(busy_wait)를 구현하기 위해 CPU 속도를 측정한 값 */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);
static struct list sleep_list; // 블락 된 스레드 관리 리스트

/* 8254 프로그래머블 간격 타이머(PIT)를 초당 PIT_FREQ번
   인터럽트하도록 설정하고, 해당 인터럽트를 등록합니다.
   
   사용 이유:
   - OS가 시간을 추적하고 스케줄링을 하기 위해 하드웨어 타이머를 초기화해야 함
   - 시스템 부팅 시 한 번만 호출되어 타이머 하드웨어를 설정 */
void timer_init (void) {
	/* 8254 입력 주파수를 TIMER_FREQ로 나눈 값,
	   가장 가까운 값으로 반올림. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;
   list_init (&sleep_list); // 블락 된 스레드 관리 리스트 초기화

	outb (0x43, 0x34);    /* 제어 워드: 카운터 0, LSB 다음 MSB, 모드 2, 바이너리. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* 짧은 지연을 구현하는 데 사용되는 loops_per_tick을 보정합니다.
   
   사용 이유:
   - CPU마다 속도가 다르므로, 정확한 짧은 시간 대기를 위해 CPU 속도를 측정해야 함
   - busy_wait() 함수가 정확한 시간만큼 대기할 수 있도록 루프 횟수를 계산
   - 시스템 부팅 시 한 번만 실행되어 CPU 속도를 측정 
   
	timer_init()
	↓
	초당 100번 인터럽트 발생하도록 하드웨어 설정
	↓
	매 10ms마다 timer_interrupt() 호출
	↓
	ticks++ (1틱 증가)
	↓
	timer_calibrate()
	↓
	한 틱(10ms) 동안 몇 번의 루프를 실행할 수 있는지 측정
	↓
	loops_per_tick 값 계산 (예: 14336)
 
   */
void timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* loops_per_tick을 하나의 타이머 틱보다 작은
	   가장 큰 2의 거듭제곱으로 근사합니다. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* loops_per_tick의 다음 8비트를 정밀화합니다. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* OS 부팅 이후의 타이머 틱 수를 반환합니다.
   
   사용 이유:
   - 현재 시간을 알아야 스레드 스케줄링, 타이머 대기 등을 할 수 있음
   - 다른 함수들이 시간을 측정하거나 비교할 때 사용
   - 인터럽트로 인한 경쟁 조건을 방지하기 위해 인터럽트를 비활성화하고 읽음 */
int64_t timer_ticks (void) {
	enum intr_level old_level = intr_disable (); // 인터럽트를 비활성화하고 이전 상태를 저장
	int64_t t = ticks; // ticks 값을 가져옮
	intr_set_level (old_level); // 인터럽트 상태를 복원
	barrier (); // 메모리 접근 순서를 보장
	/*
		barrier()가 필요한 이유
		// 컴파일러가 이렇게 최적화할 수 있음
		int64_t timer_ticks_optimized (void) {
		intr_disable ();
		intr_set_level (...);  // 순서가 바뀔 수 있음!
		int64_t t = ticks;     // 읽기 전에 복원?
		return t;
		}
	*/
	return t;
}

/* THEN 이후 경과된 타이머 틱 수를 반환합니다.
   THEN은 timer_ticks()가 반환한 값이어야 합니다.
   
   사용 이유:
   - 특정 시점으로부터 얼마나 시간이 지났는지 계산할 때 편리함
   - timer_sleep() 등에서 경과 시간을 확인하는 데 사용 */
int64_t timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}



static bool compare_tick(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
	struct thread *ta = list_entry(a, struct thread, elem);
	struct thread *tb = list_entry(b, struct thread, elem);
	return ta->wakeup_tick < tb->wakeup_tick;
}




/* 약 TICKS개의 타이머 틱 동안 실행을 일시 중단합니다.
   
   사용 이유:
   - 스레드가 특정 시간 동안 대기해야 할 때 사용
   - 현재는 busy-wait 방식으로 구현되어 있으나, 나중에 block 방식으로 개선 예정
   - 틱 단위는 OS 내부 시간 단위 (예: TIMER_FREQ=100이면 1틱 = 10ms) 

   
   사용 이유:
   - 스레드가 특정 시간 동안 대기해야 할 때 사용
   - 현재는 busy-wait 방식으로 구현되어 있으나, 나중에 block 방식으로 개선 예정
   - 틱 단위는 OS 내부 시간 단위 (예: TIMER_FREQ=100이면 1틱 = 10ms) 

   블록으로 구현
   몇 틱 동안의 슬립 요청이 들어옮
   현재 틱을 가져옮
   현재 틱에 기다려야하는 틱을 더함 = wakeup_tick
   기다려야하는 스레드를 멈추고
   현재 리스트에서 제거
   블록 리스트에 현재 스레드를 넣고 wakeup_tick 이되면 스레드 멈춤을 풀고
   다시 작동중 리스트에 넣어줌
*/
void timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks (); // 현재 틱을 가져옮
	ASSERT (intr_get_level () == INTR_ON); 
	struct thread *t = thread_current (); // 현재 스레드를 가져옮
	t->wakeup_tick = start + ticks; // 기다려야하는 틱을 더해서 재시작해야되는 시간을 계산
	// 현재 스레드를 블록 리스트에 넣어줌 wakeup_tick 기준으로 정렬
	enum intr_level old_level = intr_disable();
	list_insert_ordered(&sleep_list, &t->elem, compare_tick, NULL); 
	thread_block(); // 현재 스레드를 블록 시킴
	intr_set_level(old_level);
}

/* 약 MS 밀리초 동안 실행을 일시 중단합니다.
   
   사용 이유:
   - 밀리초(1/1000초) 단위로 대기하는 것이 더 직관적이고 편리함
   - 예: timer_msleep(100) = 100밀리초 = 0.1초 대기
   - 내부적으로 real_time_sleep()을 호출하여 틱으로 변환
   
   왜 별도 함수인가?
   - 사용자가 단위 변환을 직접 할 필요 없이 자연스럽게 밀리초 단위로 사용 가능
   - 코드 가독성 향상: timer_msleep(100)이 timer_sleep(10)보다 의미가 명확 */
void timer_msleep (int64_t ms) {
	/* 약 MS 밀리초를 타이머 틱으로 변환하여 대기 */
	real_time_sleep (ms, 1000);
}

/* 약 US 마이크로초 동안 실행을 일시 중단합니다.
   
   사용 이유:
   - 마이크로초(1/1,000,000초) 단위로 매우 짧은 시간을 정밀하게 대기할 때 사용
   - 예: timer_usleep(1000) = 1000마이크로초 = 1밀리초 대기
   - 하드웨어 제어나 정밀한 타이밍이 필요한 경우에 사용
   
   왜 별도 함수인가?
   - 밀리초보다 더 짧은 시간을 다룰 때 사용
   - 단위가 다르므로 별도 함수로 제공하는 것이 명확함 */
void timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* 약 NS 나노초 동안 실행을 일시 중단합니다.
   
   사용 이유:
   - 나노초(1/1,000,000,000초) 단위로 극도로 짧은 시간을 대기할 때 사용
   - 예: timer_nsleep(1000000) = 1000000나노초 = 1밀리초 대기
   - 실제로는 타이머 해상도(TIMER_FREQ=100이면 10ms)보다 짧으면 busy_wait 사용
   
   왜 별도 함수인가?
   - 마이크로초보다도 더 짧은 시간 단위
   - 각 시간 단위별로 함수를 제공하여 사용자가 적절한 함수를 선택할 수 있음
   
   참고: 실제 OS에서는 나노초 단위 정확도는 하드웨어 제약으로 달성하기 어려움 */
void timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* 타이머 통계를 출력합니다.
   
   사용 이유:
   - 디버깅이나 시스템 모니터링 시 현재까지 경과한 시간을 확인
   - 테스트나 성능 분석에 유용 */
void timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* 타이머 인터럽트 핸들러.
   
   사용 이유:
   - 하드웨어 타이머가 주기적으로 인터럽트를 발생시킬 때마다 호출됨
   - ticks를 증가시켜 시간을 추적하고, thread_tick()을 호출하여 스케줄링 수행
   - 매 TIMER_FREQ번(초당 100번) 호출되어 OS의 시간 개념을 제공 */
static void timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();

	if (list_empty(&sleep_list))
		return;
	while (!list_empty(&sleep_list)) {
		struct list_elem *e = list_front(&sleep_list);
		struct thread *t = list_entry(e, struct thread, elem);
		if (t->wakeup_tick <= ticks) {
			list_pop_front(&sleep_list);
			thread_unblock(t); // 해당 스레드를 블록 해제
		} else {
			break;
		}
	}
}

// static void timer_interrupt (struct intr_frame *args UNUSED) {
// 	ticks++;
// 	thread_tick ();
	
// 	// thread_awake() 호출로 대체
// 	thread_awake(ticks);
// }


/* LOOPS번의 반복이 하나 이상의 타이머 틱을 기다리면
   true를 반환하고, 그렇지 않으면 false를 반환합니다.
   
   사용 이유:
   - timer_calibrate()에서 CPU 속도를 측정할 때 사용
   - 특정 루프 횟수가 한 틱보다 짧은지 긴지 판단하여 loops_per_tick을 계산 */
static bool too_many_loops (unsigned loops) {
	/* 타이머 틱을 기다립니다. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* LOOPS번의 루프를 실행합니다. */
	start = ticks;
	busy_wait (loops);

	/* 틱 카운트가 변경되었다면, 너무 오래 반복한 것입니다. */
	barrier ();
	return start != ticks;
}

/* 짧은 지연을 구현하기 위해 간단한 루프를 LOOPS번 반복합니다.
   코드 정렬이 타이밍에 큰 영향을 미칠 수 있으므로 NO_INLINE으로
   표시되었습니다. 이 함수가 다른 위치에서 다르게 인라인되면
   결과를 예측하기 어렵기 때문입니다. 
   
   인라인화란 최적화 기법으로
   // 원본 코드
	int add(int a, int b) {
		return a + b;
	}

	void main() {
		int result = add(5, 3); // (A) 함수 호출
	}

	// 인라인화된 후 (컴파일러 내부적으로)
	void main() {
		// int result = add(5, 3); 이 부분이
		int result = 5 + 3; // 함수 본체 코드로 대체됨
	}

	저렇게 함수 호출을 코드 본체로 대체하는 것을 인라인화라고 함
	불필요한 오버헤드를 줄일수 있고
	인라인화 해서 또 추가로 최적화가 가능해 지지만
	크기가 큰 코드를 인라인화 하면 전체 크기가 증가할수 있음
	그리고 인라인화 되버리면 디버깅이 어려워짐 
   
   사용 이유:
   - 타이머 틱보다 짧은 시간(서브틱)을 정확하게 대기할 때 사용
   - CPU를 계속 사용하는 busy-wait 방식이지만, 매우 짧은 시간이므로 허용됨
   - real_time_sleep()에서 틱 단위로 변환할 수 없는 짧은 시간에 사용 */
static void NO_INLINE busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* 약 NUM/DENOM초 동안 대기합니다.
   
   사용 이유:
   - timer_msleep(), timer_usleep(), timer_nsleep()의 공통 구현체
   - 분수 형태로 시간을 표현하여 다양한 단위를 하나의 함수로 처리
   - 예: timer_msleep(100) → real_time_sleep(100, 1000) = 100/1000초 = 0.1초
   
   동작 방식:
   - ticks > 0: timer_sleep() 사용 (스레드가 block되어 CPU를 양보)
   - ticks <= 0: busy_wait() 사용 (매우 짧은 시간이므로 busy-wait가 더 정확) */
static void real_time_sleep (int64_t num, int32_t denom) {
	/* NUM/DENOM초를 타이머 틱으로 변환합니다. 내림 처리합니다.

	   (NUM / DENOM) 초
	   ---------------------- = NUM * TIMER_FREQ / DENOM 틱.
	   1 초 / TIMER_FREQ 틱
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* 최소 하나의 전체 타이머 틱을 기다립니다.
		   CPU를 다른 프로세스에 양보하므로 timer_sleep()을 사용합니다. */
		timer_sleep (ticks);
	} else {
		/* 그렇지 않으면, 더 정확한 서브틱 타이밍을 위해
		   busy-wait 루프를 사용합니다. 오버플로우 가능성을 피하기 위해
		   분자와 분모를 1000으로 나눕니다. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
