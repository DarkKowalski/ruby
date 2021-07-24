#include "event_profiling.h"

/* Ruby */
#include "ractor_core.h"
#include "ruby.h"
#include "ruby/atomic.h"
#include "ruby/thread_native.h"
#include "vm_core.h"

#if USE_EVENT_PROFILING

/* GCC warning */
#include <sys/syscall.h>
pid_t gettid(void) { return syscall(SYS_gettid); }

/* Assertion */
#define refute_greater_or_equal(var, compare, reason)                          \
    do                                                                         \
    {                                                                          \
        if ((var) >= (compare))                                                \
        {                                                                      \
            fprintf(stderr, ("[ERROR] event_profiling: " reason), var);        \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

/* Global config */
static event_profiling_config_t *rb_event_profiling_config;

/* Global bucket */
static profiling_event_bucket_t *rb_profiling_event_bucket;

/* Phase strings */
static const char profiling_event_phase_str[] = {'B', 'E', 'O'};

/* Internal functions */
static inline int get_total_events(void)
{
    int total = rb_profiling_event_bucket->system_init_event_list->tail;
    int ractors = rb_profiling_event_bucket->ractors;
    for (int i = 0; i < ractors; i++)
    {
        profiling_event_list_t *list =
            rb_profiling_event_bucket->ractor_profiling_event_lists[i];
        total += list->tail;
    }

    return total;
}

static inline time_t microsecond_timestamp(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    time_t us = t.tv_sec * 1E6 + t.tv_nsec / 1E3;

    return us;
}

static inline profiling_event_list_t *init_profiling_event_list(void)
{
    profiling_event_list_t *list =
        (profiling_event_list_t *)malloc(sizeof(profiling_event_list_t));

    profiling_event_t *events = (profiling_event_t *)malloc(
        sizeof(profiling_event_t) *
        rb_event_profiling_config->max_ractor_events);

    int *event_indexes = (int *)malloc(
        sizeof(int) * rb_event_profiling_config->max_call_stack_depth);

    list->tail = 0;
    list->last_event_id = 0;
    list->events = events;

    list->call_stack.top = 0;
    list->call_stack.event_indexes = event_indexes;

    return list;
}

static inline profiling_event_list_t *
select_profiling_event_list(const bool system)
{
    if (system)
    {
        return rb_profiling_event_bucket->system_init_event_list;
    }
    else
    {
        return GET_RACTOR()->event_profiling_storage;
    }
}

static inline profiling_event_t *get_a_profiling_event_slot(const bool system,
                                                            int *ret_index)
{
    profiling_event_list_t *list = select_profiling_event_list(system);
    int                     index = list->tail++;

    refute_greater_or_equal(index, rb_event_profiling_config->max_ractor_events,
                            "Too many events. %d \n");

    profiling_event_t *event = &(list->events[index]);

    event->ractor = system ? 0 : GET_RACTOR()->pub.id;

    if (ret_index != NULL)
    {
        *ret_index = index;
    }
    return event;
}

static inline int get_a_new_event_id(const bool system)
{
    profiling_event_list_t *list = select_profiling_event_list(system);
    return list->last_event_id++;
}

static inline int serialize_profiling_event(const profiling_event_t *event,
                                            char *buffer, const int offset)
{
    char *event_buffer = buffer + offset;
    int   count = -1;

    switch (event->phase)
    {
    case PROFILING_EVENT_PHASE_SNAPSHOT:
        count = sprintf(event_buffer,
                        "{\"name\": \"snapshot-%d\",\n"
                        "\"id\":\"%d(%d)\",\n"
                        "\"ph\":\"%c\",\n"
                        "\"pid\":\"%i\",\n"
                        "\"tid\":\"%i\",\n"
                        "\"ts\":\"%ld\",\n"
                        "\"args\": {\"snapshot\":{\"name\": \"%s:%s(%d)\", "
                        "\"line\": \"%d\", "
                        "\"ractor\":\"%d\",\"reason\": \"%s\"}}},\n",
                        event->tid, event->tid, event->id,
                        profiling_event_phase_str[event->phase], event->pid,
                        event->tid, event->timestamp, event->file,
                        event->function, event->id, event->line, event->ractor,
                        event->snapshot_reason);
        break;
    case PROFILING_EVENT_PHASE_BEGIN:
    case PROFILING_EVENT_PHASE_END:
        count =
            sprintf(event_buffer,
                    "{\"name\": \"%s:%s(%d)\",\n"
                    "\"ph\":\"%c\",\n"
                    "\"pid\":\"%i\",\n"
                    "\"tid\":\"%i\",\n"
                    "\"ts\":%ld,\n"
                    "\"args\": {\"line\": \"%d\", \"ractor\":\"%d\"}},\n",
                    event->file, event->function, event->id,
                    profiling_event_phase_str[event->phase], event->pid,
                    event->tid, event->timestamp, event->line, event->ractor);
        break;
    default:
        fprintf(stderr, "[ERROR] event_profiling: unknown phase %d\n",
                event->phase);
        count = 0;
    }

    return count;
}

static inline int
serialize_profiling_event_list(const profiling_event_list_t *list, char *buffer,
                               const int offset)
{
    int events = list->tail;
    int list_offset = offset;
    for (int i = 0; i < events; i++)
    {
        list_offset +=
            serialize_profiling_event(&(list->events[i]), buffer, list_offset);
    }
    return list_offset;
}

static inline void destroy_profiling_event_list(profiling_event_list_t *list)
{
    free(list->events);
    free(list->call_stack.event_indexes);
    free(list);
}

static inline void destroy_profiling_event_bucket(void)
{
    int ractors = rb_profiling_event_bucket->ractors;
    for (int i = 0; i < ractors; i++)
    {
        destroy_profiling_event_list(
            rb_profiling_event_bucket->ractor_profiling_event_lists[i]);
    }

    free(rb_profiling_event_bucket->system_init_event_list);
    free(rb_profiling_event_bucket->ractor_profiling_event_lists);
    free(rb_profiling_event_bucket);
}

static inline profiling_event_bucket_t *init_profiling_event_bucket(void)
{
    profiling_event_bucket_t *bucket =
        (profiling_event_bucket_t *)malloc(sizeof(profiling_event_bucket_t));
    bucket->system_init_event_list = init_profiling_event_list();
    bucket->ractor_profiling_event_lists = (profiling_event_list_t **)malloc(
        sizeof(profiling_event_list_t *) *
        rb_event_profiling_config->max_ractors);
    bucket->ractors = 0;

    rb_profiling_event_bucket = bucket;
    return bucket;
}

static inline void serialize_profiling_event_bucket(const char *outfile)
{
    /* Prepare the buffer */
    int json_symbol_size = 64;
    int event_buffer_size = 256;
    int total_events = get_total_events();
    int buffer_size = total_events * (event_buffer_size + json_symbol_size) +
                      json_symbol_size;
    char *bucket_buffer = (char *)malloc(sizeof(char) * buffer_size);

    /* Serialize */
    int offset = sprintf(bucket_buffer, "[");
    int ractors = rb_profiling_event_bucket->ractors;

    offset = serialize_profiling_event_list(
        rb_profiling_event_bucket->system_init_event_list, bucket_buffer,
        offset);
    for (int i = 0; i < ractors; i++)
    {
        profiling_event_list_t *list =
            rb_profiling_event_bucket->ractor_profiling_event_lists[i];
        offset = serialize_profiling_event_list(list, bucket_buffer, offset);
    }
    int final_offset = (offset > 1) ? offset - 2 : 1;
    sprintf(bucket_buffer + final_offset, "]\n"); /* Remove the last `,` */

    /* Output to a file */
    FILE *stream = fopen(outfile, "w");
    fputs(bucket_buffer, stream);
    fclose(stream);

    free(bucket_buffer);
}

static inline int push_call_stack(const int index, const bool system)
{
    profiling_event_list_t *list = select_profiling_event_list(system);

    refute_greater_or_equal(list->call_stack.top,
                            rb_event_profiling_config->max_call_stack_depth,
                            "Too deep call stack %d\n");
    int i = list->call_stack.top++;

    list->call_stack.event_indexes[i] = index;
    return index;
}

static inline profiling_event_t *pop_call_stack(const bool system)
{
    profiling_event_list_t *list = select_profiling_event_list(system);

    int i = --(list->call_stack.top);
    int index = list->call_stack.event_indexes[i];

    profiling_event_t *event = &(list->events[index]);
    return event;
}

static bool call_stack_empty(const bool system)
{
    profiling_event_list_t *list = select_profiling_event_list(system);
    return list->call_stack.top == 0;
}

/* Internal debugging facilities */
#if DEBUG_EVENT_PROFILING
static inline void debug_print_profling_event(const profiling_event_t *event)
{
    printf("file = %s\n"
           "function = %s\n"
           "line = %d\n"
           "ractor = %d\n"
           "id = %d\n"
           "pid = %d\n"
           "tid = %d\n"
           "phase = %c\n"
           "timestamp = %ld\n\n",
           event->file, event->function, event->line, event->ractor, event->id,
           event->pid, event->tid, profiling_event_phase_str[event->phase],
           event->timestamp);
}

static inline void
debug_print_profling_event_list(const profiling_event_list_t *list)
{
    int events = list->tail;
    for (int i = 0; i < events; i++)
    {
        debug_print_profling_event(&(list->events[i]));
    }
}

void debug_print_profling_event_bucket(void)
{
    debug_print_profling_event_list(
        rb_profiling_event_bucket->system_init_event_list);
    int ractors = rb_profiling_event_bucket->ractors;
    for (int i = 0; i < ractors; i++)
    {
        debug_print_profling_event_list(
            rb_profiling_event_bucket->ractor_profiling_event_lists[i]);
    }
}
#else
void debug_print_profling_event_bucket(void) {}
#endif

/* Public functions */

void ractor_init_profiling_event_list(rb_ractor_t *r)
{
    int ractor_id = r->pub.id;

    int ractors = rb_profiling_event_bucket->ractors;
    refute_greater_or_equal(ractors, rb_event_profiling_config->max_ractors,
                            "Too many Ractors. %d \n");
    RUBY_ATOMIC_INC(rb_profiling_event_bucket->ractors);

    profiling_event_list_t *list = init_profiling_event_list();

    r->event_profiling_storage = list;

    /* Save a pointer to serialize all events before MRI main ractor exiting */
    rb_profiling_event_bucket->ractor_profiling_event_lists[ractor_id - 1] =
        list;
}

int trace_profiling_event_begin(const char *file, const char *func,
                                const int line, const bool system)
{
    int                index = -1;
    profiling_event_t *event = get_a_profiling_event_slot(system, &index);

    /* Track this event */
    push_call_stack(index, system);

    int id = get_a_new_event_id(system);

    event->file = (char *)file;
    event->function = (char *)func;
    event->line = line;
    event->id = id;
    event->phase = PROFILING_EVENT_PHASE_BEGIN;
    event->pid = getpid();
    event->tid = gettid();
    event->snapshot_reason = NULL;
    event->timestamp = microsecond_timestamp();

    return id;
}

int trace_profiling_event_end(const char *file, const char *func,
                              const int line, const bool system)
{
    profiling_event_t *event = get_a_profiling_event_slot(system, NULL);

    profiling_event_t *begin = pop_call_stack(system);
    int                id = begin->id;

    event->file = (char *)file;
    event->function = (char *)func;
    event->line = line;
    event->id = id;
    event->phase = PROFILING_EVENT_PHASE_END;
    event->pid = begin->pid;
    event->tid = begin->tid;
    event->snapshot_reason = NULL;
    event->timestamp = microsecond_timestamp();

    return id;
}

void trace_profiling_event_exception(const bool system)
{
    while (!call_stack_empty(system))
    {
        profiling_event_t *event = get_a_profiling_event_slot(system, NULL);

        profiling_event_t *begin = pop_call_stack(system);

        event->file = begin->file;
        event->function = begin->function;
        event->line = -1;
        event->id = begin->id;
        event->phase = PROFILING_EVENT_PHASE_END;
        event->pid = begin->pid;
        event->tid = begin->tid;
        event->snapshot_reason = NULL;
        event->timestamp = microsecond_timestamp();
    }
}

int trace_profiling_event_snapshot(const char *file, const char *func,
                                   const int line, const char *reason,
                                   bool system)
{
    profiling_event_t *event = get_a_profiling_event_slot(system, NULL);
    int                id = get_a_new_event_id(system);

    event->file = (char *)file;
    event->function = (char *)func;
    event->line = line;
    event->id = id;
    event->phase = PROFILING_EVENT_PHASE_SNAPSHOT;
    event->pid = getpid();
    event->tid = gettid();
    event->snapshot_reason = (char *)reason;
    event->timestamp = microsecond_timestamp();

    return id;
}

event_profiling_config_t *setup_event_profiling(const int max_ractors,
                                                const int max_ractor_events,
                                                const int max_call_stack_depth)
{
    event_profiling_config_t *config =
        (event_profiling_config_t *)malloc(sizeof(event_profiling_config_t));
    config->max_ractors = max_ractors;
    config->max_ractor_events = max_ractor_events;
    config->max_call_stack_depth = max_call_stack_depth;

    rb_event_profiling_config = config;

    init_profiling_event_bucket();

    return config;
}

void finalize_event_profiling(const char *outfile)
{
    serialize_profiling_event_bucket(outfile);

    destroy_profiling_event_bucket();
    free(rb_event_profiling_config);

    rb_event_profiling_config = NULL;
    rb_profiling_event_bucket = NULL;
}

#endif /* USE_EVENT_PROFILING */
