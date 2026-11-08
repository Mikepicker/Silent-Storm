/*  android/log.h -- macOS stand-in.  The platform layer logs through
 *  __android_log_print; on a desktop host that is just stderr.  */
#ifndef A5_MACOS_ANDROID_LOG_H
#define A5_MACOS_ANDROID_LOG_H
#include <stdio.h>
#include <stdarg.h>

enum {
    ANDROID_LOG_UNKNOWN = 0, ANDROID_LOG_DEFAULT, ANDROID_LOG_VERBOSE,
    ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN,
    ANDROID_LOG_ERROR, ANDROID_LOG_FATAL, ANDROID_LOG_SILENT
};

#ifdef __cplusplus
extern "C" {
#endif
static inline int __android_log_print( int nPrio, const char *pszTag,
                                       const char *pszFmt, ... )
{
    va_list ap;
    va_start( ap, pszFmt );
    fprintf( stderr, "[%s] ", pszTag ? pszTag : "a5" );
    vfprintf( stderr, pszFmt, ap );
    fputc( '\n', stderr );
    va_end( ap );
    (void)nPrio;
    return 0;
}
#ifdef __cplusplus
}
#endif
#endif
