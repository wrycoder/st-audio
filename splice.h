#include <stdint.h>
#include <math.h>
#include <windows.h>
#include "sox.h"

/* Define the format specifier to use for uint64_t values. */
#ifndef PRIu64 /* Maybe <inttypes.h> already defined this. */
#if defined(_MSC_VER) || defined(__MINGW32__) /* Older versions of msvcrt.dll don't recognize %llu. */
#define PRIu64 "I64u"
#elif ULONG_MAX==0xffffffffffffffff
#define PRIu64 "lu"
#else
#define PRIu64 "llu"
#endif
#endif /* PRIu64 */

/* Define the format specifier to use for size_t values.
 * Example: printf("Sizeof(x) = %" PRIuPTR " bytes", sizeof(x)); */
#ifndef PRIuPTR /* Maybe <inttypes.h> already defined this. */
#if defined(_MSC_VER) || defined(__MINGW32__) /* older versions of msvcrt.dll don't recognize %zu. */
#define PRIuPTR "Iu"
#else
#define PRIuPTR "zu"
#endif
#endif /* PRIuPTR */

#define SOX_LIB_ERROR     399
#define ST_ERROR          7734
#define USER_ERROR        7735
#define linear_to_dB(x) (log10(x) * 20)
#define DEFAULT_SILENCE_THRESHOLD ".041"
#define DEFAULT_NOISE_DURATION "00:00:00.2"
#define DEFAULT_OUTPUT_FILENAME "spliced-audio.wav"
#define MAXIMUM_SPLICES 50
#define MAXIMUM_SAMPLES (size_t)2048 /* Typical operating system I/O buffer size */

static sox_signalinfo_t st_default_signalinfo = {
  44100,    /* samples per second */
  2,        /* channels */
  16,       /* bits per sample */
  0,        /* length, not relevant for default */
  NULL      /* Effects headroom multiplier */
};

void show_stats(sox_format_t * in);
void show_name_and_runtime(sox_format_t * in);
wchar_t const * str_time(double seconds);
void report_error(HWND hwnd, int errcode, char* file, int line_number);
void report_current_action(HWND, const char*);
const char* convert_pwstr_to_const_char(PWSTR wideString);
void trim_silence(wchar_t * filename, char * duration, char * threshold);
void splice();
static int sox_quit_called;
int cleanup();
