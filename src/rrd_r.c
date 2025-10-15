#include "../include/rrd_r.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

unsigned long select_optimal_step(const char *filename, time_t start, time_t end) {
    rrd_info_t *info = rrd_info_r(filename);
    if (!info) {
        fprintf(stderr, "Failed to get RRD info: %s\n", rrd_get_error());
        return 70; // Fallback to a safer step (70s) since 10s is problematic
    }

    // Базовый шаг RRD файла
    unsigned long base_step = 10;
    for (rrd_info_t *ptr = info; ptr; ptr = ptr->next) {
        if (strcmp(ptr->key, "step") == 0) {
            base_step = ptr->value.u_cnt;
            break;
        }
    }

    // Собери информацию о RRA
    typedef struct {
        unsigned long pdp_per_row;
        unsigned long rows;
        unsigned long effective_step;
        char *cf; // Функция консолидации
        int index; // Индекс RRA
    } RRAInfo;
    RRAInfo rras[20]; // Максимум 20 RRA
    int rra_count = 0;

    for (rrd_info_t *ptr = info; ptr; ptr = ptr->next) {
        if (strncmp(ptr->key, "rra[", 4) == 0) {
            int rra_idx = atoi(ptr->key + 4);
            if (strstr(ptr->key, ".pdp_per_row")) {
                rras[rra_count].pdp_per_row = ptr->value.u_cnt;
                rras[rra_count].index = rra_idx;
            } else if (strstr(ptr->key, ".rows")) {
                rras[rra_count].rows = ptr->value.u_cnt;
            } else if (strstr(ptr->key, ".cf")) {
                rras[rra_count].cf = strdup(ptr->value.u_str);
                rras[rra_count].effective_step = rras[rra_count].pdp_per_row * base_step;
                rra_count++;
            }
        }
    }

    // Проверка первого валидного таймстампа
    time_t first_timestamp = -1;
    int selected_rra_index = -1;
    for (int i = 0; i < rra_count; i++) {
        if (strcmp(rras[i].cf, "AVERAGE") == 0 && (first_timestamp == -1 || rras[i].pdp_per_row == 1)) {
            time_t ts = rrd_first_r(filename, rras[i].index);
            if (ts != -1) {
                first_timestamp = ts;
                selected_rra_index = rras[i].index;
                fprintf(stderr, "First timestamp for rra[%d] (step=%lu): %ld (%s)", 
                        rras[i].index, rras[i].effective_step, first_timestamp, ctime(&first_timestamp));
                if (rras[i].pdp_per_row == 1) break; // Предпочтение высокому разрешению
            }
        }
    }
    if (first_timestamp == -1) {
        fprintf(stderr, "Failed to get first timestamp, using fallback: %s\n", rrd_get_error());
        first_timestamp = end - 3600; // Fallback: 1 час назад
    }
    if (start < first_timestamp) {
        fprintf(stderr, "Adjusting start time from %ld to %ld\n", start, first_timestamp);
        start = first_timestamp;
    }

    // Рассчитай диапазон
    time_t range = end - start;
    if (range <= 0) {
        fprintf(stderr, "Invalid time range: start=%ld, end=%ld\n", start, end);
        for (int i = 0; i < rra_count; i++) free(rras[i].cf);
        rrd_info_free(info);
        return 70; // Fallback to safer step
    }

    // Целевое количество точек
    const int min_points = 100;
    const int max_points = 1000;
    const unsigned long min_step = 70; // Минимальный шаг, чтобы избежать проблем с 10s

    // Найди RRA с подходящим шагом
    unsigned long optimal_step = 0;
    int best_num_points = 0;
    int best_rra_index = -1;

    for (int i = 0; i < rra_count; i++) {
        if (strcmp(rras[i].cf, "AVERAGE") != 0) continue; // Только AVERAGE
        unsigned long step = rras[i].effective_step;
        if (step < min_step) continue; // Пропускаем шаги меньше 70 секунд

        int num_points = (range + step - 1) / step;
        fprintf(stderr, "Evaluating rra[%d]: step=%lu, num_points=%d\n", 
                rras[i].index, step, num_points);

        // Проверяем, подходит ли шаг
        if (num_points >= min_points && num_points <= max_points) {
            optimal_step = step;
            best_rra_index = rras[i].index;
            best_num_points = num_points;
            break; // Найден идеальный шаг
        }
        // Сохраняем шаг с наибольшим количеством точек, если все дают слишком мало
        if (num_points < min_points && (optimal_step == 0 || num_points > best_num_points)) {
            optimal_step = step;
            best_rra_index = rras[i].index;
            best_num_points = num_points;
        }
        // Если слишком много точек, сохраняем как запасной вариант
        if (num_points > max_points && (optimal_step == 0 || step < optimal_step)) {
            optimal_step = step;
            best_rra_index = rras[i].index;
            best_num_points = num_points;
        }
    }

    // Если не нашли подходящий шаг, попробуем шаг 10 секунд для коротких диапазонов
    if (optimal_step == 0 && range <= 3600) { // Только для диапазонов <= 1 часа
        for (int i = 0; i < rra_count; i++) {
            if (strcmp(rras[i].cf, "AVERAGE") == 0 && rras[i].pdp_per_row == 1) {
                optimal_step = rras[i].effective_step;
                best_rra_index = rras[i].index;
                best_num_points = (range + optimal_step - 1) / optimal_step;
                fprintf(stderr, "Falling back to step=10 for short range (%ld seconds)\n", range);
                break;
            }
        }
    }

    // Если шаг так и не выбран, используем fallback
    if (optimal_step == 0) {
        optimal_step = min_step;
        best_num_points = (range + optimal_step - 1) / optimal_step;
        fprintf(stderr, "No suitable step found, using fallback step=%lu\n", optimal_step);
    }

    // Тестовый вызов rrd_fetch_r для проверки валидности данных
    unsigned long test_step = optimal_step;
    unsigned long ds_cnt;
    char **ds_names = NULL;
    rrd_value_t *data = NULL;
    time_t test_start = start;
    time_t test_end = end;
    int status = rrd_fetch_r(filename, "AVERAGE", &test_start, &test_end, &test_step, &ds_cnt, &ds_names, &data);
    if (status != 0) {
        fprintf(stderr, "Test fetch failed for step=%lu: %s\n", test_step, rrd_get_error());
        optimal_step = min_step; // Fallback
        best_num_points = (range + optimal_step - 1) / optimal_step;
    } else {
        // Проверяем, есть ли валидные данные
        int valid_points = 0;
        int num_points = (test_end - test_start + test_step - 1) / test_step;
        for (int i = 0; i < num_points; i++) {
            for (unsigned long ds = 0; ds < ds_cnt; ds++) {
                if (!isnan(data[i * ds_cnt + ds]) && data[i * ds_cnt + ds] >= 0) {
                    valid_points++;
                    break;
                }
            }
        }
        if (ds_names) rrd_freemem(ds_names);
        if (data) rrd_freemem(data);

        if (valid_points == 0) {
            fprintf(stderr, "No valid data for step=%lu, switching to fallback step=%lu\n", 
                    test_step, min_step);
            optimal_step = min_step;
            best_num_points = (range + optimal_step - 1) / optimal_step;
        }
    }

    fprintf(stderr, "Selected step=%lu for range=%ld seconds (%d points), rra[%d]\n", 
            optimal_step, range, best_num_points, best_rra_index);

    // Освободи память
    for (int i = 0; i < rra_count; i++) free(rras[i].cf);
    rrd_info_free(info);
    return optimal_step;
}

MetricData *fetch_metric_data(const char *filename, time_t start, char *metric_type, char *param1) {
    time_t end = time(NULL);
    unsigned long step = select_optimal_step(filename, start, end);
    unsigned long ds_cnt;
    char **ds_names = NULL;
    rrd_value_t *data = NULL;

    fprintf(stderr, "Fetching data for %s, start=%ld (%s), end=%ld (%s)\n", 
            filename, start, ctime(&start), end, ctime(&end));
    int status = rrd_fetch_r(filename, "AVERAGE", &start, &end, &step, &ds_cnt, &ds_names, &data);
    if (status != 0) {
        fprintf(stderr, "RRD fetch failed for %s: %s\n", filename, rrd_get_error());
        return NULL;
    }

    fprintf(stderr, "After rrd_fetch_r: start=%ld, end=%ld, step=%lu, ds_cnt=%lu\n", start, end, step, ds_cnt);
    for (unsigned long ds = 0; ds < ds_cnt; ds++) {
        fprintf(stderr, "Data source %lu: %s\n", ds, ds_names[ds]);
    }

    int num_points = (end - start + step - 1) / step;
    fprintf(stderr, "Calculated num_points=%d\n", num_points);
    if (num_points <= 0) {
        fprintf(stderr, "No data points in range for %s (start=%ld, end=%ld, step=%lu)\n", filename, start, end, step);
        if (ds_names) rrd_freemem(ds_names);
        if (data) rrd_freemem(data);
        return NULL;
    }

    MetricData *metric_data = malloc(sizeof(MetricData));
    if (!metric_data) {
        perror("Memory allocation failed");
        if (ds_names) rrd_freemem(ds_names);
        if (data) rrd_freemem(data);
        return NULL;
    }

    metric_data->series_count = ds_cnt; // Use both read and write
    metric_data->series_names = malloc(ds_cnt * sizeof(char*));
    metric_data->series_data = malloc(ds_cnt * sizeof(DataPoint*));
    metric_data->series_counts = malloc(ds_cnt * sizeof(int));
    metric_data->metric_type = strdup(metric_type ? metric_type : "unknown");
    metric_data->param1 = strdup(param1 ? param1 : "");

    for (unsigned long ds = 0; ds < ds_cnt; ds++) {
        metric_data->series_names[ds] = strdup(ds_names[ds]);
        metric_data->series_data[ds] = malloc(num_points * sizeof(DataPoint));
        metric_data->series_counts[ds] = 0;

        for (int i = 0; i < num_points; i++) {
            double value = data[i * ds_cnt + ds];
            if (isnan(value) || value < 0) continue; // Skip invalid or negative values
            metric_data->series_data[ds][metric_data->series_counts[ds]].timestamp = start + i * step;
            metric_data->series_data[ds][metric_data->series_counts[ds]].value = value;
            metric_data->series_counts[ds]++;
        }
        fprintf(stderr, "Series %s has %d valid points\n", metric_data->series_names[ds], metric_data->series_counts[ds]);
    }

    if (ds_names) rrd_freemem(ds_names);
    if (data) rrd_freemem(data);

    // Check if any valid data exists
    int has_valid_data = 0;
    for (unsigned long ds = 0; ds < ds_cnt; ds++) {
        if (metric_data->series_counts[ds] > 0) {
            has_valid_data = 1;
            break;
        }
    }
    if (!has_valid_data) {
        fprintf(stderr, "No valid data points found for %s\n", filename);
        free_metric_data(metric_data);
        return NULL;
    }

    return metric_data;
}

void free_metric_data(MetricData *data) {
    if (!data) return;
    for (int i = 0; i < data->series_count; i++) {
        free(data->series_names[i]);
        free(data->series_data[i]);
    }
    free(data->series_names);
    free(data->series_data);
    free(data->series_counts);
    free(data->metric_type);
    free(data->param1);
    free(data);
}

char *generate_svg(duk_context *ctx, const char *script_path, MetricData *data) {
    if (load_js_file(ctx, script_path) != 0) {
        fprintf(stderr, "Failed to load JS file %s: %s\n", script_path, duk_safe_to_string(ctx, -1));
        return NULL;
    }

    duk_push_global_object(ctx);
    if (!duk_has_prop_string(ctx, -1, "generateSVG")) {
        fprintf(stderr, "Function 'generateSVG' not found in global object for %s\n", script_path);
        duk_pop(ctx);
        return NULL;
    }
    duk_get_prop_string(ctx, -1, "generateSVG");

    duk_push_array(ctx);
    for (int s = 0; s < data->series_count; s++) {
        if (data->series_counts[s] == 0) continue;
        duk_push_object(ctx);
        duk_push_string(ctx, data->series_names[s]);
        duk_put_prop_string(ctx, -2, "name");
        duk_push_array(ctx);
        for (int i = 0; i < data->series_counts[s]; i++) {
            duk_push_object(ctx);
            duk_push_number(ctx, (double)data->series_data[s][i].timestamp);
            duk_put_prop_string(ctx, -2, "timestamp");
            duk_push_number(ctx, data->series_data[s][i].value);
            duk_put_prop_string(ctx, -2, "value");
            duk_put_prop_index(ctx, -2, i);
        }
        duk_put_prop_string(ctx, -2, "data");
        duk_put_prop_index(ctx, -2, s);
    }

    duk_push_object(ctx);
    duk_push_string(ctx, data->metric_type ? data->metric_type : "unknown");
    duk_put_prop_string(ctx, -2, "metricType");
    if (data->param1 && strlen(data->param1) > 0) {
        duk_push_string(ctx, data->param1);
        duk_put_prop_string(ctx, -2, "param1");
    }

    if (duk_pcall(ctx, 2) != 0) {
        fprintf(stderr, "JS call error in %s: %s\n", script_path, duk_safe_to_string(ctx, -1));
        duk_pop(ctx);
        return NULL;
    }

    const char *svg = duk_get_string(ctx, -1);
    if (!svg) {
        fprintf(stderr, "Failed to get SVG string from JS in %s\n", script_path);
        duk_pop(ctx);
        return NULL;
    }

    char *result = strdup(svg);
    duk_pop(ctx);
    return result;
}

int load_js_file(duk_context *ctx, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open JS file: %s (%s)\n", filename, strerror(errno));
        duk_push_error_object(ctx, DUK_ERR_ERROR, "cannot open file: %s", filename);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(len + 1);
    if (!buf) {
        fclose(f);
        fprintf(stderr, "Memory allocation failed for JS file: %s\n", filename);
        duk_push_error_object(ctx, DUK_ERR_ERROR, "memory allocation failed");
        return -1;
    }

    fread(buf, 1, len, f);
    buf[len] = 0;
    fclose(f);

    int ret = duk_peval_string(ctx, buf);
    if (ret != 0) {
        fprintf(stderr, "Failed to evaluate JS file %s: %s\n", filename, duk_safe_to_string(ctx, -1));
    }
    free(buf);
    return ret;
}
