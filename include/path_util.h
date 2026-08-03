/**
 * @file path_util.h
 * @brief Утилиты построения путей к RRD-файлам
 *
 * Чистые функции манипуляции путями, выделенные из handler.c в отдельную
 * единицу трансляции для независимого unit-тестирования (без зависимости от
 * Duktape, RRD и контекста обработчика).
 */

#ifndef SVGD_PATH_UTIL_H
#define SVGD_PATH_UTIL_H

#include <stddef.h>

/**
 * @brief Извлечь параметр из пути endpoint'а
 *
 * Напр., path="cpu/process/nginx" с endpoint="cpu/process" -> "nginx".
 * Параметром считается всё после префикса endpoint и следующего за ним '/'
 * (сам '/' отбрасывается). Если path совпадает с endpoint без параметра
 * или endpoint не является префиксом — возвращается NULL.
 *
 * @param path Запрошенный путь (напр. "network/eth0")
 * @param endpoint Шаблон endpoint'а из конфига (напр. "network")
 * @return malloc'd строка с параметром (вызывающий освобождает free) или NULL
 */
char* extract_param_from_path(const char *path, const char *endpoint);

/**
 * @brief Собрать путь к RRD-файлу по шаблону
 *
 * Если path_template содержит '%' и задан param, шаблон трактуется как
 * printf-формат с единственным %s, куда подставляется param. Иначе путь
 * собирается как base_path/path_template без форматирования (param игнорируется).
 *
 * @param dest Буфер-приёмник
 * @param dest_size Размер буфера dest
 * @param base_path Базовый путь к RRD-хранилищу
 * @param path_template Шаблон (возможно с %s) из конфига метрики
 * @param param Параметр для подстановки (может быть NULL)
 */
void build_rrd_path(char *dest, size_t dest_size, const char *base_path,
                    const char *path_template, const char *param);

#endif /* SVGD_PATH_UTIL_H */
