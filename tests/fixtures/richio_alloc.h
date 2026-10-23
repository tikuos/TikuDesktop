/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * richio_alloc.h - isolated native codec allocation and output faults.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_TEST_RICHIO_ALLOC_H_
#define TIKU_TEST_RICHIO_ALLOC_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Count and optionally fail malloc. */
void *richio_test_malloc(size_t n);
/** @brief Count and optionally fail calloc. */
void *richio_test_calloc(size_t n, size_t size);
/** @brief Count and optionally fail realloc. */
void *richio_test_realloc(void *old, size_t n);
/** @brief Duplicate text through the counted allocator. */
char *richio_test_strdup(const char *text);
/** @brief Release a counted allocation. */
void richio_test_free(void *p);
/** @brief Emit a deliberately short write at a selected output call. */
size_t richio_test_fwrite(const void *p, size_t size, size_t n, FILE *out);

#ifndef TIKU_RICHIO_ALLOC_IMPLEMENTATION
#define malloc richio_test_malloc
#define calloc richio_test_calloc
#define realloc richio_test_realloc
#define strdup richio_test_strdup
#define free richio_test_free
#define fwrite richio_test_fwrite
#endif
#endif
