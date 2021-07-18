#ifndef USE_EVENT_PROFILING
#define USE_EVENT_PROFILING 0
#endif

#ifndef DEBUG_EVENT_PROFILING
#define DEBUG_EVENT_PROFILING 0
#endif

#ifndef RUBY_EVENT_PROFILING_H
#define RUBY_EVENT_PROFILING_H 1

#if USE_EVENT_PROFILING

#include "ruby/internal/config.h"

#include "vm_core.h"

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if !defined(__GNUC__) && USE_EVENT_PROFILING
#error "USE_EVENT_PROFILING is not supported by other than __GNUC__"
#endif

/* To avoid using realloc, allocate large heap memory ahead of execution. */
typedef struct event_profiling_config
{
    int max_ractors;
    int max_ractor_events;
    int max_call_stack_depth;
} event_profiling_config_t;

typedef enum profiling_event_phase
{
    PROFILING_EVENT_PHASE_BEGIN,
    PROFILING_EVENT_PHASE_END,
    PROFILING_EVENT_PHASE_SNAPSHOT,
} profiling_event_phase_t;

typedef struct profiling_event
{
    /* Title: file:function(id) Args: line, ractor */
    char *file;
    char *function;
    int   line;
    int   ractor;
    int   id;

    pid_t pid;
    pid_t tid;

    profiling_event_phase_t phase;

    char *snapshot_reason;

    time_t timestamp;
} profiling_event_t;

/* Allocate an event list for each Ractor */
typedef struct profiling_event_list
{
    int last_event_id;
    int tail;

    struct
    {
        int  top;
        int *event_indexes;
    } call_stack;

    profiling_event_t *events;
} profiling_event_list_t;

typedef struct profiling_event_bucket
{
    profiling_event_list_t * system_init_event_list;
    profiling_event_list_t **ractor_profiling_event_lists;
    int                      ractors;
} profiling_event_bucket_t;

/* Global configuration */
extern event_profiling_config_t *rb_event_profiling_config;

/* Global bucket */
extern profiling_event_bucket_t *rb_profiling_event_bucket;

RUBY_SYMBOL_EXPORT_BEGIN
event_profiling_config_t *setup_event_profiling(const int max_ractors,
                                                const int max_ractor_events,
                                                const int max_call_stack_depth);
void                      finalize_event_profiling(const char *outfile);

int  trace_profiling_event_begin(const char *file, const char *func,
                                 const int line, const bool system);
int  trace_profiling_event_end(const char *file, const char *func,
                               const int line, const bool system);
void trace_profiling_event_exception(const bool system);
int  trace_profiling_event_snapshot(const char *file, const char *func,
                                    const int line, const char *reason,
                                    const bool system);
RUBY_SYMBOL_EXPORT_END

void ractor_init_profiling_event_list(rb_ractor_t *r);
void debug_print_profling_event_bucket();

#define PROFILING_EVENT_DEFAULT_FILE_NAME     __FILE__
#define PROFILING_EVENT_DEFAULT_FUNCTION_NAME __func__
#define PROFILING_EVENT_DEFAULT_LINE_NUMBER   __LINE__
#define RB_EVENT_PROFILING_DEFAULT_INFO       __FILE__, __func__, __LINE__

/* Public marcos */
#define RB_EVENT_PROFILING_BEGIN()                                             \
    trace_profiling_event_begin(RB_EVENT_PROFILING_DEFAULT_INFO, false)
#define RB_EVENT_PROFILING_END()                                               \
    trace_profiling_event_end(RB_EVENT_PROFILING_DEFAULT_INFO, false)
#define RB_EVENT_PROFILING_SNAPSHOT(reason)                                    \
    trace_profiling_event_snapshot(RB_EVENT_PROFILING_DEFAULT_INFO, reason,    \
                                   false)
#define RB_EVENT_PROFILING_EXCEPTION() trace_profiling_event_exception(false)

#define RB_SYSTEM_EVENT_PROFILING_BEGIN()                                      \
    trace_profiling_event_begin(RB_EVENT_PROFILING_DEFAULT_INFO, true)
#define RB_SYSTEM_EVENT_PROFILING_END()                                        \
    trace_profiling_event_end(RB_EVENT_PROFILING_DEFAULT_INFO, true)
#define RB_SYSTEM_EVENT_PROFILING_SNAPSHOT(reason)                             \
    trace_profiling_event_snapshot(RB_EVENT_PROFILING_DEFAULT_INFO, reason,    \
                                   true)
#define RB_SYSTEM_EVENT_PROFILING_EXCEPTION()                                  \
    trace_profiling_event_exception(true)

#define RB_EVENT_PROFILING_DEFAULT_MAX_RACTORS          (512)
#define RB_EVENT_PROFILING_DEFAULT_MAX_RACTOR_EVENTS    (8192 * 512)
#define RB_EVENT_PROFILING_DEFAULT_MAX_CALL_STACK_DEPTH (64)
#define RB_EVENT_PROFILING_DEFAULT_OUTFILE              "event_profiling_out.json"

#define RB_SETUP_EVENT_PROFILING(max_ractors, max_ractor_events,               \
                                 max_call_stack_depth)                         \
    setup_event_profiling(max_ractors, max_ractor_events, max_call_stack_depth)
#define RB_SETUP_EVENT_PROFILING_DEFAULT()                                     \
    setup_event_profiling(RB_EVENT_PROFILING_DEFAULT_MAX_RACTORS,              \
                          RB_EVENT_PROFILING_DEFAULT_MAX_RACTOR_EVENTS,        \
                          RB_EVENT_PROFILING_DEFAULT_MAX_CALL_STACK_DEPTH)
#define RB_FINALIZE_EVENT_PROFILING(outfile) finalize_event_profiling(outfile)
#define RB_FINALIZE_EVENT_PROFILING_DEFAULT()                                  \
    finalize_event_profiling(RB_EVENT_PROFILING_DEFAULT_OUTFILE)

#else

#define RB_EVENT_PROFILING_BEGIN()
#define RB_EVENT_PROFILING_END()
#define RB_EVENT_PROFILING_SNAPSHOT(reason)
#define RB_EVENT_PROFILING_EXCEPTION()
#define RB_SYSTEM_EVENT_PROFILING_BEGIN()
#define RB_SYSTEM_EVENT_PROFILING_END()
#define RB_SYSTEM_EVENT_PROFILING_SNAPSHOT(reason)
#define RB_SYSTEM_EVENT_PROFILING_EXCEPTION()
#define RB_SETUP_EVENT_PROFILING_DEFAULT()
#define RB_FINALIZE_EVENT_PROFILING_DEFAULT()

#endif /* USE_EVENT_PROFILING */

#endif /* RUBY_EVENT_PROFILING_H */
