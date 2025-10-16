#ifndef CONFIG_H
#define CONFIG_H

#include <string.h>
#include <stdlib.h>
#include <duktape.h>

typedef struct {
    int tcp_port;
    char allowed_ips[1024];
    char rrd_base_path[256];
    char js_script_path[256];
    char path_cpu_total[256];
    char path_cpu_process[256];
    char path_ram_total[256];
    char path_ram_process[256];
    char path_network[256];
    char path_disk[256];
    char path_postgresql_connections[256];
} Config;

Config load_config(duk_context *ctx, const char *filename);

#endif