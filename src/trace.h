/*
 * Milestone tracing.
 *
 * The harness cannot ask the port "did it work?"; it can only read what the
 * port prints. Every observable milestone (module loaded, onCreate returned,
 * gl ready, frame, non-black framebuffer) is announced through trace() so the
 * verification script asserts on facts instead of on the absence of a crash.
 *
 * Enabled by LOADER_TRACE=1 so a released port stays quiet.
 */
#ifndef KATAMARI_TRACE_H
#define KATAMARI_TRACE_H

#if defined(__GNUC__) || defined(__clang__)
#define OPEN_CITADEL_PRINTF_FORMAT(format_index, arg_index) \
    __attribute__((format(printf, format_index, arg_index)))
#else
#define OPEN_CITADEL_PRINTF_FORMAT(format_index, arg_index)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Prints "TRACE: <msg>" to stderr, unbuffered, when LOADER_TRACE is set. */
void trace(const char *fmt, ...) OPEN_CITADEL_PRINTF_FORMAT(1, 2);

/* Always printed, tracing on or off: the user needs to see why it refused. */
void fatal(const char *fmt, ...) OPEN_CITADEL_PRINTF_FORMAT(1, 2);

#ifdef __cplusplus
}
#endif

#undef OPEN_CITADEL_PRINTF_FORMAT

#endif
