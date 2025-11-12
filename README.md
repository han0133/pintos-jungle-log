# Pintos: 우선순위 스케줄링 구현

Pintos 교육용 운영체제에서 우선순위 기반 스레드 스케줄링을 시스템 수준으로 구현한 프로젝트입니다.

## 개요

이 프로젝트에서는 우선순위 역전(priority inversion)을 방지하기 위한 우선순위 기부(priority donation) 메커니즘과 세마포어/락 관리를 통해, 교착 상태(deadlock)나 기아(starvation) 없이 복잡한 동시성 작업을 구현했습니다

## 주요 기능

### 우선순위 스케줄링
- **우선순위 기반 준비 큐**: 준비 상태 스레드를 우선순위 순서대로 유지하여 높은 우선순위 스레드부터 실행되도록 보장
- **동적 우선순위 선점**: 높은 우선순위 스레드가 준비 상태가 되면 현재 실행 중인 낮은 우선순위 스레드를 즉시 선점
- **효율적 우선순위 삽입**: 정렬된 리스트 삽입(`list_insert_ordered`)을 사용하여 O(n) 삽입 시간 유지하면서 매 깨어남마다 O(n log n) 전체 정렬 회피

### 우선순위 기부 (우선순위 역전 방지)
- **중첩 기부 지원**: 최대 8단계 깊이의 락 체인을 처리하여 우선순위를 보유-대기 관계로 전파
- **선택적 기부 제거**: 락 해제 시 해당 락과 관련된 기부만 제거하며, 다른 락으로부터의 기부는 유지
- **원래 우선순위 보존**: 스레드가 `priority`(donate 후 우선순위)와 `original_priority`(기본 우선순위)를 모두 유지하여 우선순위 복원 가능



## 아키텍처

### 스레드 구조
```c
struct thread {
    int priority;                    // 현재 효과적 우선순위
    int original_priority;           // 기부 이전의 기본 우선순위
    struct list donators;            // 이 스레드에게 기부한 스레드들
    struct lock *waiting_lock;       // 이 스레드가 대기 중인 락
    struct list_elem donation_elem;  // donators 리스트용 요소
    // ... 다른 필드들
};
```

### 락과 세마포어 설계
```
락 (이진 자원)
├── holder: 현재 소유 스레드
└── semaphore: 접근 제어 (값: 0 또는 1)
    ├── value: 사용 가능한 개수
    └── waiters: 우선순위 순서의 대기 스레드 리스트

조건 변수
└── waiters: semaphore_elem 구조체 리스트
    └── 각 요소는 스레드 조정용 개별 세마포어 포함
```

## 구현 하이라이트

### 우선순위 순서 삽입
준비 큐와 세마포어 대기자에 우선순위 비교를 통해 삽입:
```c
list_insert_ordered(&ready_list, &thread->elem, 
                    compare_ready_priority, NULL);
```

이를 통해 보장되는 것:
- 준비 스레드는 항상 우선순위 순서로 정렬됨 (높은 순서가 먼저)
- 락 대기자와 조건 변수 대기자는 신호 전에 정렬됨
- 선점 확인은 `list_front()`로 O(1) 시간에 수행

### 기부 체인 관리
락을 획득할 때:
1. 대기 중인 락 기록
2. 현재 스레드를 보유자의 donators 리스트에 추가 (우선순위 정렬)
3. `donate_priority()` 최대 8단계까지 재귀적 호출:
   - 보유자의 우선순위 < 현재 우선순위면 보유자 우선순위 갱신
   - 보유자가 다른 락을 기다리면 그 락의 보유자에게 기부 전파
   - 비대기 스레드에 도달하거나 최대 깊이까지 계속

락을 해제할 때:
1. 이 락을 기다리는 모든 donators 제거
2. 남은 기부로부터 우선순위 재계산
3. 더 이상 기부가 없으면 원래 우선순위로 복원

### 인터럽트 안전성
- 모든 동기화 연산이 원자적으로 인터럽트 비활성화
- 세마포어 up/down 연산이 인터럽트 컨텍스트 보존
- 논블로킹 연산(try-down)이 인터럽트 핸들러 지원

## 핵심 함수

| 함수 | 목적 |
|------|------|
| `sema_down()` | 우선순위 순서 대기를 지원하는 P 연산 |
| `sema_up()` | 최고 우선순위 대기자를 깨우는 V 연산 |
| `lock_acquire()` | 우선순위 기부를 지원하는 락 획득 |
| `lock_release()` | 락 해제 및 우선순위 재계산 |
| `donate_priority()` | 락 체인을 통한 우선순위 전파 |
| `cond_wait()` | 락 관리를 지원하는 조건 대기 |
| `cond_signal()` | 최고 우선순위 대기 스레드 깨우기 |
| `preemption_by_priority()` | 선점 필요 여부 확인 및 수행 |

## 사용 예제

```c
// 동기화 원시 요소 초기화
struct lock resource_lock;
struct condition data_ready;
lock_init(&resource_lock);
cond_init(&data_ready);

// 자동 우선순위 기부를 지원하는 임계 섹션
lock_acquire(&resource_lock);
// ... 보호된 코드 ...
if (condition_met) {
    cond_signal(&data_ready, &resource_lock);
}
lock_release(&resource_lock);

// 조건 변수를 이용한 대기
lock_acquire(&resource_lock);
while (!condition_met) {
    cond_wait(&data_ready, &resource_lock);
}
// ... 보호된 데이터로 진행 ...
lock_release(&resource_lock);
```

## 성능 특성

- **락 획득**: O(n) 삽입 시간, O(1) 기부 조회
- **락 해제**: 최악의 경우 O(n) 기부 제거
- **세마포어 연산**: O(n) 우선순위 정렬 삽입, O(n log n) 깨어남 전 정렬
- **우선순위 선점**: 정렬된 준비 큐를 이용한 O(1) 확인

## 테스트

다음을 포함하는 우선순위 스케줄링 테스트 케이스를 통과합니다:
- 준비 큐에서의 우선순위 순서 지정
- 우선순위 역전 방지 우선순위 기부
- 깊은 기부 전파를 지원하는 중첩 락 체인
- 조건 변수 우선순위 의미론
- 올바른 우선순위 복원을 지원하는 락 해제

## 설계 결정

1. **정렬된 삽입 vs. 전체 정렬**: 매 연산마다 `list_insert_ordered`를 사용하여 삽입 시간과 매 세마포어_up()의 전체 정렬을 회피하는 이점 사이의 트레이드오프 선택

2. **선택적 기부 제거**: 특정 락과 관련된 기부만 제거하여 스레드가 독립적인 우선순위 기부를 지원하는 다중 락 보유 가능

3. **최대 깊이 제한**: 중첩 기부를 8단계로 제한하여 무한 루프 방지 및 현실적 락 계층 구조 지원

4. **Mesa vs. Hoare 의미론**: 조건 변수가 Mesa 스타일 의미론(원자적이지 않은 신호/대기) 사용으로 깨어남 후 조건 재확인 요구

## 파일 구성

- `include/threads/synch.h` - 동기화 원시 요소 헤더
- `threads/synch.c` - 세마포어, 락, 조건 변수 구현
- `include/threads/thread.h` - 스레드 구조체 및 우선순위 함수
- `threads/thread.c` - 스레드 초기화 및 스케줄링

## 향후 개선 사항

- 락 경합(lock contention) 시나리오 성능 최적화

## 참고 자료

Pintos 공식 문서

