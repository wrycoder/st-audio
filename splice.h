#include <stdint.h>
#include <math.h>

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

void show_stats(sox_format_t * in);
