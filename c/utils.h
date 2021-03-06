//------------------------------------------------------------------------------
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// © H2O.ai 2018
//------------------------------------------------------------------------------


//==============================================================================
//  Collection of macros / helper functions
//==============================================================================
#ifndef dt_UTILS_H
#define dt_UTILS_H
#include <Python.h>
#include <stddef.h>
#include <stdio.h>     // vsnprintf
#include <stdint.h>
#include <stdio.h>     // vsnprintf
#include <errno.h>     // errno
#include <string.h>    // strerr
#include <memory>      // std::unique_ptr
#include <string>
#include "utils/exceptions.h"


// On Windows variables of type `size_t` cannot be printed with "%zu" in the
// `snprintf()` function. For those variables we will cast them into
// `unsigned long long int` before printing; and this #define makes it simpler.
#define llu   unsigned long long int


int64_t min(int64_t a, int64_t b);
int64_t max(int64_t a, int64_t b);
float max_f4(float a, float b);
double wallclock(void);
const char* filesize_to_str(size_t filesize);
const char* humanize_number(size_t num);

inline std::string operator "" _s(const char* str, size_t len) {
  return std::string(str, len);
}



//==============================================================================
// Pointer macros
//==============================================================================

// Some macro hackery based on
// https://github.com/pfultz2/Cloak/wiki/C-Preprocessor-tricks,-tips,-and-idioms
#define zPRIMITIVE_CAT(a, ...) a ## __VA_ARGS__
#define zCAT(a, ...) zPRIMITIVE_CAT(a, __VA_ARGS__)
#define zIIF_0(t, ...) __VA_ARGS__
#define zIIF_1(t, ...) t
#define zCHECK_N(x, n, ...) n
#define zCHECK(...) zCHECK_N(__VA_ARGS__, 0,)
#define zIIF(c) zPRIMITIVE_CAT(zIIF_, c)
#define zTEST1_void x,1,
#define zTEST2_void char
#define zTEST3_ ~,1,
// This expands into `x,1,` if T is void, into `x,1,*` if T is void*, or into
// `zTEST1_<T>` otherwise.
#define zEXPANDT1(T) zCAT(zTEST1_, T)
// Similarly, this expands into `char` if T is void, into `char*` if T is void*,
// or into `zTEST2_<T>` otherwise
#define zEXPANDT2(T) zCAT(zTEST2_, T)
// Expands to `1` if T is void or void*, or to `0` otherwise
#define zIS_T_VOID(T) zCHECK(zEXPANDT1(T))
// Finally, this expands into `sizeof(char)` if T is void, into `sizeof(char*)`
// if T is void*, and into `sizeof(<T>)` in all other cases.
#define SIZEOF(T) zIIF(zIS_T_VOID(T))(sizeof(zEXPANDT2(T)), sizeof(T))

// This macro expands to `t` if symbol `x` is defined and equal to 1, and to
// `f` otherwise.
#define IFDEF(x, t, f) zIIF(zCHECK(zPRIMITIVE_CAT(zTEST3_, x)))(t, f)
#define WHEN(x, t) IFDEF(x, t, )



#define APPLY(macro, arg) macro(arg)
#define STRINGIFY_(L) #L
#define STRINGIFY(x) APPLY(STRINGIFY_, x)

/**
 * Use this macro to malloc a memory region and assign it to a variable. For
 * example, to malloc an array of 1000 integers:
 *
 *     int32_t *arr;
 *     dtmalloc(arr, int32_t, 1000);
 *
 * This will also work correctly for allocating a `void*` pointer:
 *
 *     void *buf;
 *     dtmalloc(buf, void, 1024 * 1024);
 *
 * The macro checks that the allocation was successful, and if not it executes
 * `return NULL`. Thus, it can only be used within a function that returns a
 * pointer. The `_g` version of this macro instead executes `goto fail`, where
 * any special cleanup may be performed.
 **/
#define dtmalloc(ptr, T, n) do {                                               \
    ptr = (T*) _dt_malloc((size_t)(n) * SIZEOF(T));                            \
    if (ptr == null && n) return null;                                         \
} while(0)

#define dtmalloc_g(ptr, T, n) do {                                             \
    ptr = (T*) _dt_malloc((size_t)(n) * SIZEOF(T));                            \
    if (ptr == null && n) goto fail;                                           \
} while(0)



/**
 * Similar to `dtmalloc(ptr, T, n)`, except that it declares the variable `ptr`
 * in addition to malloc-ing it.
 */
#define dtdeclmalloc(ptr, T, n)                                                \
    T* ptr = (T*) _dt_malloc((size_t)(n) * SIZEOF(T));                         \
    if (ptr == null && n) return null;



/**
 * This macro is a replacement for standard `realloc` in much the same way as
 * `dtmalloc` is a replacement for system `malloc`:
 *
 *     float *data = NULL;
 *     dtrealloc(data, float, 100);
 *
 * The macro checks that the allocation was successful, and if not it executes
 * `return NULL`. Thus, it can only be used within a function that returns a
 * pointer. The `_g` version of this macro instead executes `goto fail`, where
 * any special cleanup may be performed.
 */
#define dtrealloc(ptr, T, n) do {                                              \
    ptr = (T*) _dt_realloc(ptr, (size_t)(n) * SIZEOF(T));                      \
    if (ptr == null && n) return null;                                         \
} while(0)

#define dtrealloc_g(ptr, T, n) do {                                            \
    ptr = (T*) _dt_realloc(ptr, (size_t)(n) * SIZEOF(T));                      \
    if (ptr == null && n) goto fail;                                           \
} while(0)

#define dtrealloc_x(ptr, T, n) do {                                            \
    ptr = (T*) _dt_realloc(ptr, (size_t)(n) * SIZEOF(T));                      \
    if (ptr == null && n) throw MemoryError();                                 \
} while(0)



/**
 * Replacement for `calloc`.
 */
#define dtcalloc(ptr, T, n) do {                                               \
    ptr = (T*) _dt_calloc((size_t)(n), SIZEOF(T));                             \
    if (ptr == null && n) return null;                                         \
} while(0)

#define dtcalloc_g(ptr, T, n) do {                                             \
    ptr = (T*) _dt_calloc((size_t)(n), SIZEOF(T));                             \
    if (ptr == null && n) goto fail;                                           \
} while(0)



/**
 * Macro to free the memory previously allocated with `dtmalloc` / `dtrealloc`.
 * This frees the memory and sets the pointer to NULL, for safety.
 **/
#define dtfree(ptr) while (ptr) {                                              \
    /*printf("  dtfree(%p) at %s line %d\n", (void*)ptr, __FILE__, __LINE__);*/\
    _dt_free(ptr);                                                             \
    ptr = null;                                                                \
}



/**
 * Macro for safe pointer arithmetic on `void*` pointers. Normally we'd want to
 * say simply `ptr + n`, but this is not in standard C, and produces a warning
 * on Clang compiler (and in C++ such operation is straight prohibited...).
 */
#define add_ptr(ptr, n)       ((void*)((char*)(ptr) + (n)))
#define add_constptr(ptr, n)  ((const void*)((const char*)(ptr) + (n)))


//---- Private -------------------------
// The _dt_xxx functions are external and should be defined in py_utils.c
void* _dt_calloc(size_t n, size_t size);
void* _dt_malloc(size_t n);
void* _dt_realloc(void *ptr, size_t n);
void  _dt_free(void *ptr);
#ifdef __cplusplus
    #define null NULL
#else
    #define null NULL
    #define bool _Bool
#endif


//==============================================================================
// Error handling
//==============================================================================

/**
 * Raise a RuntimeError with the provided message
 */
#define dterrr(format, ...) do {                                               \
    _dt_err_r(format "\nat %s#L%d", __VA_ARGS__, __FILE__, __LINE__);          \
    return NULL;                                                               \
} while (0)

#define dterrr0(message) do {                                                  \
    _dt_err_r(message "\nat %s#L%d", __FILE__, __LINE__);                      \
    return NULL;                                                               \
} while (0)

/**
 * Raise a ValueError with the provided message
 */
#define dterrv(format, ...) do {                                               \
    _dt_err_v(format "\nat %s#L%d", __VA_ARGS__, __FILE__, __LINE__);          \
    return NULL;                                                               \
} while (0)

/**
 * Raise a RuntimeError, but also include in the message current `errno`.
 */
#define dterre(format, ...) do {                                               \
    _dt_err_r(format ": Error %d %s\nat %s#L%d",                               \
              __VA_ARGS__, errno, strerror(errno), __FILE__, __LINE__);        \
    return NULL;                                                               \
} while (0)

#define dterre0(message) do {                                                  \
    _dt_err_r(message ": Error %d %s\nat %s#L%d",                              \
              errno, strerror(errno), __FILE__, __LINE__);                     \
    return NULL;                                                               \
} while (0)


// External functions -- should be defined in py_utils.c
// These functions merely set the error message, and nothing else.
void _dt_err_r(const char *format, ...) __attribute__ ((format(printf, 1, 2)));
void _dt_err_v(const char *format, ...) __attribute__ ((format(printf, 1, 2)));



//==============================================================================
// Binary arithmetics
//==============================================================================

namespace dt {
template <typename T> int nlz(T x);  // Number of leading zeros

extern template int nlz(uint64_t);
extern template int nlz(uint32_t);
extern template int nlz(uint16_t);
extern template int nlz(uint8_t);
};


//==============================================================================
// Other
//==============================================================================

/**
 * Mark a function parameter as deliberately unused.
 **/
#define UNUSED(x)  __attribute__((unused)) x

#define zMAKE_PRAGMA(x) _Pragma(#x);

#define IGNORE_WARNING(name) \
    zMAKE_PRAGMA(GCC diagnostic push) \
    zMAKE_PRAGMA(GCC diagnostic ignored #name)

#define RESTORE_WARNINGS() \
    zMAKE_PRAGMA(GCC diagnostic pop)


void set_value(void * __restrict__ ptr, const void * __restrict__ value,
               size_t sz, size_t count);

#endif
