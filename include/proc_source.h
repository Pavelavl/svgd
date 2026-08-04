/**
 * @file proc_source.h
 * @brief Источник метрик SRC_PROC — live-чтение /proc (Фаза 2, Stage 2)
 *
 * Читает мгновенные значения из /proc (без диска и RRD) и собирает MetricData
 * в памяти. Use-case: «вообще без RRD» на сверхслабых устройствах; svgd как
 * живой системный монитор. Источник выбирается per-metric: config.json →
 * "source": "proc", "proc_metric": "cpu" | "load".
 *
 * Чистые функции-парсеры (proc_parse_cpu_stat / proc_cpu_utilization /
 * proc_parse_loadavg) выделены отдельно и покрыты unit-тестами
 * (tests/c/test_proc.c) — дословно тот же приём, что с select_step_from_rras.
 *
 * Ограничение (честный gap): /proc даёт лишь текущее мгновенное значение, поэтому
 * серии состоят из одной точки (timestamp=now). Исторический ряд без кольцевого
 * буфера сэмплов не строится — это сознательная минимализация Stage 2; будущий
 * ring-buffer в памяти расширил бы ряд до N последних сэмплов.
 */
#ifndef SVGD_PROC_SOURCE_H
#define SVGD_PROC_SOURCE_H

#include "cfg.h"
#include "rrd/reader.h"  /* MetricData */

/**
 * @brief Разобрать агрегатную строку "cpu" из /proc/stat
 *
 * Формат: "cpu  user nice system idle iowait irq softirq steal [guest ...]".
 * Считаются первые 8 полей (guest/guest_nice не учитываются — они уже входят
 * в user/nice, двойной подсчёт избегается). busy = user+nice+system+irq+
 * softirq+steal; total = busy+idle+iowait.
 *
 * @param line Строка из /proc/stat (агрегатная "cpu ...")
 * @param busy[out] Сумма «активных» jiffies
 * @param total[out] Сумма всех jiffies
 * @return 0 при успехе, -1 при ошибке/несоответствии формату
 */
int proc_parse_cpu_stat(const char *line, unsigned long long *busy,
                        unsigned long long *total);

/**
 * @brief Утилизация CPU (%) по двум сэмплам jiffies
 *
 * 100 * (Δbusy / Δtotal). Клампится в [0,100]. При total1 <= total0 (нет
 * прошедшего времени / wrap) возвращает 0. Используется для дельты между
 * последовательными сэмплами; для первого сэмпла передают (0,0, busy,total),
 * что даёт утилизацию с момента загрузки.
 */
double proc_cpu_utilization(unsigned long long busy0, unsigned long long total0,
                            unsigned long long busy1, unsigned long long total1);

/**
 * @brief Разобрать первую строку /proc/loadavg
 *
 * Формат: "1min 5min 15min running/total last_pid". Разбираются три load-средних.
 * @return 0 при успехе, -1 если нет трёх чисел
 */
int proc_parse_loadavg(const char *line, double *l1, double *l5, double *l15);

/**
 * @brief Получить MetricData из /proc по metric->proc_metric
 *
 * Выбор ридера: "cpu" → /proc/stat (утилизация, 1 серия, 1 точка);
 * "load" → /proc/loadavg (3 серии load1/load5/load15, по 1 точке).
 * @param config (не используется — proc читает системное /proc)
 * @param metric Конфиг метрики (поле proc_metric выбирает ридер)
 * @param period (не используется — live-значение, без истории)
 * @return MetricData (освобождается free_metric_data) или NULL при ошибке
 */
MetricData* proc_source_fetch(Config *config, MetricConfig *metric, int period);

#endif /* SVGD_PROC_SOURCE_H */
