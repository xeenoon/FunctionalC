#include "list.h"
#include "profiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

enum {
    LIST_POOL_MAX = 512,
    LIST_POOL_CAPACITY_LIMIT = 8
};

static List *g_list_pool[LIST_POOL_MAX];
static int g_list_pool_count = 0;

static int normalized_capacity(int capacity) {
    int result = 2;
    while (result < capacity) {
        result *= 2;
    }
    return result;
}

List *init_list_with_capacity(int capacity) {
    uint64_t start = PROFILE_NOW_NS();
    int normalized = normalized_capacity(capacity);
    List *result = NULL;

    if (normalized <= LIST_POOL_CAPACITY_LIMIT) {
        for (int i = g_list_pool_count - 1; i >= 0; --i) {
            if (g_list_pool[i]->allocatedsize >= normalized) {
                result = g_list_pool[i];
                g_list_pool[i] = g_list_pool[g_list_pool_count - 1];
                --g_list_pool_count;
                break;
            }
        }
    }

    if (result == NULL) {
        result = malloc(sizeof(List));
        result->allocatedsize = normalized;
        result->data = malloc(result->allocatedsize * sizeof(void *));
    }

    result->payload_block = NULL;
    result->front = 0;
    result->rear = 0;
    result->size = 0;
    PROFILE_INC(init_list_calls);
    PROFILE_ADD(init_list_ns, PROFILE_NOW_NS() - start);
    return result;
}

List *init_list() {
    return init_list_with_capacity(2);
}

void list_reserve(List *list, int capacity) {
    if (capacity <= list->allocatedsize) {
        return;
    }

    uint64_t start = PROFILE_NOW_NS();
    int newsize = normalized_capacity(capacity);
    void **resultdata = calloc(newsize, sizeof(void *));
    memcpy(resultdata, list->data, list->size * sizeof(void *));

    free(list->data);
    list->data = resultdata;
    list->allocatedsize = newsize;
    list->front = 0;
    list->rear = list->size;
    PROFILE_INC(resize_calls);
    PROFILE_ADD(resize_ns, PROFILE_NOW_NS() - start);
    PROFILE_ADD(resize_items_copied, list->size);
}

void push_back(List *list, void *item) {
    uint64_t start = PROFILE_NOW_NS();
    if (list->size == list->allocatedsize) {
        resize(list);
    }
    list->data[list->size] = item;
    ++list->size;
    list->rear = list->size;
    PROFILE_INC(push_back_calls);
    PROFILE_ADD(push_back_ns, PROFILE_NOW_NS() - start);
}

void push_front(List *list, void *item) {
    uint64_t start = PROFILE_NOW_NS();
    if (list->size == list->allocatedsize) {
        resize(list);
    }
    memmove(&list->data[1], &list->data[0], list->size * sizeof(void *));
    list->data[0] = item;
    ++list->size;
    list->rear = list->size;
    PROFILE_INC(push_front_calls);
    PROFILE_ADD(push_front_ns, PROFILE_NOW_NS() - start);
}

void *list_get(List *list, int idx) {
    uint64_t start = PROFILE_NOW_NS();
    if (list->size == 0) {
        PROFILE_INC(list_get_calls);
        PROFILE_ADD(list_get_ns, PROFILE_NOW_NS() - start);
        return NULL;
    }
    PROFILE_INC(list_get_calls);
    PROFILE_ADD(list_get_ns, PROFILE_NOW_NS() - start);
    return list->data[idx];
}

void *pop(List *list) {
    uint64_t start = PROFILE_NOW_NS();
    if (list->size == 0) {
        PROFILE_INC(pop_calls);
        PROFILE_ADD(pop_ns, PROFILE_NOW_NS() - start);
        return NULL;
    }
    void *result = list->data[list->size - 1];
    --list->size;
    list->rear = list->size;
    PROFILE_INC(pop_calls);
    PROFILE_ADD(pop_ns, PROFILE_NOW_NS() - start);
    return result;
}

void *peek(List *list) {
    if (list->size == 0)
        return NULL;
    void *result = list->data[list->size - 1];
    return result;
}

void *popstart(List *list) {
    uint64_t start = PROFILE_NOW_NS();
    if (list->size == 0) {
        PROFILE_INC(popstart_calls);
        PROFILE_ADD(popstart_ns, PROFILE_NOW_NS() - start);
        return NULL;
    }
    void *result = list->data[0];
    if (list->size > 1) {
        memmove(&list->data[0], &list->data[1],
                (list->size - 1) * sizeof(void *));
    }
    --list->size;
    list->rear = list->size;
    PROFILE_INC(popstart_calls);
    PROFILE_ADD(popstart_ns, PROFILE_NOW_NS() - start);
    return result;
}

bool list_isempty(List *list) {
    uint64_t start = PROFILE_NOW_NS();
    PROFILE_INC(list_isempty_calls);
    PROFILE_ADD(list_isempty_ns, PROFILE_NOW_NS() - start);
    return list->size == 0;
}

void resize(List *list) {
    list_reserve(list, list->allocatedsize * 2);
}

void freelist(List *list) {
    uint64_t start = PROFILE_NOW_NS();
    if (list->payload_block == NULL &&
        list->allocatedsize <= LIST_POOL_CAPACITY_LIMIT &&
        g_list_pool_count < LIST_POOL_MAX) {
        list->front = 0;
        list->rear = 0;
        list->size = 0;
        g_list_pool[g_list_pool_count++] = list;
    } else {
        free(list->payload_block);
        free(list->data);
        free(list);
    }
    PROFILE_INC(freelist_calls);
    PROFILE_ADD(freelist_ns, PROFILE_NOW_NS() - start);
}

void list_remove(List *list, int idx) {
    if (idx < 0 || idx >= list->size)
        return; // invalid index, do nothing or handle error

    if (idx < list->size - 1) {
        memmove(&list->data[idx], &list->data[idx + 1],
                (list->size - idx - 1) * sizeof(void *));
    }
    list->size--;
    list->rear = list->size;
}
