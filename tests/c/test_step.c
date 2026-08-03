/**
 * @file test_step.c
 * @brief Тесты select_step_from_rras — чистой логики выбора шага агрегации RRA
 *
 * Покрывает все ветви алгоритма: попадание в окно [100,2400] точек, все RRA
 * с недостатком точек, все RRA с избытком, пропуск не-AVERAGE, fallback на
 * «сырой» RRA (pdp_per_row==1), пустой список, шаг ниже min_step.
 */
#include "minitest.h"
#include "rrd/reader.h"

/* Компактное построение элемента RRA для тестов. */
static RRAStepInfo mk(unsigned long pdp, unsigned long step, const char *cf) {
    RRAStepInfo r;
    r.pdp_per_row = pdp;
    r.effective_step = step;
    r.cf = cf;
    return r;
}

/* Диапазон 3600с, первый же RRA даёт 120 точек (в окне) -> выбирается сразу. */
TEST(in_window_first_match_wins) {
    RRAStepInfo rras[] = {
        mk(2, 30, "AVERAGE"),  /* 3600/30 = 120 точек — в окне [100,2400] */
        mk(1, 15, "AVERAGE"),  /* 240 точек (тоже в окне, но он второй) */
    };
    ASSERT(select_step_from_rras(rras, 2, 3600, 3600, 15) == 30);
}

/* Все RRA дают <100 точек: выбирается шаг с максимумом точек. */
TEST(all_below_window_picks_most_points) {
    RRAStepInfo rras[] = {
        mk(20, 300, "AVERAGE"),  /* 12 точек */
        mk(40, 600, "AVERAGE"),  /* 6 точек */
        mk(8,  120, "AVERAGE"),  /* 30 точек — максимум среди <100 */
    };
    ASSERT(select_step_from_rras(rras, 3, 3600, 3600, 15) == 120);
}

/* Все RRA дают >2400 точек: выбирается минимальный шаг. */
TEST(all_above_window_picks_smallest_step) {
    RRAStepInfo rras[] = {
        mk(1, 15, "AVERAGE"),  /* 360000/15 = 24000 */
        mk(2, 30, "AVERAGE"),  /* 12000 */
        mk(4, 60, "AVERAGE"),  /* 6000 */
    };
    ASSERT(select_step_from_rras(rras, 3, 360000, 360000, 15) == 15);
}

/* Только не-AVERAGE RRA: цикл их пропускает, fallback ничего не находит ->
 * возвращается base_step. */
TEST(non_average_returns_base_step) {
    RRAStepInfo rras[] = { mk(1, 15, "MAX"), mk(2, 30, "MIN") };
    ASSERT(select_step_from_rras(rras, 2, 3600, 3600, 15) == 15);
}

/* Fallback на «сырой» AVERAGE (pdp_per_row==1): основной цикл пропускает его
 * (эффективный шаг 15 < min_step=30), но срабатывает ветка range<=period. */
TEST(fallback_to_raw_average) {
    RRAStepInfo rras[] = { mk(1, 15, "AVERAGE") };
    ASSERT(select_step_from_rras(rras, 1, 600, 600, 30) == 15);
}

/* Пустой список RRA (rra_count==0) -> base_step. NULL не разыменовывается. */
TEST(empty_rras_returns_base_step) {
    ASSERT(select_step_from_rras(NULL, 0, 3600, 3600, 15) == 15);
}

/* AVERAGE с шагом < min_step пропускается; fallback не активен (range>period). */
TEST(step_below_min_skipped_no_fallback) {
    RRAStepInfo rras[] = { mk(1, 5, "AVERAGE") };  /* step 5 < base 15 */
    ASSERT(select_step_from_rras(rras, 1, 3600, 600, 15) == 15);
}

/* Только не-AVERAGE И range>period (fallback не активен) -> base_step. */
TEST(non_average_range_over_period_returns_base) {
    RRAStepInfo rras[] = { mk(1, 15, "MAX") };
    ASSERT(select_step_from_rras(rras, 1, 7200, 3600, 15) == 15);
}

TEST_MAIN()
    RUN(in_window_first_match_wins);
    RUN(all_below_window_picks_most_points);
    RUN(all_above_window_picks_smallest_step);
    RUN(non_average_returns_base_step);
    RUN(fallback_to_raw_average);
    RUN(empty_rras_returns_base_step);
    RUN(step_below_min_skipped_no_fallback);
    RUN(non_average_range_over_period_returns_base);
TEST_RETURN()
