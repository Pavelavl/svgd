/**
 * @file path_util.c
 * @brief Реализация утилит построения путей к RRD-файлам
 *
 * Функции перенесены из handler.c без изменения поведения (ранее были static);
 * вынесены в отдельный модуль для тестируемости.
 */

#include "../include/path_util.h"
#include <stdio.h>
#include <string.h>

/**
 * Extract parameter from endpoint path
 * e.g., "cpu/process/nginx" with endpoint "cpu/process" -> "nginx"
 */
char* extract_param_from_path(const char *path, const char *endpoint) {
    if (!path || !endpoint) return NULL;

    size_t endpoint_len = strlen(endpoint);
    if (strncmp(path, endpoint, endpoint_len) != 0) {
        return NULL;
    }

    const char *param_start = path + endpoint_len;
    if (*param_start == '/') param_start++;

    if (*param_start == '\0') return NULL;

    return strdup(param_start);
}

/**
 * Build RRD file path from template
 */
void build_rrd_path(char *dest, size_t dest_size, const char *base_path,
                    const char *path_template, const char *param) {
    if (!dest || dest_size == 0) return;

    if (strchr(path_template, '%') && param) {
        snprintf(dest, dest_size, "%s/", base_path);
        snprintf(dest + strlen(dest), dest_size - strlen(dest), path_template, param);
    } else {
        snprintf(dest, dest_size, "%s/%s", base_path, path_template);
    }
}
