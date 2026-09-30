/*
 * TikuOS kits -- the foundation TikuDesktop and TikuTracker
 * both stand on.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_editdoc_storage.c - the gap buffers and the line partitions, held
 * to the upstream cases they were adapted from, each run on a fresh
 * fixture, and to what the C adaptation adds: explicit room, no
 * allocation after it, storage kept when emptied.
 *
 * Cases adapted from Scintilla 5.6.7 test/unit/testSplitVector.cxx and
 * testPartitioning.cxx, Copyright Neil Hodgson; see
 * kits/editor/LICENSE.Scintilla and kits/editor/UPSTREAM.md.
 *
 * SPDX-License-Identifier: Apache-2.0 AND LicenseRef-Scintilla
 */

#include <stdio.h>
#include <string.h>

#include "tiku_editdoc_private.h"

static int fails;

/** @brief Count a failure and say which case it belongs to. */
static void
check(int cond, const char *row, const char *what)
{
    if (!cond) {
        printf("  [FAIL] %-28s %s\n", row, what);
        fails++;
    }
}

/** @brief A fresh vector, as each upstream SECTION starts from one. */
static tiku_ed_starts_t
fresh(void)
{
    tiku_ed_starts_t v;

    tiku_ed_starts_init(&v);
    return v;
}

/** @brief Insert one value, making room first as the C adaptation asks. */
static void
ins(tiku_ed_starts_t *v, ptrdiff_t pos, int64_t value)
{
    check(tiku_ed_starts_reserve(v, 1), "room", "reserve one");
    tiku_ed_starts_insert(v, pos, value);
}

/** @brief Upstream InsertValue: @p n copies of @p value at @p pos. */
static void
fill(tiku_ed_starts_t *v, ptrdiff_t pos, ptrdiff_t n, int64_t value)
{
    int64_t values[128];
    ptrdiff_t i;

    for (i = 0; i < n; i++)
        values[i] = value;
    check(tiku_ed_starts_reserve(v, n), "room", "reserve many");
    tiku_ed_starts_insert_many(v, pos, values, n);
}

/** @brief Whether @p v holds exactly @p expect. */
static int
holds(const tiku_ed_starts_t *v, const int64_t *expect, ptrdiff_t n)
{
    ptrdiff_t i;

    if (v->length != n)
        return 0;
    for (i = 0; i < n; i++)
        if (tiku_ed_starts_at(v, i) != expect[i])
            return 0;
    return 1;
}

static const int64_t test_array[] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
#define TEST_ARRAY_LEN 10

/** @brief testSplitVector.cxx, SplitVector, one SECTION per block. */
static void
split_vector(void)
{
    tiku_ed_starts_t sv;
    int64_t got[16];
    ptrdiff_t i;

    sv = fresh();
    check(sv.length == 0, "SplitVector/IsEmptyInitially", "empty");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 10, 0);
    ins(&sv, 5, 3);
    check(sv.length == 11, "SplitVector/InsertOne", "length 11");
    for (i = 0; i < sv.length; i++)
        check(tiku_ed_starts_at(&sv, i) == (i == 5 ? 3 : 0),
              "SplitVector/InsertOne", "value at each index");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 10, 0);
    check(sv.length == 10, "SplitVector/Insertion", "length 10");
    for (i = 0; i < sv.length; i++)
        check(tiku_ed_starts_at(&sv, i) == 0, "SplitVector/Insertion",
              "zero at each index");
    tiku_ed_starts_free(&sv);

    {
        /* InsertionString, adapted: the same insertions and deletion, of
         * distinct numbers instead of strings, against a plain array. */
        static const struct { int at; int64_t value; } steps[] = {
            {0, 1}, {0, 2}, {0, 3}, {1, 4}, {0, 5}, {1, 6}, {0, 7}, {1, 8},
            {-4, 0}, {0, 9}, {1, 10}, {0, 11}, {1, 12}, {0, 13}, {1, 14}
        };
        int64_t model[32];
        ptrdiff_t n = 0;
        size_t s;

        sv = fresh();
        for (s = 0; s < sizeof steps / sizeof *steps; s++) {
            if (steps[s].at < 0) {
                ptrdiff_t d = -steps[s].at;

                tiku_ed_starts_delete(&sv, d, 1);
                memmove(model + d, model + d + 1,
                        (size_t)(n - d - 1) * sizeof *model);
                n--;
            } else {
                ins(&sv, steps[s].at, steps[s].value);
                memmove(model + steps[s].at + 1, model + steps[s].at,
                        (size_t)(n - steps[s].at) * sizeof *model);
                model[steps[s].at] = steps[s].value;
                n++;
            }
            check(holds(&sv, model, n), "SplitVector/InsertionString",
                  "matches the model after every step");
        }
        tiku_ed_starts_free(&sv);
    }

    {
        static const int64_t after8[] = { 7, 8, 5, 6, 3, 4, 2, 1 };
        static const int64_t after_delete[] = { 7, 8, 5, 6, 4, 2, 1 };
        static const int64_t after13[] = {
            0xd, 0xe, 0xb, 0xc, 9, 0xa, 7, 8, 5, 6, 4, 2, 1
        };

        sv = fresh();
        ins(&sv, 0, 1); ins(&sv, 0, 2); ins(&sv, 0, 3); ins(&sv, 1, 4);
        ins(&sv, 0, 5); ins(&sv, 1, 6); ins(&sv, 0, 7); ins(&sv, 1, 8);
        check(holds(&sv, after8, 8), "SplitVector/InsertionPattern",
              "78563421");
        tiku_ed_starts_delete(&sv, 4, 1);
        check(holds(&sv, after_delete, 7), "SplitVector/InsertionPattern",
              "7856421 after deleting index 4");
        ins(&sv, 0, 9); ins(&sv, 1, 0xa); ins(&sv, 0, 0xb);
        ins(&sv, 1, 0xc); ins(&sv, 0, 0xd); ins(&sv, 1, 0xe);
        check(holds(&sv, after13, 13), "SplitVector/InsertionPattern",
              "debc9a7856421");
        tiku_ed_starts_free(&sv);
    }

    sv = fresh();
    check(tiku_ed_starts_reserve(&sv, TEST_ARRAY_LEN), "room", "array");
    tiku_ed_starts_insert_many(&sv, 0, test_array, TEST_ARRAY_LEN);
    check(holds(&sv, test_array, TEST_ARRAY_LEN), "SplitVector/InsertFromArray",
          "3..12");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 10, 0);
    tiku_ed_starts_set(&sv, 5, 3);
    check(sv.length == 10, "SplitVector/SetValue", "length 10");
    for (i = 0; i < sv.length; i++)
        check(tiku_ed_starts_at(&sv, i) == (i == 5 ? 3 : 0),
              "SplitVector/SetValue", "value at each index");
    fill(&sv, 7, 1, 17);                 /* moves the gap */
    check(tiku_ed_starts_at(&sv, 7) == 17 && tiku_ed_starts_at(&sv, 8) == 0,
          "SplitVector/SetValue", "insert after moving the gap");
    tiku_ed_starts_set(&sv, 8, 19);      /* after the gap */
    check(tiku_ed_starts_at(&sv, 8) == 19, "SplitVector/SetValue",
          "set after the gap");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 10, 0);
    tiku_ed_starts_set(&sv, 5, 3);
    for (i = 0; i < sv.length; i++)
        check(tiku_ed_starts_at(&sv, i) == (i == 5 ? 3 : 0),
              "SplitVector/Indexing", "indexed value");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 10, 0);
    fill(&sv, 7, 1, 1);
    check(sv.length == 11, "SplitVector/Fill", "length 11");
    for (i = 0; i < sv.length; i++)
        check(tiku_ed_starts_at(&sv, i) == (i == 7 ? 1 : 0),
              "SplitVector/Fill", "value at each index");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    check(tiku_ed_starts_reserve(&sv, TEST_ARRAY_LEN), "room", "array");
    tiku_ed_starts_insert_many(&sv, 0, test_array, TEST_ARRAY_LEN);
    tiku_ed_starts_delete(&sv, 2, 1);
    check(sv.length == TEST_ARRAY_LEN - 1 && tiku_ed_starts_at(&sv, 0) == 3 &&
          tiku_ed_starts_at(&sv, 1) == 4 && tiku_ed_starts_at(&sv, 2) == 6,
          "SplitVector/DeleteOne", "3 4 6 ...");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 10, 0);
    fill(&sv, 7, 1, 1);
    tiku_ed_starts_delete(&sv, 2, 3);
    check(sv.length == 8, "SplitVector/DeleteRange", "length 8");
    for (i = 0; i < sv.length; i++)
        check(tiku_ed_starts_at(&sv, i) == (i == 4 ? 1 : 0),
              "SplitVector/DeleteRange", "value at each index");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 10, 0);
    fill(&sv, 7, 1, 1);
    tiku_ed_starts_delete(&sv, 2, 3);
    {
        ptrdiff_t capacity = sv.capacity;

        tiku_ed_starts_delete(&sv, 0, sv.length);
        check(sv.length == 0, "SplitVector/DeleteAll", "empty");
        /* Tiku: emptying keeps the storage, so a commit can refill it. */
        check(sv.capacity == capacity && sv.gap == capacity,
              "SplitVector/DeleteAll", "storage kept (adaptation)");
    }
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 10, 0);
    fill(&sv, 7, 1, 1);
    tiku_ed_starts_copy(&sv, got, 0, 11);
    for (i = 0; i < 11; i++)
        check(got[i] == (i == 7 ? 1 : 0), "SplitVector/GetRange",
              "retrieved value");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    check(tiku_ed_starts_reserve(&sv, TEST_ARRAY_LEN), "room", "array");
    tiku_ed_starts_insert_many(&sv, 0, test_array, TEST_ARRAY_LEN);
    tiku_ed_starts_copy(&sv, got, 0, TEST_ARRAY_LEN);
    for (i = 0; i < TEST_ARRAY_LEN; i++)
        check(got[i] == i + 3, "SplitVector/GetRangeOverGap",
              "retrieved value");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 105, 0);
    for (i = 0; i < 105; i++)
        tiku_ed_starts_set(&sv, i, i + 2);
    for (i = 0; i < sv.length; i++) {
        fill(&sv, i, 1, i + 9);
        tiku_ed_starts_delete(&sv, i + 1, 1);
    }
    for (i = 0; i < sv.length; i++)
        check(tiku_ed_starts_at(&sv, i) == i + 9, "SplitVector/ReplaceUp",
              "every element replaced");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 24, 0);
    for (i = 0; i < 24; i++)
        tiku_ed_starts_set(&sv, i, i + 12);
    for (i = sv.length - 1; i >= 0; i--) {
        fill(&sv, i, 1, i + 5);
        tiku_ed_starts_delete(&sv, i + 1, 1);
    }
    for (i = 0; i < sv.length; i++)
        check(tiku_ed_starts_at(&sv, i) == i + 5, "SplitVector/ReplaceDown",
              "every element replaced");
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 10, 87);
    for (i = 0; i < 10; i += 2) {
        ptrdiff_t len = 10 - i, j;

        check(sv.length == len, "SplitVector/DeleteBackAndForth", "length");
        for (j = 0; j < sv.length; j++)
            check(tiku_ed_starts_at(&sv, j) == 87,
                  "SplitVector/DeleteBackAndForth", "value");
        tiku_ed_starts_delete(&sv, len - 1, 1);
        tiku_ed_starts_delete(&sv, 0, 1);
    }
    tiku_ed_starts_free(&sv);

    sv = fresh();
    fill(&sv, 0, 10, 87);
    check(tiku_ed_starts_at(&sv, -1) == 0 && tiku_ed_starts_at(&sv, 10) == 0,
          "SplitVector/OutsideBounds", "empty value outside");
    tiku_ed_starts_free(&sv);
}

/** @brief The byte specialisation: the same buffer rules over text. */
static void
split_bytes(void)
{
    tiku_ed_bytes_t b;
    char got[64];
    ptrdiff_t capacity;

    tiku_ed_bytes_init(&b);
    check(tiku_ed_bytes_reserve(&b, 8), "BYTES-room", "reserve");
    tiku_ed_bytes_insert(&b, 0, "world", 5);
    tiku_ed_bytes_insert(&b, 0, "hello ", 6);
    tiku_ed_bytes_copy(&b, got, 0, b.length);
    check(b.length == 11 && !memcmp(got, "hello world", 11), "BYTES-insert",
          "inserted at the front across the gap");
    tiku_ed_bytes_delete(&b, 5, 1);
    tiku_ed_bytes_copy(&b, got, 0, b.length);
    check(b.length == 10 && !memcmp(got, "helloworld", 10), "BYTES-delete",
          "deleted in the middle");
    check(tiku_ed_bytes_at(&b, 0) == 'h' && tiku_ed_bytes_at(&b, 9) == 'd' &&
          tiku_ed_bytes_at(&b, 10) == 0 && tiku_ed_bytes_at(&b, -1) == 0,
          "BYTES-at", "bytes and the empty value outside");
    capacity = b.capacity;
    tiku_ed_bytes_delete(&b, 0, b.length);
    check(b.length == 0 && b.capacity == capacity, "BYTES-empty",
          "emptying keeps the storage");
    tiku_ed_bytes_free(&b);
}

/** @brief Upstream Partitioning::Check: starts ascending, none empty
 *         unless the whole is. */
static int
partitions_ok(const tiku_ed_lines_t *p)
{
    int64_t i, n = tiku_ed_lines_count(p);

    if (n < 1 || tiku_ed_lines_start(p, n) < 0)
        return 0;
    if (tiku_ed_lines_start(p, n) == 0)
        return tiku_ed_lines_start(p, 0) == 0 && tiku_ed_lines_start(p, 1) == 0;
    for (i = 0; i < n; i++)
        if (tiku_ed_lines_start(p, i) >= tiku_ed_lines_start(p, i + 1))
            return 0;
    return 1;
}

/** @brief A fresh partitioning with room for 64 more starts. */
static tiku_ed_lines_t
part_fresh(void)
{
    tiku_ed_lines_t p;

    check(tiku_ed_lines_init(&p) && tiku_ed_lines_reserve(&p, 64), "room",
          "partitions");
    return p;
}

/** @brief testPartitioning.cxx, Partitioning, one SECTION per block. */
static void
partitioning(void)
{
    tiku_ed_lines_t p;
    int64_t i;

    p = part_fresh();
    check(tiku_ed_lines_count(&p) == 1 && tiku_ed_lines_start(&p, 1) == 0 &&
          tiku_ed_lines_find(&p, 0) == 0, "Partitioning/IsEmptyInitially",
          "one empty partition");
    tiku_ed_lines_free(&p);

    p = part_fresh();
    tiku_ed_lines_shift(&p, 0, 1);
    check(tiku_ed_lines_count(&p) == 1 && tiku_ed_lines_start(&p, 1) == 1,
          "Partitioning/SimpleInsert", "one partition of 1");
    tiku_ed_lines_free(&p);

    p = part_fresh();
    tiku_ed_lines_shift(&p, 0, 2);
    tiku_ed_lines_insert(&p, 1, 1);
    check(tiku_ed_lines_count(&p) == 2 && tiku_ed_lines_start(&p, 0) == 0 &&
          tiku_ed_lines_start(&p, 1) == 1 && tiku_ed_lines_start(&p, 2) == 2,
          "Partitioning/TwoPartitions", "0 1 2");
    tiku_ed_lines_free(&p);

    p = part_fresh();
    tiku_ed_lines_shift(&p, 0, 3);
    tiku_ed_lines_insert(&p, 1, 2);
    tiku_ed_lines_set_start(&p, 1, 1);
    check(tiku_ed_lines_count(&p) == 2 && tiku_ed_lines_start(&p, 0) == 0 &&
          tiku_ed_lines_start(&p, 1) == 1 && tiku_ed_lines_start(&p, 2) == 3 &&
          partitions_ok(&p), "Partitioning/MoveStart", "0 1 3");
    tiku_ed_lines_free(&p);

    p = part_fresh();
    tiku_ed_lines_shift(&p, 0, 3);
    tiku_ed_lines_insert(&p, 1, 2);
    tiku_ed_lines_shift(&p, 0, 3);
    tiku_ed_lines_shift(&p, 1, 2);
    check(tiku_ed_lines_count(&p) == 2 && tiku_ed_lines_start(&p, 0) == 0 &&
          tiku_ed_lines_start(&p, 1) == 5 && tiku_ed_lines_start(&p, 2) == 8 &&
          partitions_ok(&p), "Partitioning/InsertAgain", "0 5 8");
    tiku_ed_lines_free(&p);

    {
        static const int64_t pos[] = { 2, 5, 7 };

        p = part_fresh();
        tiku_ed_lines_shift(&p, 0, 10);
        tiku_ed_lines_insert_many(&p, 1, pos, 3);
        check(tiku_ed_lines_count(&p) == 4 && tiku_ed_lines_start(&p, 1) == 2 &&
              tiku_ed_lines_start(&p, 2) == 5 &&
              tiku_ed_lines_start(&p, 3) == 7 &&
              tiku_ed_lines_start(&p, 4) == 10 && partitions_ok(&p),
              "Partitioning/InsertMultiple", "0 2 5 7 10");
        tiku_ed_lines_free(&p);
    }

    {
        /* InsertMultipleWithCast, adapted: one element type, so the cast
         * variant is the same call. */
        static const int64_t pos[] = { 2, 4, 6, 8 };

        p = part_fresh();
        tiku_ed_lines_shift(&p, 0, 9);
        tiku_ed_lines_insert_many(&p, 1, pos, 4);
        check(tiku_ed_lines_count(&p) == 5 && tiku_ed_lines_start(&p, 4) == 8 &&
              tiku_ed_lines_start(&p, 5) == 9 && partitions_ok(&p),
              "Partitioning/InsertMultipleWithCast", "0 2 4 6 8 9");
        tiku_ed_lines_free(&p);
    }

    p = part_fresh();
    tiku_ed_lines_shift(&p, 0, 3);
    tiku_ed_lines_insert(&p, 1, 2);
    tiku_ed_lines_shift(&p, 1, 2);
    tiku_ed_lines_shift(&p, 0, 3);
    check(tiku_ed_lines_count(&p) == 2 && tiku_ed_lines_start(&p, 1) == 5 &&
          tiku_ed_lines_start(&p, 2) == 8 && partitions_ok(&p),
          "Partitioning/InsertReversed", "0 5 8");
    tiku_ed_lines_free(&p);

    p = part_fresh();
    tiku_ed_lines_shift(&p, 0, 3);
    tiku_ed_lines_insert(&p, 1, 2);
    tiku_ed_lines_set_start(&p, 1, 1);
    check(tiku_ed_lines_find(&p, 0) == 0 && tiku_ed_lines_find(&p, 1) == 1 &&
          tiku_ed_lines_find(&p, 2) == 1 && tiku_ed_lines_find(&p, 3) == 1 &&
          partitions_ok(&p), "Partitioning/InverseSearch",
          "positions to partitions");
    tiku_ed_lines_free(&p);

    p = part_fresh();
    tiku_ed_lines_shift(&p, 0, 2);
    tiku_ed_lines_insert(&p, 1, 1);
    tiku_ed_lines_remove(&p, 1);
    check(tiku_ed_lines_count(&p) == 1 && tiku_ed_lines_start(&p, 1) == 2 &&
          partitions_ok(&p), "Partitioning/DeletePartition", "0 2");
    tiku_ed_lines_free(&p);

    p = part_fresh();
    tiku_ed_lines_shift(&p, 0, 3);
    tiku_ed_lines_insert(&p, 1, 2);
    tiku_ed_lines_set_start(&p, 1, 1);
    tiku_ed_lines_reset(&p);
    check(tiku_ed_lines_count(&p) == 1 && tiku_ed_lines_start(&p, 1) == 0,
          "Partitioning/DeleteAll", "back to the initial state");
    tiku_ed_lines_free(&p);

    p = part_fresh();
    tiku_ed_lines_shift(&p, 0, 10);
    tiku_ed_lines_insert(&p, 1, 3);
    tiku_ed_lines_insert(&p, 2, 6);
    tiku_ed_lines_insert(&p, 3, 9);
    tiku_ed_lines_shift(&p, 2, 4);
    tiku_ed_lines_shift(&p, 1, 2);
    tiku_ed_lines_shift(&p, 0, 3);
    check(tiku_ed_lines_count(&p) == 4 && tiku_ed_lines_start(&p, 1) == 6 &&
          tiku_ed_lines_start(&p, 2) == 11 &&
          tiku_ed_lines_start(&p, 3) == 18 &&
          tiku_ed_lines_start(&p, 4) == 19 && partitions_ok(&p),
          "Partitioning/TestBackwards", "0 6 11 18 19");
    tiku_ed_lines_free(&p);

    p = part_fresh();
    tiku_ed_lines_shift(&p, 0, 42);                 /* provoke backstep */
    for (i = 0; i < 20; i++)
        tiku_ed_lines_insert(&p, i + 1, (i + 1) * 2);
    for (i = 20; i > 0; i--)
        tiku_ed_lines_shift(&p, i, 2);
    check(tiku_ed_lines_count(&p) == 21, "Partitioning/TestMany", "21");
    for (i = 1; i < 20; i++)
        check(tiku_ed_lines_start(&p, i) == i * 4 - 2 &&
              tiku_ed_lines_find(&p, i * 4 - 2) == i,
              "Partitioning/TestMany", "start and inverse");
    tiku_ed_lines_shift(&p, 19, 2);
    check(tiku_ed_lines_find(&p, 10) == 3, "Partitioning/TestMany", "10 in 3");
    tiku_ed_lines_shift(&p, 0, 2);
    tiku_ed_lines_shift(&p, 0, -2);
    tiku_ed_lines_remove(&p, 1);
    check(tiku_ed_lines_start(&p, 0) == 0 && tiku_ed_lines_start(&p, 1) == 6 &&
          tiku_ed_lines_start(&p, 2) == 10, "Partitioning/TestMany",
          "after removing 1");
    tiku_ed_lines_remove(&p, 10);
    check(tiku_ed_lines_start(&p, 10) == 46 &&
          tiku_ed_lines_find(&p, 46) == 10 &&
          tiku_ed_lines_start(&p, 11) == 50 &&
          tiku_ed_lines_find(&p, 50) == 11 &&
          partitions_ok(&p), "Partitioning/TestMany", "after removing 10");
    tiku_ed_lines_free(&p);
}

/** @brief The adaptation's own promise: once room is made, inserting
 *         allocates nothing, and emptying the lines allocates nothing. */
static void
no_allocation_after_room(void)
{
    tiku_ed_text_t t;
    unsigned long before;

    check(tiku_ed_text_init(&t), "TXN-NOALLOC", "text");
    check(tiku_ed_text_reserve(&t, 64, 16), "TXN-NOALLOC", "reserve");
    before = tiku_ed_test_commit_allocations();
    tiku_ed_commit_begin();
    tiku_ed_text_insert(&t, 0, "a\r\nb\rc\nd", 8);
    tiku_ed_text_delete(&t, 0, 8);
    tiku_ed_text_insert(&t, 0, "x\ny", 3);
    tiku_ed_commit_end();
    check(tiku_ed_test_commit_allocations() == before, "TXN-NOALLOC",
          "insert, empty and refill inside a commit allocate nothing");
    check(tiku_ed_lines_count(&t.lines) == 2, "TXN-NOALLOC", "two lines");
    tiku_ed_text_free(&t);
}

int
main(void)
{
    split_vector();
    split_bytes();
    partitioning();
    no_allocation_after_room();
    printf("%s\n", fails ? "FAIL: editor storage" :
           "PASS: editor storage -- upstream SplitVector and Partitioning "
           "cases, the byte buffer, no allocation after room");
    return fails != 0;
}
