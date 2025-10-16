#include "../include/cfg.h"
#include <stdio.h>
#include <string.h>

static void set_string_field(duk_context *ctx, Config *config, const char *section, const char *field, char *dest, size_t dest_size) {
    duk_get_prop_string(ctx, -1, section);
    if (duk_is_object(ctx, -1)) {
        duk_get_prop_string(ctx, -1, field);
        if (duk_is_string(ctx, -1)) {
            strncpy(dest, duk_get_string(ctx, -1), dest_size - 1);
            dest[dest_size - 1] = '\0';
        } else {
            fprintf(stderr, "Warning: %s.%s is not a string in config.json\n", section, field);
        }
        duk_pop(ctx);
    } else {
        fprintf(stderr, "Warning: %s section is not an object in config.json\n", section);
    }
    duk_pop(ctx);
}

Config load_config(duk_context *ctx, const char *filename) {
    Config config = {
        .tcp_port = 8080,
        .allowed_ips = "127.0.0.1",
        .rrd_base_path = "/opt/collectd/var/lib/collectd/rrd/localhost",
        .js_script_path = "/home/workerpool/svgd/scripts/generate_cpu_svg.js",
        .path_cpu_total = "cpu-total/percent-active.rrd",
        .path_cpu_process = "processes-%s/ps_cputime.rrd",
        .path_ram_total = "memory/percent-used.rrd",
        .path_ram_process = "processes-%s/ps_rss.rrd",
        .path_network = "interface-%s/if_octets.rrd",
        .path_disk = "disk-%s/disk_ops.rrd",
        .path_postgresql_connections = "postgresql-iqchannels/pg_numbackends.rrd"
    };

    // Чтение JSON-файла
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Warning: Cannot open config file %s, using default configuration\n", filename);
        return config;
    }

    // Определяем размер файла
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_code = malloc(fsize + 1);
    if (!json_code) {
        fprintf(stderr, "Error: Cannot allocate memory for config file\n");
        fclose(f);
        return config;
    }

    fread(json_code, 1, fsize, f);
    json_code[fsize] = '\0';
    fclose(f);

    // Сохраняем текущий индекс стека
    duk_idx_t top = duk_get_top(ctx);

    // Помещаем JSON-строку на стек
    duk_push_string(ctx, json_code);
    free(json_code);

    // Парсинг JSON
    duk_json_decode(ctx, -1);
    if (duk_is_error(ctx, -1)) {
        fprintf(stderr, "Error: Failed to parse config.json: %s\n", duk_safe_to_string(ctx, -1));
        duk_pop_n(ctx, duk_get_top(ctx) - top);
        return config;
    }

    // Проверяем, что на вершине стека объект
    if (!duk_is_object(ctx, -1)) {
        fprintf(stderr, "Error: config.json must contain an object\n");
        duk_pop_n(ctx, duk_get_top(ctx) - top);
        return config;
    }

    // Проверяем наличие обязательных секций
    if (!duk_has_prop_string(ctx, -1, "server") || !duk_has_prop_string(ctx, -1, "rrd") || !duk_has_prop_string(ctx, -1, "js")) {
        fprintf(stderr, "Error: config.json missing required sections (server, rrd, js)\n");
        duk_pop_n(ctx, duk_get_top(ctx) - top);
        return config;
    }

    // Извлекаем значения
    duk_get_prop_string(ctx, -1, "server");
    if (duk_is_object(ctx, -1)) {
        duk_get_prop_string(ctx, -1, "tcp_port");
        if (duk_is_number(ctx, -1)) {
            config.tcp_port = duk_get_int(ctx, -1);
        } else {
            fprintf(stderr, "Warning: server.tcp_port is not a number in config.json\n");
        }
        duk_pop(ctx);

        set_string_field(ctx, &config, "server", "allowed_ips", config.allowed_ips, sizeof(config.allowed_ips));
    }
    duk_pop(ctx);

    set_string_field(ctx, &config, "rrd", "base_path", config.rrd_base_path, sizeof(config.rrd_base_path));
    set_string_field(ctx, &config, "rrd", "cpu_total", config.path_cpu_total, sizeof(config.path_cpu_total));
    set_string_field(ctx, &config, "rrd", "cpu_process", config.path_cpu_process, sizeof(config.path_cpu_process));
    set_string_field(ctx, &config, "rrd", "ram_total", config.path_ram_total, sizeof(config.path_ram_total));
    set_string_field(ctx, &config, "rrd", "ram_process", config.path_ram_process, sizeof(config.path_ram_process));
    set_string_field(ctx, &config, "rrd", "network", config.path_network, sizeof(config.path_network));
    set_string_field(ctx, &config, "rrd", "disk", config.path_disk, sizeof(config.path_disk));
    set_string_field(ctx, &config, "rrd", "postgresql_connections", config.path_postgresql_connections, sizeof(config.path_postgresql_connections));
    set_string_field(ctx, &config, "js", "script_path", config.js_script_path, sizeof(config.js_script_path));

    duk_pop_n(ctx, duk_get_top(ctx) - top); // Очищаем стек
    return config;
}