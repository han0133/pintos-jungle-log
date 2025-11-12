#include "list.h"
#include "../debug.h"

// 리스트 삽입 최적화 측정용 코드
#ifdef MEASURE_INSERT
static unsigned insert_ordered_count = 0;

void print_list_stats(void)
{
	printf("\n=== LIST STATISTICS ===\n");
	printf("list_insert_ordered() calls: %u\n", insert_ordered_count);
	printf("========================\n\n");
}

void reset_list_stats(void)
{
	insert_ordered_count = 0;
}
#endif
// 리스트 삽입 최적화 측정용 코드 END.

/* Our doubly linked lists have two header elements: the "head"
	 just before the first element and the "tail" just after the
	 last element.  The `prev' link of the front header is null, as
	 is the `next' link of the back header.  Their other two links
	 point toward each other via the interior elements of the list.

	 An empty list looks like this:

	 +------+     +------+
	 <---| head |<--->| tail |--->
	 +------+     +------+

	 A list with two elements in it looks like this:

	 +------+     +-------+     +-------+     +------+
	 <---| head |<--->|   1   |<--->|   2   |<--->| tail |<--->
	 +------+     +-------+     +-------+     +------+

	 The symmetry of this arrangement eliminates lots of special
	 cases in list processing.  For example, take a look at
	 list_remove(): it takes only two pointer assignments and no
	 conditionals.  That's a lot simpler than the code would be
	 without header elements.

	 (Because only one of the pointers in each header element is used,
	 we could in fact combine them into a single header element
	 without sacrificing this simplicity.  But using two separate
	 elements allows us to do a little bit of checking on some
	 operations, which can be valuable.) */

static bool is_sorted(struct list_elem *a, struct list_elem *b,
											list_less_func *less, void *aux) UNUSED;

/* Returns true if ELEM is a head, false otherwise. */
static inline bool
is_head(struct list_elem *elem)
{
	return elem != NULL && elem->prev == NULL && elem->next != NULL;
}

/* Returns true if ELEM is an interior element,
	 false otherwise. */
static inline bool
is_interior(struct list_elem *elem)
{
	return elem != NULL && elem->prev != NULL && elem->next != NULL;
}

/* Returns true if ELEM is a tail, false otherwise. */
static inline bool
is_tail(struct list_elem *elem)
{
	return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* Initializes LIST as an empty list. */
void list_init(struct list *list)
{
	ASSERT(list != NULL);
	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;
}

/* Returns the beginning of LIST.  */
struct list_elem *
list_begin(struct list *list)
{
	ASSERT(list != NULL);
	return list->head.next;
}

/* Returns the element after ELEM in its list.  If ELEM is the
	 last element in its list, returns the list tail.  Results are
	 undefined if ELEM is itself a list tail. */
struct list_elem *
list_next(struct list_elem *elem)
{
	ASSERT(is_head(elem) || is_interior(elem));
	return elem->next;
}

/* Returns LIST's tail.

	 list_end() is often used in iterating through a list from
	 front to back.  See the big comment at the top of list.h for
	 an example. */
struct list_elem *
list_end(struct list *list)
{
	ASSERT(list != NULL);
	return &list->tail;
}

/* Returns the LIST's reverse beginning, for iterating through
	 LIST in reverse order, from back to front. */
struct list_elem *
list_rbegin(struct list *list)
{
	ASSERT(list != NULL);
	return list->tail.prev;
}

/* Returns the element before ELEM in its list.  If ELEM is the
	 first element in its list, returns the list head.  Results are
	 undefined if ELEM is itself a list head. */
struct list_elem *
list_prev(struct list_elem *elem)
{
	ASSERT(is_interior(elem) || is_tail(elem));
	return elem->prev;
}

/* Returns LIST's head.

	 list_rend() is often used in iterating through a list in
	 reverse order, from back to front.  Here's typical usage,
	 following the example from the top of list.h:

	 for (e = list_rbegin (&foo_list); e != list_rend (&foo_list);
	 e = list_prev (e))
	 {
	 struct foo *f = list_entry (e, struct foo, elem);
	 ...do something with f...
	 }
	 */
struct list_elem *
list_rend(struct list *list)
{
	ASSERT(list != NULL);
	return &list->head;
}

/* Return's LIST's head.

	 list_head() can be used for an alternate style of iterating
	 through a list, e.g.:

	 e = list_head (&list);
	 while ((e = list_next (e)) != list_end (&list))
	 {
	 ...
	 }
	 */
struct list_elem *
list_head(struct list *list)
{
	ASSERT(list != NULL);
	return &list->head;
}

/* Return's LIST's tail. */
struct list_elem *
list_tail(struct list *list)
{
	ASSERT(list != NULL);
	return &list->tail;
}

/* Inserts ELEM just before BEFORE, which may be either an
	 interior element or a tail.  The latter case is equivalent to
	 list_push_back(). */
void list_insert(struct list_elem *before, struct list_elem *elem)
{
	ASSERT(is_interior(before) || is_tail(before));
	ASSERT(elem != NULL);

	elem->prev = before->prev;
	elem->next = before;
	before->prev->next = elem;
	before->prev = elem;
}

/* Removes elements FIRST though LAST (exclusive) from their
	 current list, then inserts them just before BEFORE, which may
	 be either an interior element or a tail. */
void list_splice(struct list_elem *before,
								 struct list_elem *first, struct list_elem *last)
{
	ASSERT(is_interior(before) || is_tail(before));
	if (first == last)
		return;
	last = list_prev(last);

	ASSERT(is_interior(first));
	ASSERT(is_interior(last));

	/* Cleanly remove FIRST...LAST from its current list. */
	first->prev->next = last->next;
	last->next->prev = first->prev;

	/* Splice FIRST...LAST into new list. */
	first->prev = before->prev;
	last->next = before;
	before->prev->next = first;
	before->prev = last;
}

/* Inserts ELEM at the beginning of LIST, so that it becomes the
	 front in LIST. */
void list_push_front(struct list *list, struct list_elem *elem)
{
	list_insert(list_begin(list), elem);
}

/* Inserts ELEM at the end of LIST, so that it becomes the
	 back in LIST. */
void list_push_back(struct list *list, struct list_elem *elem)
{
	list_insert(list_end(list), elem);
}

/* Removes ELEM from its list and returns the element that
	 followed it.  Undefined behavior if ELEM is not in a list.

	 It's not safe to treat ELEM as an element in a list after
	 removing it.  In particular, using list_next() or list_prev()
	 on ELEM after removal yields undefined behavior.  This means
	 that a naive loop to remove the elements in a list will fail:

 ** DON'T DO THIS **
 for (e = list_begin (&list); e != list_end (&list); e = list_next (e))
 {
 ...do something with e...
 list_remove (e);
 }
 ** DON'T DO THIS **

 Here is one correct way to iterate and remove elements from a
list:

for (e = list_begin (&list); e != list_end (&list); e = list_remove (e))
{
...do something with e...
}

If you need to free() elements of the list then you need to be
more conservative.  Here's an alternate strategy that works
even in that case:

while (!list_empty (&list))
{
struct list_elem *e = list_pop_front (&list);
...do something with e...
}
*/
struct list_elem *
list_remove(struct list_elem *elem)
{
	ASSERT(is_interior(elem));
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	return elem->next;
}

/* Removes the front element from LIST and returns it.
	 Undefined behavior if LIST is empty before removal. */
struct list_elem *
list_pop_front(struct list *list)
{
	struct list_elem *front = list_front(list);
	list_remove(front);
	return front;
}

/* Removes the back element from LIST and returns it.
	 Undefined behavior if LIST is empty before removal. */
struct list_elem *
list_pop_back(struct list *list)
{
	struct list_elem *back = list_back(list);
	list_remove(back);
	return back;
}

/* Returns the front element in LIST.
	 Undefined behavior if LIST is empty. */
struct list_elem *
list_front(struct list *list)
{
	ASSERT(!list_empty(list));
	return list->head.next;
}

/* Returns the back element in LIST.
	 Undefined behavior if LIST is empty. */
struct list_elem *
list_back(struct list *list)
{
	ASSERT(!list_empty(list));
	return list->tail.prev;
}

/* Returns the number of elements in LIST.
	 Runs in O(n) in the number of elements. */
size_t
list_size(struct list *list)
{
	struct list_elem *e;
	size_t cnt = 0;

	for (e = list_begin(list); e != list_end(list); e = list_next(e))
		cnt++;
	return cnt;
}

/* Returns true if LIST is empty, false otherwise. */
bool list_empty(struct list *list)
{
	return list_begin(list) == list_end(list);
}

/* Swaps the `struct list_elem *'s that A and B point to. */
static void
swap(struct list_elem **a, struct list_elem **b)
{
	struct list_elem *t = *a;
	*a = *b;
	*b = t;
}

/* Reverses the order of LIST. */
void list_reverse(struct list *list)
{
	if (!list_empty(list))
	{
		struct list_elem *e;

		for (e = list_begin(list); e != list_end(list); e = e->prev)
			swap(&e->prev, &e->next);
		swap(&list->head.next, &list->tail.prev);
		swap(&list->head.next->prev, &list->tail.prev->next);
	}
}

/* Returns true only if the list elements A through B (exclusive)
	 are in order according to LESS given auxiliary data AUX. */
static bool
is_sorted(struct list_elem *a, struct list_elem *b,
					list_less_func *less, void *aux)
{
	if (a != b)
		while ((a = list_next(a)) != b)
			if (less(a, list_prev(a), aux))
				return false;
	return true;
}

/* Finds a run, starting at A and ending not after B, of list
	 elements that are in nondecreasing order according to LESS
	 given auxiliary data AUX.  Returns the (exclusive) end of the
	 run.
	 A through B (exclusive) must form a non-empty range. */
// 정렬된 구간(run)의 끝(정렬이 깨지는 첫 번째 요소)을 찾아 반환한다
static struct list_elem *
find_end_of_run(struct list_elem *a, struct list_elem *b, list_less_func *less, void *aux)
{
	ASSERT(a != NULL);
	ASSERT(b != NULL);
	ASSERT(less != NULL);
	ASSERT(a != b);

	do
	{
		a = list_next(a);
	} while (a != b && !less(a, list_prev(a), aux));
	return a;
}

/* Merges A0 through A1B0 (exclusive) with A1B0 through B1
	 (exclusive) to form a combined range also ending at B1
	 (exclusive).  Both input ranges must be nonempty and sorted in
	 nondecreasing order according to LESS given auxiliary data
	 AUX.  The output range will be sorted the same way. */
static void
inplace_merge(struct list_elem *first_run_start, struct list_elem *first_run_end,
							struct list_elem *second_run_end,
							list_less_func *less, void *aux)
{
	ASSERT(first_run_start != NULL);
	ASSERT(first_run_end != NULL);
	ASSERT(second_run_end != NULL);
	ASSERT(less != NULL);
	ASSERT(is_sorted(first_run_start, first_run_end, less, aux));
	ASSERT(is_sorted(first_run_end, second_run_end, less, aux));

	while (first_run_start != first_run_end && first_run_end != second_run_end) // 두 run 모두 병합이 끝나지 않았을 때 반복
		if (!less(first_run_end, first_run_start, aux))
			first_run_start = list_next(first_run_start);
		else
		{
			first_run_end = list_next(first_run_end);
			list_splice(first_run_start, list_prev(first_run_end), first_run_end);
		}
}

/* 리스트를 LESS 비교 함수와 AUX 데이터 기준으로 정렬합니다.
	자연 병합 정렬(natural iterative merge sort) 방식으로 동작하며,
	시간 복잡도는 O(n log n), 공간 복잡도는 O(1)입니다. */
void list_sort(struct list *list, list_less_func *less, void *aux)
{
	size_t output_run_cnt; /* 현재 순회에서 병합된 run(정렬 구간) 개수 */

	ASSERT(list != NULL); // 리스트 포인터 NULL 체크
	ASSERT(less != NULL); // 비교 함수 NULL 체크

	// 리스트 전체가 하나의 run이 될 때까지 반복
	do
	{
		struct list_elem *run_start;			/* 첫 번째 run의 시작 */
		struct list_elem *first_run_end;	/* 첫 번째 run의 끝, 두 번째 run의 시작 */
		struct list_elem *second_run_end; /* 두 번째 run의 끝 */

		output_run_cnt = 0; // run 병합 시마다 카운트 증가

		// 리스트의 처음부터 끝까지 run 단위로 병합
		for (run_start = list_begin(list); run_start != list_end(list); run_start = second_run_end)
		{
			output_run_cnt++; // run 병합 시마다 카운트 증가

			first_run_end = find_end_of_run(run_start, list_end(list), less, aux); // 첫 번째 run의 끝 찾기
			if (first_run_end == list_end(list))																	 // 두 번째 run이 없으면 종료
				break;
			second_run_end = find_end_of_run(first_run_end, list_end(list), less, aux); // 두 번째 run의 끝 찾기

			inplace_merge(run_start, first_run_end, second_run_end, less, aux); // 두 run을 병합
		}
	} while (output_run_cnt > 1); // run이 2개 이상이면 계속 반복

	ASSERT(is_sorted(list_begin(list), list_end(list), less, aux)); // 최종적으로 정렬 확인
}

/* less()의 기준에 따라 정렬됨.
	반드시 리스트가 이미 정렬되어 있을 때만 올바르게 동작함
	시간 복잡도 O(n). */
void list_insert_ordered(struct list *list, struct list_elem *elem,
												 list_less_func *less, void *aux)
{
// 리스트 삽입 최적화 측정용 코드
#ifdef MEASURE_INSERT
	insert_ordered_count++;
#endif
	// 리스트 삽입 최적화 측정용 코드 END.

	struct list_elem *e;

	ASSERT(list != NULL);
	ASSERT(elem != NULL);
	ASSERT(less != NULL);

	for (e = list_begin(list); e != list_end(list); e = list_next(e))
		if (less(elem, e, aux))
			break;
	return list_insert(e, elem);
}

/* Iterates through LIST and removes all but the first in each
	 set of adjacent elements that are equal according to LESS
	 given auxiliary data AUX.  If DUPLICATES is non-null, then the
	 elements from LIST are appended to DUPLICATES. */
void list_unique(struct list *list, struct list *duplicates,
								 list_less_func *less, void *aux)
{
	struct list_elem *elem, *next;

	ASSERT(list != NULL);
	ASSERT(less != NULL);
	if (list_empty(list))
		return;

	elem = list_begin(list);
	while ((next = list_next(elem)) != list_end(list))
		if (!less(elem, next, aux) && !less(next, elem, aux))
		{
			list_remove(next);
			if (duplicates != NULL)
				list_push_back(duplicates, next);
		}
		else
			elem = next;
}

/* Returns the element in LIST with the largest value according
	 to LESS given auxiliary data AUX.  If there is more than one
	 maximum, returns the one that appears earlier in the list.  If
	 the list is empty, returns its tail. */
struct list_elem *
list_max(struct list *list, list_less_func *less, void *aux)
{
	struct list_elem *max = list_begin(list);
	if (max != list_end(list))
	{
		struct list_elem *e;

		for (e = list_next(max); e != list_end(list); e = list_next(e))
			if (less(max, e, aux))
				max = e;
	}
	return max;
}

/* Returns the element in LIST with the smallest value according
	 to LESS given auxiliary data AUX.  If there is more than one
	 minimum, returns the one that appears earlier in the list.  If
	 the list is empty, returns its tail. */
struct list_elem *
list_min(struct list *list, list_less_func *less, void *aux)
{
	struct list_elem *min = list_begin(list);
	if (min != list_end(list))
	{
		struct list_elem *e;

		for (e = list_next(min); e != list_end(list); e = list_next(e))
			if (less(e, min, aux))
				min = e;
	}
	return min;
}
