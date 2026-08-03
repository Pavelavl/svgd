/**
 * @file minitest.h
 * @brief Минималистичный харнес C unit-тестов (в духе svgd-collect/tests/minitest.h)
 *
 * Подход сознательно повторяет svgd-collect: каждый тестовый файл — отдельный
 * бинарник, TEST/RUN/ASSERT/ASSERT_STR/TEST_MAIN/TEST_RETURN — привычный API.
 *RUN печатает «ok»/«FAIL» по изменению счётчика (более честный вывод, чем
 * безусловное «ok» после раннего return из ASSERT).
 */
#ifndef SVGD_MINITEST_H
#define SVGD_MINITEST_H

#include <stdio.h>
#include <string.h>

/* Счётчик провалов — file-local (один бинарник на тестовый файл). */
static int mt_failures = 0;

#define TEST(name) static void name(void)

#define RUN(name) do { \
    int mt_before = mt_failures; \
    printf("  %s ... ", #name); \
    name(); \
    printf("%s\n", (mt_failures == mt_before) ? "ok" : "FAIL"); \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\n    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        mt_failures++; \
        return; \
    } \
} while (0)

#define ASSERT_STR(a, b) ASSERT(strcmp((a), (b)) == 0)

#define TEST_MAIN() int main(void) { mt_failures = 0;

#define TEST_RETURN() printf("\n%d failure(s)\n", mt_failures); return mt_failures ? 1 : 0; }

#endif /* SVGD_MINITEST_H */
