/**********************************************************************

  main.c -

  $Author$
  created at: Fri Aug 19 13:19:58 JST 1994

  Copyright (C) 1993-2007 Yukihiro Matsumoto

**********************************************************************/

/*!
 * \mainpage Developers' documentation for Ruby
 *
 * This documentation is produced by applying Doxygen to
 * <a href="https://github.com/ruby/ruby">Ruby's source code</a>.
 * It is still under construction (and even not well-maintained).
 * If you are familiar with Ruby's source code, please improve the doc.
 */
#undef RUBY_EXPORT
#include "ruby.h"
#include "vm_debug.h"
#include "event_profiling.h"
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#if RUBY_DEVEL && !defined RUBY_DEBUG_ENV
#define RUBY_DEBUG_ENV 1
#endif
#if defined RUBY_DEBUG_ENV && !RUBY_DEBUG_ENV
#undef RUBY_DEBUG_ENV
#endif


int main(int argc, char **argv)
{
#ifdef RUBY_DEBUG_ENV
    ruby_set_debug_option(getenv("RUBY_DEBUG"));
#endif
#ifdef HAVE_LOCALE_H
    setlocale(LC_CTYPE, "");
#endif

#if USE_EVENT_PROFILING
    setup_event_profiling(PROFILING_EVENT_DEFAULT_MAX_RACTORS, PROFILING_EVENT_DEFAULT_MAX_RACTOR_EVENTS);
    int id = trace_system_init_profiling_event_begin();
#endif

    int result = 1;
    ruby_sysinit(&argc, &argv);
    {
        RUBY_INIT_STACK;
        ruby_init();
        result = ruby_run_node(ruby_options(argc, argv));
    }

#if USE_EVENT_PROFILING
    trace_system_init_profiling_event_end(id);
    finalize_event_profiling(PROFILING_EVENT_DEFAULT_OUTFILE);
#endif

    return result;
}
