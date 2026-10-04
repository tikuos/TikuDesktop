/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * circuit_alloc.h - the allocator test_circuit_faults counts and fails,
 * put in place of the C library's in its own copies of the circuit
 * importers and the XML reader, each built with -include of this file.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CIRCUIT_TEST_ALLOC_H_
#define CIRCUIT_TEST_ALLOC_H_

#include <stdlib.h>
#include <string.h>

/** @brief malloc, as test_circuit_faults counts and fails it. */
void *circuit_test_malloc(size_t bytes);

/** @brief calloc, counted and failed the same way. */
void *circuit_test_calloc(size_t n, size_t size);

/** @brief realloc, counted and failed the same way. */
void *circuit_test_realloc(void *p, size_t bytes);

/** @brief strdup, counted and failed the same way. */
char *circuit_test_strdup(const char *s);

/** @brief free, for blocks the four above gave. */
void circuit_test_free(void *p);

#define malloc  circuit_test_malloc
#define calloc  circuit_test_calloc
#define realloc circuit_test_realloc
#define strdup  circuit_test_strdup
#define free    circuit_test_free

#endif /* CIRCUIT_TEST_ALLOC_H_ */
