#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ut.h>
#include <linklist.h>
#include <pthread.h>
#include <unistd.h>
#include <libgen.h>
#include <time.h>

typedef struct {
    int start;
    int end;
    linked_list_t *list;
} parallel_insert_arg;

static void *parallel_insert(void *user) {
    parallel_insert_arg *arg = (parallel_insert_arg *)user;
    int i;
    for (i = arg->start; i <= arg->end; i++) {
        char *v = malloc(100);
        sprintf(v, "test%d", i+1);
        list_set_value(arg->list, i, v);
    }
    return NULL;
}

static int free_count = 0;
static void free_value(void *val) {
    free(val);
    free_count++;
}

int iterator_callback(void *item, uint32_t idx, void *user) {
    int *failed = (int *)user;
    char *val = (char *)item;
    char test[100];
    sprintf(test, "test%u", idx+1);
    if (strcmp(test, val) != 0) {
        ut_failure("Value at index %d doesn't match %s  (%s)", idx, test, val);
        *failed = 1;
        return 0;
    }
    return 1;
}

typedef struct {
    linked_list_t *list;
    int count;
} queue_worker_arg;

void *queue_worker(void *user) {
    void *v;
    queue_worker_arg *arg = (queue_worker_arg *)user;
    for (;;) {
        v = list_shift_value(arg->list);
        if (v) {
            __sync_add_and_fetch(&arg->count, 1);
            free(v);
        }
        pthread_testcancel();
    }
    return NULL;
}

static int
cmp(void *v1, void *v2)
{
    int *i1 = (int *)v1;
    int *i2 = (int *)v2;
    if (*i1 == *i2)
        return 0;
    else if (*i1 > *i2)
        return -1;
    return 1;
}

int main(int argc, char **argv) {
    ut_init(basename(argv[0]));

    ut_testing("Create list");
    linked_list_t *list = list_create();
    ut_result(list != NULL, "Can't create a new list");

    ut_testing("list_push_value() return value on success");
    ut_result(list_push_value(list, strdup("test1")) == 0, "Can't push value");

    list_push_value(list, strdup("test2"));
    list_push_value(list, strdup("test3"));

    ut_testing("list_count() after push");
    ut_result(list_count(list) == 3, "Length is not 3 (but %d) after 3 pushes", list_count(list));

    ut_testing("list_pick_value()");
    ut_validate_string(list_pick_value(list, 1), "test2");

    ut_testing("list_shift_value()");
    char *v = list_shift_value(list);
    ut_validate_string(v, "test1"); 

    ut_testing("list_count() after shift");
    ut_result(list_count(list) == 2, "Length is not 2 after shifting one value");

    ut_testing("list_unshift_value()");
    list_unshift_value(list, v);
    ut_validate_string(list_pick_value(list, 0), "test1"); 

    ut_testing("list_push_value() accepts NULL");
    ut_result(list_push_value(list, NULL) == 0, "list_push_value didn't accept a NULL value");

    ut_testing("length updated after pushing NULL");
    ut_result(list_count(list) == 4, "Length is not 4 after pushing the NULL value");

    ut_testing("list_pop_value()");
    v = list_pop_value(list);
    ut_result(list_count(list) == 3, "Length is not 3 after popping a value from the list");
    ut_testing("list_pop_value() returned last element");
    ut_result(v == NULL, "Value is not NULL");

    ut_testing("still list_pop_value() return value");
    v = list_pop_value(list);
    ut_validate_string(v, "test3");

    list_push_value(list, v);
    ut_testing("list_count() consistent");
    ut_result(list_count(list) == 3, "Length is not 3 after pushing back the 'test3' value");

    ut_testing("pushing 100 values to the list");
    int i;
    for (i = 4; i <= 100; i++) {
        char *val = malloc(100);
        sprintf(val, "test%d", i);
        list_push_value(list, val);
    }
    ut_result(list_count(list) == 100, "Length is not 100"); 

    ut_testing("Order is preserved");
    int failed = 0;
    for (i = 0; i < 100; i++) {
        char test[100];
        sprintf(test, "test%d", i+1);
        char *val = list_pick_value(list, i);
        if (strcmp(val, test) != 0) {
            ut_failure("Value at index %d doesn't match %s  (%s)", i, test, val);
            failed = 1;
            break;
        }
    }
    if (!failed)
        ut_success();

    ut_testing("Value iterator");
    failed = 0;
    list_foreach_value(list, iterator_callback, &failed);
    if (!failed)
        ut_success();
    else
        ut_failure("Order is wrong");

    ut_testing("list_set_value() overrides previous value (and returns it)");
    // Note: we need to copy the string because the value will be released from our free callback
    char *test_value = strdup("blah");
    char *old_value = list_set_value(list, 5, test_value);
    char *new_value = list_pick_value(list, 5);
    if (strcmp(old_value, "test6") != 0) {
        ut_failure("Old value is wrong ('%s' should have been 'test6', old_value)", old_value);
    } else if (strcmp(new_value, test_value) != 0) {
        ut_failure("New value is wrong ('%s' should have been '%s', old_value)", new_value, test_value);
    } else {
        ut_success();
    }
    free(old_value);

    ut_testing("list_swap_values()");
    list_swap_values(list, 9, 19);
    char *v1 = list_pick_value(list, 9);
    char *v2 = list_pick_value(list, 19);
    if (strcmp(v1, "test20") != 0) {
        ut_failure("Value at position 9 (%s) should have been equal to 'test20'", v1);
    } else if (strcmp(v2, "test10") != 0) {
        ut_failure("Value at position 19 (%s) should have been equal to 'test10'", v2);
    } else {
        ut_success();
    }

    ut_testing("list_move_value()");
    old_value = list_pick_value(list, 45);
    list_move_value(list, 45, 67);
    ut_validate_string(list_pick_value(list, 67), old_value);

    list_set_free_value_callback(list, free_value);

    ut_testing("list_clear()");
    list_clear(list);
    ut_result(list_count(list) == 0, "List count is not 0 after clear_list()");

    ut_testing("free value callback");
    ut_result(free_count == 100, "Free count is not 100 after clear_list() (%d)", free_count);

    int num_parallel_threads = 5;
    int num_parallel_items = 10000;

    ut_testing("Parallel insert (%d items)", num_parallel_items);

    parallel_insert_arg args[num_parallel_threads];
    pthread_t threads[num_parallel_threads];
    for (i = 0; i < num_parallel_threads; i++) {
        args[i].start = 0 + (i * (num_parallel_items / num_parallel_threads)); 
        args[i].end = args[i].start + (num_parallel_items / num_parallel_threads) -1;
        args[i].list = list;
        pthread_create(&threads[i], NULL, parallel_insert, &args[i]);
    }

    for (i = 0; i < num_parallel_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    ut_result(list_count(list) == num_parallel_items, "Count is not %d after parallel insert (%d)", num_parallel_items, list_count(list));

    ut_testing("Order after parallel insertion");
    failed = 0;
    list_foreach_value(list, iterator_callback, &failed);
    if (!failed)
        ut_success();
    else
        ut_failure("Order is wrong");

    free_count = 0;
    ut_testing("list_destroy()");
    list_destroy(list);
    ut_result(free_count == num_parallel_items, "Free count is not %d after list_destroy() (%d)", num_parallel_items, free_count);

    queue_worker_arg arg = {
        .list = list_create(),
        .count = 0
    };

    int num_queued_items = 10000;
    ut_testing("Threaded queue (%d pull-workers, %d items pushed to the queue from the main thread)",
              num_parallel_threads, num_queued_items);

    for (i = 0; i < num_parallel_threads; i++) {
        pthread_create(&threads[i], NULL, queue_worker, &arg);
    }

    for (i = 0; i < num_queued_items; i++) {
        char *val = malloc(21);
        sprintf(val, "%d", i);
        list_push_value(arg.list, val);
    }

    while(list_count(arg.list))
        usleep(500);

    for (i = 0; i < num_parallel_threads; i++) {
        pthread_cancel(threads[i]);
        pthread_join(threads[i], NULL);
    }

    ut_result(arg.count == num_queued_items, "Handled items should have been %d (was %d)", num_queued_items, arg.count);

    list_destroy(arg.list);

    linked_list_t *tagged_list = list_create();
    for (i = 0; i < 100; i++) {
        char key[21];
        char val[21];
        sprintf(key, "key%d", i);
        sprintf(val, "value%d", i);
        tagged_value_t *tv1 = list_create_tagged_value(key, val, strlen(val));
        list_push_tagged_value(tagged_list, tv1);
    }

    ut_testing("get_tagged_value()");
    tagged_value_t *test_tagged_value = list_get_tagged_value(tagged_list, "key10");
    ut_validate_string(test_tagged_value->value, "value10");

    ut_testing("set_tagged_value()");
    test_tagged_value = list_set_tagged_value(tagged_list, "key10", "test", 4, 0);
    if (strcmp(test_tagged_value->value, "value10") != 0)
        ut_failure("Old value doesn't match");
    else {
        test_tagged_value = list_get_tagged_value(tagged_list, "key10");
        ut_validate_string(test_tagged_value->value, "test");
    }

    list_destroy(tagged_list);

    ut_testing("list_sort()");
    linked_list_t *t = list_create();
    int i1 = 5,i2 = 2,i3 = 1,i4 = 7,i5 = 4,i6 = 3, i7 = 1, i8 = 1, i9 = 1, i10 = 1, i11 = 1, i12 = 5, i13 = 5, i14 = 5;
    list_push_value(t, &i1);
    list_push_value(t, &i2);
    list_push_value(t, &i3);
    list_push_value(t, &i4);
    list_push_value(t, &i5);
    list_push_value(t, &i6);
    list_push_value(t, &i7);
    list_push_value(t, &i8);
    list_push_value(t, &i9);
    list_push_value(t, &i10);
    list_push_value(t, &i11);
    list_push_value(t, &i12);
    list_push_value(t, &i13);
    list_push_value(t, &i14);

    list_sort(t, cmp);
    failed = 0;
    int prev, len = list_count(t);
    for (i = 0; i < len; i++) {
        int cur = *((int *)list_pick_value(t, i));
        if (i > 0 && cur < prev) {
            ut_failure("%d is smaller than the previous element %d", cur, prev);
            failed++;
        }
        prev = cur;
    }
    if (!failed)
        ut_success();

    //sort performance
    int loop = 10000, max_num = 10000;
    int a[max_num];
    int end, begin = time(NULL);

    for (i = 0; i < loop; ++i) {
        linked_list_t *t = list_create();
        int j = 0;
        for (j = 0; j < max_num; ++j) {
            a[j] = 10;
            list_push_value(t, a + j);
        }
        list_qsort(t, cmp);
        list_destroy(t);
    }
    end = time(NULL);
    fprintf(stderr, "qsort time consumed: %d\n", end - begin);

    begin = time(NULL);
    for (i = 0; i < loop; ++i) {
        linked_list_t *t = list_create();
        int j = 0;
        for (j = 0; j < max_num; ++j) {
            a[j] = 10;
            list_push_value(t, a + j);
        }
        list_sort(t, cmp);
        list_destroy(t);
    }
    end = time(NULL);
    fprintf(stderr, "time consumed: %d\n", end - begin);

    begin = time(NULL);
    for (i = 0; i < loop; ++i) {
        linked_list_t *t = list_create();
        int j = 0;
        for (j = 0; j < max_num; ++j) {
            a[j] = j;
            list_push_value(t, a + j);
        }
        list_qsort(t, cmp);
        list_destroy(t);
    }
    end = time(NULL);
    fprintf(stderr, "qsort time consumed: %d\n", end - begin);

    begin = time(NULL);
    for (i = 0; i < loop; ++i) {
        linked_list_t *t = list_create();
        int j = 0;
        for (j = 0; j < max_num; ++j) {
            a[j] = j;
            list_push_value(t, a + j);
        }
        list_sort(t, cmp);
        list_destroy(t);
    }
    end = time(NULL);
    fprintf(stderr, "time consumed: %d\n", end - begin);

    begin = time(NULL);
    for (i = 0; i < loop; ++i) {
        linked_list_t *t = list_create();
        int j = 0;
        for (j = 0; j < max_num; ++j) {
            a[j] = max_num - j;
            list_push_value(t, a + j);
        }
        list_qsort(t, cmp);
        list_destroy(t);
    }
    end = time(NULL);
    fprintf(stderr, "qsort time consumed: %d\n", end - begin);

    begin = time(NULL);
    for (i = 0; i < loop; ++i) {
        linked_list_t *t = list_create();
        int j = 0;
        for (j = 0; j < max_num; ++j) {
            a[j] = max_num - j;
            list_push_value(t, a + j);
        }
        list_sort(t, cmp);
        list_destroy(t);
    }
    end = time(NULL);
    fprintf(stderr, "time consumed: %d\n", end - begin);

    {
        int j;
        for (j = 0; j < max_num; ++j)
            a[j] = rand();
    }
    begin = time(NULL);
    for (i = 0; i < loop; ++i) {
        linked_list_t *t = list_create();
        int j = 0;
        for (j = 0; j < max_num; ++j) {
            list_push_value(t, a + j);
        }
        list_qsort(t, cmp);
        list_destroy(t);
    }
    end = time(NULL);
    fprintf(stderr, "qsort time consumed: %d\n", end - begin);

    {
        int j;
        for (j = 0; j < max_num; ++j)
            a[j] = rand();
    }
    begin = time(NULL);
    for (i = 0; i < loop; ++i) {
        linked_list_t *t = list_create();
        int j = 0;
        for (j = 0; j < max_num; ++j) {
            list_push_value(t, a + j);
        }
        list_sort(t, cmp);
        list_destroy(t);
    }
    end = time(NULL);
    fprintf(stderr, "time consumed: %d\n", end - begin);

    ut_summary();

    exit(ut_failed);
}
