#include "../inih/ini.h"
#include "../include/cfg.h"

static int handler(void *user, const char *section, const char *name,
                   const char *value) {
  Config *config = (Config *)user;
  if (strcmp(section, "server") == 0) {
    if (strcmp(name, "tcp_port") == 0)
      config->tcp_port = atoi(value);
    else if (strcmp(name, "rrdcached_addr") == 0)
      strncpy(config->rrdcached_addr, value, sizeof(config->rrdcached_addr));
    else if (strcmp(name, "allowed_ips") == 0)
      strncpy(config->allowed_ips, value, sizeof(config->allowed_ips));
  } else if (strcmp(section, "rrd") == 0) {
    if (strcmp(name, "base_path") == 0)
      strncpy(config->rrd_base_path, value, sizeof(config->rrd_base_path));
    else if (strcmp(name, "cpu_total") == 0)
      strncpy(config->path_cpu_total, value, sizeof(config->path_cpu_total));
    else if (strcmp(name, "cpu_process") == 0)
      strncpy(config->path_cpu_process, value,
              sizeof(config->path_cpu_process));
    else if (strcmp(name, "ram_total") == 0)
      strncpy(config->path_ram_total, value, sizeof(config->path_ram_total));
    else if (strcmp(name, "ram_process") == 0)
      strncpy(config->path_ram_process, value,
              sizeof(config->path_ram_process));
    else if (strcmp(name, "network") == 0)
      strncpy(config->path_network, value, sizeof(config->path_network));
    else if (strcmp(name, "disk") == 0)
      strncpy(config->path_disk, value, sizeof(config->path_disk));
    else if (strcmp(name, "postgresql_connections") == 0)
      strncpy(config->path_postgresql_connections, value,
              sizeof(config->path_postgresql_connections));
  } else if (strcmp(section, "js") == 0) {
    if (strcmp(name, "script_path") == 0)
      strncpy(config->js_script_path, value, sizeof(config->js_script_path));
  }

  return 1;
}

Config load_config(const char *filename) {
  Config config = {
      .tcp_port = 8080,
      .rrdcached_addr = "unix:/var/run/rrdcached.sock",
      .allowed_ips = "127.0.0.1",
      .rrd_base_path = "/opt/collectd/var/lib/collectd/rrd/localhost",
      .js_script_path = "/home/workerpool/svgd/scripts/generate_cpu_svg.js",
      .path_cpu_total = "cpu-total/percent-active.rrd",
      .path_cpu_process = "processes-%s/ps_cputime.rrd",
      .path_ram_total = "memory/percent-used.rrd",
      .path_ram_process = "processes-%s/ps_rss.rrd",
      .path_network = "interface-%s/if_octets.rrd",
      .path_disk = "disk-%s/disk_ops.rrd",
      .path_postgresql_connections = "postgresql-iqc_stand1/backends.rrd"};

  if (ini_parse(filename, handler, &config) < 0) {
    fprintf(stderr, "Warning: Using default configuration\n");
  }
  return config;
}