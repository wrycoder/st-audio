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

#define SOX_LIB_ERROR 399
#define linear_to_dB(x) (log10(x) * 20)
#define DEFAULT_SILENCE_THRESHOLD ".041"
#define DEFAULT_NOISE_DURATION "00:00:00.2"
#define DEFAULT_OUTPUT_FILENAME "spliced-audio.wav"
#define MAXIMUM_SPLICES 50

void show_stats(sox_format_t * in);
void show_name_and_runtime(sox_format_t * in);
wchar_t const * str_time(double seconds);
void report_error(HWND hwnd, int errcode, int line_number);
const char* ConvertPWSTRToConstChar(PWSTR wideString);
void trim_silence(wchar_t * filename, char * duration, char * threshold);
void splice();
static int sox_quit_called;
int cleanup();
