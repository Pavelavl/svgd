/**
 * @file test_proc.c
 * @brief Тесты чистых функций SRC_PROC (proc_parse_cpu_stat / proc_cpu_utilization /
 *        proc_parse_loadavg)
 *
 * Парсеры выделены без I/O (см. include/proc_source.h) — тестируются на
 * строках-эталонах. Значения jiffies/урезание диапазона проверяются явно.
 * I/O-ридеры (read_cpu/read_load) работают с живым /proc — покрыты smoke-тестом
 * бинарника, не unit-тестом.
 */
#include "minitest.h"
#include "proc_source.h"

TEST(parse_cpu_stat_aggregate) {
    /* Реальный формат /proc/stat (агрегатная строка), округлённые числа. */
    const char *line = "cpu  100 0 200 5000 50 10 5 0 0 0";
    unsigned long long busy = 0, total = 0;
    ASSERT(proc_parse_cpu_stat(line, &busy, &total) == 0);
    /* busy = user(100)+nice(0)+system(200)+irq(10)+softirq(5)+steal(0) = 315 */
    ASSERT(busy == 315ULL);
    /* idle = idle(5000)+iowait(50) = 5050; total = busy+idle = 5365 */
    ASSERT(total == 5365ULL);
}

TEST(parse_cpu_stat_minimal_four_fields) {
    /* Минимальный случай: только user,nice,system,idle (старые ядра). */
    const char *line = "cpu  10 0 5 100";
    unsigned long long busy = 0, total = 0;
    ASSERT(proc_parse_cpu_stat(line, &busy, &total) == 0);
    ASSERT(busy == 15ULL);     /* 10+0+5, остальное 0 */
    ASSERT(total == 115ULL);   /* 15 + 100(idle) */
}

TEST(parse_cpu_stat_rejects_per_core_line) {
    /* Поядровая строка "cpu0" отвергается (читать надо агрегат "cpu"). */
    const char *line = "cpu0 10 0 5 100 0 0 0 0";
    unsigned long long busy = 0, total = 0;
    ASSERT(proc_parse_cpu_stat(line, &busy, &total) == -1);
}

TEST(parse_cpu_stat_rejects_bad_input) {
    unsigned long long busy = 0, total = 0;
    ASSERT(proc_parse_cpu_stat("meminfo: ...", &busy, &total) == -1);
    ASSERT(proc_parse_cpu_stat("cpu 1 2", &busy, &total) == -1); /* <4 полей */
    ASSERT(proc_parse_cpu_stat(NULL, &busy, &total) == -1);
    ASSERT(proc_parse_cpu_stat("cpu 1 2 3 4", NULL, &total) == -1);
}

TEST(cpu_utilization_basic) {
    /* Δbusy=10, Δtotal=100 → 10%. */
    ASSERT(proc_cpu_utilization(100, 1000, 110, 1100) == 10.0);
}

TEST(cpu_utilization_first_sample_since_boot) {
    /* (0,0)→(busy,total): первый сэмпл = утилизация с загрузки. busy=300,total=1200 → 25%. */
    double u = proc_cpu_utilization(0, 0, 300, 1200);
    ASSERT(u > 24.99 && u < 25.01);
}

TEST(cpu_utilization_no_time_returns_zero) {
    /* total1 <= total0 — нет прошедшего времени / wrap. */
    ASSERT(proc_cpu_utilization(50, 1000, 60, 1000) == 0.0);
    ASSERT(proc_cpu_utilization(50, 1000, 60, 999) == 0.0);
}

TEST(cpu_utilization_clamps_to_range) {
    /* busy уменьшилось (счётчик не должен расти назад, но защитимся) → 0%. */
    ASSERT(proc_cpu_utilization(200, 1000, 100, 1100) == 0.0);
    /* Δbusy > Δtotal невозможно физически, но при garbage-input клампим в 100. */
    double u = proc_cpu_utilization(0, 100, 1000, 200); /* db=1000, dt=100 → 1000% → clamp 100 */
    ASSERT(u == 100.0);
}

TEST(parse_loadavg_three_values) {
    const char *line = "0.34 0.45 0.50 2/1234 5678";
    double l1 = 0, l5 = 0, l15 = 0;
    ASSERT(proc_parse_loadavg(line, &l1, &l5, &l15) == 0);
    ASSERT(l1 > 0.339 && l1 < 0.341);
    ASSERT(l5 > 0.449 && l5 < 0.451);
    ASSERT(l15 > 0.499 && l15 < 0.501);
}

TEST(parse_loadavg_rejects_bad_input) {
    double l1, l5, l15;
    ASSERT(proc_parse_loadavg("not a number", &l1, &l5, &l15) == -1);
    ASSERT(proc_parse_loadavg("1.0 2.0", &l1, &l5, &l15) == -1); /* <3 чисел */
    ASSERT(proc_parse_loadavg(NULL, &l1, &l5, &l15) == -1);
}

TEST_MAIN()
    RUN(parse_cpu_stat_aggregate);
    RUN(parse_cpu_stat_minimal_four_fields);
    RUN(parse_cpu_stat_rejects_per_core_line);
    RUN(parse_cpu_stat_rejects_bad_input);
    RUN(cpu_utilization_basic);
    RUN(cpu_utilization_first_sample_since_boot);
    RUN(cpu_utilization_no_time_returns_zero);
    RUN(cpu_utilization_clamps_to_range);
    RUN(parse_loadavg_three_values);
    RUN(parse_loadavg_rejects_bad_input);
TEST_RETURN()
