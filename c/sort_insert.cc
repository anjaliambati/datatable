//------------------------------------------------------------------------------
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// © H2O.ai 2018
//------------------------------------------------------------------------------
// Insertion sort functions
//
// Insert Sort algorithm has O(n²) complexity, and therefore should only be
// used for small arrays.
//
// See also:
//   - https://en.wikipedia.org/wiki/Insertion_sort
//   - datatable/microbench/insertsort
//------------------------------------------------------------------------------
#include "sort.h"
#include <cstdlib>  // std::abs
#include <cstring>  // std::memcpy



//==============================================================================
// Helper functions
//==============================================================================

/**
 * Compare two strings a and b, each given as a pair of offsets `off0` ..
 * `off1` into the common character buffer `strdata`. If `off1` is negative,
 * then that string is an NA string. If `off0 >= off1`, then the string is
 * considered empty.
 * Return 0 if strings are equal, 1 if a < b, or -1 if a > b. An NA string
 * compares equal to another NA string, and less than any non-NA string. An
 * empty string compares greater than NA, but less than any non-empty string.
 *
 * Type T can be either `int32_t` or `int64_t`.
 */
template <typename T>
int compare_offstrings(
    const uint8_t* strdata, T aoff0, T aoff1, T boff0, T boff1)
{
  // Handle NAs and empty strings
  if (boff1 < 0) return aoff1 < 0? 0 : -1;
  if (aoff1 < 0) return 1;
  T lena = aoff1 - aoff0;
  T lenb = boff1 - boff0;
  if (lenb <= 0) return lena <= 0? 0 : -1;
  if (lena <= 0) return 1;

  for (T t = 0; t < lena; ++t) {
    if (t == lenb) return -1;  // b is shorter than a
    uint8_t ca = strdata[aoff0 + t];
    uint8_t cb = strdata[boff0 + t];
    if (ca == cb) continue;
    return (ca < cb)? 1 : -1;  // a and b differ at character t
  }
  return lena == lenb? 0 : 1;
}



//==============================================================================
// Insertion sort of arrays with primitive C types
//==============================================================================

/**
 * insert_sort_values<T, V>(x, o, n)
 *
 * Sorts values in `x` and writes the ordering into `o`. Array `o` must be
 * pre-allocated and have the same length `n` as `x`.
 *
 * For example, if `x` is {5, 2, -1, 7, 2}, then this function will leave `x`
 * unmodified and write {2, 1, 4, 0, 3} into `o`.
 *
 * The template function is parametrized by
 *   T: the type of the elements in the "keys" array `x`. This type should be
 *      comparable using standard operator "<".
 *   V: type of elements in the "output" array `o`. Since output array is
 *      usually an ordering, this type is either `int32_t` or `int64_t`.
 */
template <typename T, typename V>
void insert_sort_values(const T* x, V* o, int n, GroupGatherer& gg)
{
  o[0] = 0;
  for (int i = 1; i < n; ++i) {
    T xival = x[i];
    int j = i;
    while (j && xival < x[o[j - 1]]) {
      o[j] = o[j - 1];
      j--;
    }
    o[j] = static_cast<V>(i);
  }
  if (gg) {
    gg.from_data(x, o, static_cast<size_t>(n));
  }
}


/**
 * insert_sort_keys<T, V>(x, o, tmp, n)
 *
 * Sorts array `o` according to the values in `x` (both arrays must have the
 * same length `n`). Additionally, array `tmp` of length `n` must be provided,
 * which will be used as "scratch space" during sorting.
 *
 * For example, if `x` is {5, 2, -1, 7, 2}, then this function will leave `x`
 * unmodified but reorder the elements of `o` into {o[2], o[1], o[4], o[0],
 * o[3]}.
 *
 * The template function is parametrized by
 *   T: the type of the elements in the "keys" array `x`. This type should be
 *      comparable using standard operator "<".
 *   V: type of elements in the "output" array `o`. Since output array is
 *      usually an ordering, this type is either `int32_t` or `int64_t`.
 */
template <typename T, typename V>
void insert_sort_keys(const T* x, V* o, V* tmp, int n, GroupGatherer& gg)
{
  insert_sort_values(x, tmp, n, gg);
  for (int i = 0; i < n; ++i) {
    tmp[i] = o[tmp[i]];
  }
  std::memcpy(o, tmp, static_cast<size_t>(n) * sizeof(V));
}



//==============================================================================
// Insertion sort of string arrays
//==============================================================================

// For the string sorting procedure `insert_sort_?_str` the argument `x` is
// replaced with a triple `strdata`, `stroffs`, `strstart`. The first is a
// pointer to a memory buffer containing the string data. The `stroffs` is an
// array of offsets within `strdata` (each `stroffs[i]` gives the end of
// string `i`; the beginning of the first string is at offset `stroffs[-1]`).
// Finally, parameter `strstart` instructs to compare the strings starting from
// that byte.
//
template <typename T, typename V>
void insert_sort_keys_str(
    const uint8_t* strdata, const T* stroffs, T strstart, V* o, V* tmp, int n,
    GroupGatherer& gg)
{
  int j;
  tmp[0] = 0;
  for (int i = 1; i < n; ++i) {
    T off0i = std::abs(stroffs[o[i]-1]) + strstart;
    T off1i = stroffs[o[i]];
    for (j = i; j > 0; --j) {
      V k = tmp[j - 1];
      T off0k = std::abs(stroffs[o[k]-1]) + strstart;
      T off1k = stroffs[o[k]];
      int cmp = compare_offstrings(strdata, off0i, off1i, off0k, off1k);
      if (cmp != 1) break;
      tmp[j] = tmp[j-1];
    }
    tmp[j] = static_cast<V>(i);
  }
  for (int i = 0; i < n; ++i) {
    tmp[i] = o[tmp[i]];
  }
  if (gg) {
    gg.from_data(strdata, stroffs, strstart, tmp, static_cast<size_t>(n));
  }
  std::memcpy(o, tmp, static_cast<size_t>(n) * sizeof(V));
}


template <typename T, typename V>
void insert_sort_values_str(
    const uint8_t* strdata, const T* stroffs, T strstart, V* o, int n,
    GroupGatherer& gg)
{
  int j;
  o[0] = 0;
  for (int i = 1; i < n; ++i) {
    T off0i = std::abs(stroffs[i-1]) + strstart;
    T off1i = stroffs[i];
    for (j = i; j > 0; j--) {
      V k = o[j - 1];
      T off0k = std::abs(stroffs[k-1]) + strstart;
      T off1k = stroffs[k];
      int cmp = compare_offstrings(strdata, off0i, off1i, off0k, off1k);
      if (cmp != 1) break;
      o[j] = o[j-1];
    }
    o[j] = static_cast<V>(i);
  }
  if (gg) {
    gg.from_data(strdata, stroffs, strstart, o, static_cast<size_t>(n));
  }
}



//==============================================================================
// Explicitly instantate template functions
//==============================================================================

template void insert_sort_keys(const uint8_t*,  int32_t*, int32_t*, int, GroupGatherer&);
template void insert_sort_keys(const uint16_t*, int32_t*, int32_t*, int, GroupGatherer&);
template void insert_sort_keys(const uint32_t*, int32_t*, int32_t*, int, GroupGatherer&);
template void insert_sort_keys(const uint64_t*, int32_t*, int32_t*, int, GroupGatherer&);

template void insert_sort_values(const uint8_t*,  int32_t*, int, GroupGatherer&);
template void insert_sort_values(const uint16_t*, int32_t*, int, GroupGatherer&);
template void insert_sort_values(const uint32_t*, int32_t*, int, GroupGatherer&);
template void insert_sort_values(const uint64_t*, int32_t*, int, GroupGatherer&);

template void insert_sort_keys_str(const uint8_t*, const int32_t*, int32_t, int32_t*, int32_t*, int, GroupGatherer&);
template void insert_sort_values_str(const uint8_t*, const int32_t*, int32_t, int32_t*, int, GroupGatherer&);
template int compare_offstrings(const uint8_t*, int32_t, int32_t, int32_t, int32_t);
