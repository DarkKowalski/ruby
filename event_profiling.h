#ifndef USE_EVENT_PROFILING
#define USE_EVENT_PROFILING 1
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
    /* Tracing */
    int max_ractors;
    int max_ractor_events;
} event_profiling_config_t;

typedef enum profiling_event_phase
{
    PROFILING_EVENT_PHASE_BEGIN,
    PROFILING_EVENT_PHASE_END,
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

    time_t timestamp;
} profiling_event_t;

#define PROFILING_EVENT_DEFAULT_FILE_NAME     __FILE__
#define PROFILING_EVENT_DEFAULT_FUNCTION_NAME __func__
#define PROFILING_EVENT_DEFAULT_LINE_NUMBER   __LINE__

/* Allocate an event list for each Ractor */
typedef struct profiling_event_list
{
    int                last_event_id;
    int                tail;
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
                                                const int max_ractor_events);
void                      finalize_event_profiling(const char *outfile);
int trace_profiling_event(const char *file, const char *func, const int line,
                          const int                     event_id,
                          const profiling_event_phase_t phase,
                          const bool                    system_init);
RUBY_SYMBOL_EXPORT_END

void ractor_init_profiling_event_list(rb_ractor_t *r);
void debug_print_profling_event_bucket();

#define PROFILING_EVENT_DEFAULT_MAX_RACTORS       (512)
#define PROFILING_EVENT_DEFAULT_MAX_RACTOR_EVENTS (8192 * 512)
#define PROFILING_EVENT_DEFAULT_OUTFILE           "event_profiling_out.json"

#define trace_ractor_profiling_event(event_id, phase)                          \
    trace_profiling_event(PROFILING_EVENT_DEFAULT_FILE_NAME,                   \
                          PROFILING_EVENT_DEFAULT_FUNCTION_NAME,               \
                          PROFILING_EVENT_DEFAULT_LINE_NUMBER, event_id,       \
                          phase, false)

#define trace_system_init_profiling_event(event_id, phase)                     \
    trace_profiling_event(PROFILING_EVENT_DEFAULT_FILE_NAME,                   \
                          PROFILING_EVENT_DEFAULT_FUNCTION_NAME,               \
                          PROFILING_EVENT_DEFAULT_LINE_NUMBER, event_id,       \
                          phase, true)

#define NEW_PROFILING_EVENT_ID (-1) /* Return a new event_id */

#define trace_ractor_profiling_event_begin()                                   \
    trace_ractor_profiling_event(NEW_PROFILING_EVENT_ID,                       \
                                 PROFILING_EVENT_PHASE_BEGIN)
#define trace_ractor_profiling_event_end(event_id)                             \
    trace_ractor_profiling_event(event_id, PROFILING_EVENT_PHASE_END)

#define trace_system_init_profiling_event_begin()                              \
    trace_system_init_profiling_event(NEW_PROFILING_EVENT_ID,                  \
                                      PROFILING_EVENT_PHASE_BEGIN)
#define trace_system_init_profiling_event_end(event_id)                        \
    trace_system_init_profiling_event(event_id, PROFILING_EVENT_PHASE_END)

#endif /* USE_EVENT_PROFILING */

#endif /* RUBY_EVENT_PROFILING_H */
