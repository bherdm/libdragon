/**
 * @file assert.h
 * @brief newlib's assert, with libdragon's own texts in .assert_texts
 * @ingroup lowlevel
 *
 * Found before newlib's in libdragon's own build only: assert() is newlib's,
 * but its file and expression go where assertf keeps its texts (see
 * __ASSERT_TEXT in debug.h), so a program may leave them in ROM.
 */
#include_next <assert.h>
#include "debug.h"

#undef assert
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : __assert_func(__ASSERT_TEXT(__FILE__), __LINE__, __ASSERT_FUNC, __ASSERT_TEXT(#e)))
#endif
