#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>
#include <dirent.h>

#define MAX_LINE_LENGTH 512

typedef struct {
    char output_file[256];
    pid_t target_pid;
    int sample_interval;
    volatile int running;
} MetricsCollector;

typedef struct {
    time_t timestamp;
    // CPU метрики
    double cpu_percent;
    unsigned long cpu_time_user;
    unsigned long cpu_time_system;
    // Memory метрики
    unsigned long mem_rss_kb;
    unsigned long mem_vsz_kb;
    // IO метрики
    unsigned long io_read_bytes;
    unsigned long io_write_bytes;
    unsigned long io_read_ops;
    unsigned long io_write_ops;
    // Thread метрики
    int num_threads;
    // File descriptor метрики
    int num_fds;
    // Context switch метрики
    unsigned long ctx_switches_voluntary;
    unsigned long ctx_switches_involuntary;
    // Page fault метрики
    unsigned long page_faults_minor;
    unsigned long page_faults_major;
} ProcessMetrics;

static MetricsCollector collector;

// Получение метрик CPU и памяти из /proc/[pid]/stat
static int get_cpu_mem_metrics(pid_t pid, ProcessMetrics *metrics) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[MAX_LINE_LENGTH];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    
    // Парсинг /proc/[pid]/stat
    char *ptr = strrchr(line, ')');
    if (!ptr) return -1;
    ptr += 2;
    
    unsigned long utime, stime, vsize, rss, minflt, majflt;
    int num_threads;
    
    // Формат: state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime...
    sscanf(ptr, "%*c %*d %*d %*d %*d %*d %*u %lu %*u %lu %*u %lu %lu %*d %*d %*d %*d %d %*d %*u %lu %ld",
           &minflt, &majflt, &utime, &stime, &num_threads, &vsize, &rss);
    
    metrics->cpu_time_user = utime;
    metrics->cpu_time_system = stime;
    metrics->mem_vsz_kb = vsize / 1024;
    metrics->mem_rss_kb = rss * (sysconf(_SC_PAGESIZE) / 1024);
    metrics->num_threads = num_threads;
    metrics->page_faults_minor = minflt;
    metrics->page_faults_major = majflt;
    
    return 0;
}

// Вычисление процента CPU
static double calculate_cpu_percent(unsigned long prev_total, unsigned long curr_total, 
                                    double elapsed_sec) {
    unsigned long delta = curr_total - prev_total;
    long hz = sysconf(_SC_CLK_TCK);
    double cpu_percent = (delta * 100.0) / (hz * elapsed_sec);
    return cpu_percent;
}

// Получение IO метрик из /proc/[pid]/io
static int get_io_metrics(pid_t pid, ProcessMetrics *metrics) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/io", pid);
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "read_bytes:", 11) == 0) {
            sscanf(line + 12, "%lu", &metrics->io_read_bytes);
        } else if (strncmp(line, "write_bytes:", 12) == 0) {
            sscanf(line + 13, "%lu", &metrics->io_write_bytes);
        } else if (strncmp(line, "syscr:", 6) == 0) {
            sscanf(line + 7, "%lu", &metrics->io_read_ops);
        } else if (strncmp(line, "syscw:", 6) == 0) {
            sscanf(line + 7, "%lu", &metrics->io_write_ops);
        }
    }
    fclose(f);
    return 0;
}

// Получение context switches из /proc/[pid]/status
static int get_context_switches(pid_t pid, ProcessMetrics *metrics) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "voluntary_ctxt_switches:", 24) == 0) {
            sscanf(line + 25, "%lu", &metrics->ctx_switches_voluntary);
        } else if (strncmp(line, "nonvoluntary_ctxt_switches:", 27) == 0) {
            sscanf(line + 28, "%lu", &metrics->ctx_switches_involuntary);
        }
    }
    fclose(f);
    return 0;
}

// Подсчет открытых файловых дескрипторов через readdir
static int count_fds(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/fd", pid);
    
    DIR *dir = opendir(path);
    if (!dir) return 0;
    
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    closedir(dir);
    return count;
}

// Сбор всех метрик
static int collect_metrics(pid_t pid, ProcessMetrics *metrics, 
                          ProcessMetrics *prev_metrics, double elapsed_sec) {
    metrics->timestamp = time(NULL);
    
    if (get_cpu_mem_metrics(pid, metrics) != 0) return -1;
    if (get_io_metrics(pid, metrics) != 0) return -1;
    if (get_context_switches(pid, metrics) != 0) return -1;
    
    metrics->num_fds = count_fds(pid);
    
    // Вычисление процента CPU
    if (prev_metrics && elapsed_sec > 0) {
        unsigned long prev_total = prev_metrics->cpu_time_user + prev_metrics->cpu_time_system;
        unsigned long curr_total = metrics->cpu_time_user + metrics->cpu_time_system;
        metrics->cpu_percent = calculate_cpu_percent(prev_total, curr_total, elapsed_sec);
    } else {
        metrics->cpu_percent = 0.0;
    }
    
    return 0;
}

// Запись метрик в CSV файл
static void write_metrics_to_csv(FILE *f, ProcessMetrics *metrics) {
    fprintf(f, "%ld,%.2f,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d,%lu,%lu,%lu,%lu\n",
            metrics->timestamp,
            metrics->cpu_percent,
            metrics->cpu_time_user,
            metrics->cpu_time_system,
            metrics->mem_rss_kb,
            metrics->mem_vsz_kb,
            metrics->io_read_bytes,
            metrics->io_write_bytes,
            metrics->io_read_ops,
            metrics->io_write_ops,
            metrics->num_threads,
            metrics->num_fds,
            metrics->ctx_switches_voluntary,
            metrics->ctx_switches_involuntary,
            metrics->page_faults_minor,
            metrics->page_faults_major);
    fflush(f);
}

// Обработчик сигналов для graceful shutdown
static void signal_handler(int _) {
    (void)_;
    collector.running = 0;
}

// Основной цикл сбора метрик
static void *collector_thread(void *_) {
    (void)_;
    FILE *output = fopen(collector.output_file, "w");
    if (!output) {
        fprintf(stderr, "Failed to open output file: %s\n", collector.output_file);
        return NULL;
    }
    
    // Записываем заголовок CSV
    fprintf(output, "timestamp,cpu_percent,cpu_user,cpu_system,mem_rss_kb,mem_vsz_kb,"
                   "io_read_bytes,io_write_bytes,io_read_ops,io_write_ops,threads,fds,"
                   "ctx_switches_voluntary,ctx_switches_involuntary,page_faults_minor,page_faults_major\n");
    fflush(output);
    
    ProcessMetrics current, previous;
    memset(&previous, 0, sizeof(previous));
    memset(&current, 0, sizeof(current));
    
    struct timespec prev_time, curr_time;
    clock_gettime(CLOCK_MONOTONIC, &prev_time);
    
    // Делаем первый замер для инициализации
    if (collect_metrics(collector.target_pid, &previous, NULL, 0) != 0) {
        fprintf(stderr, "Failed to collect initial metrics\n");
        fclose(output);
        return NULL;
    }
    
    while (collector.running) {
        sleep(collector.sample_interval);
        
        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        double elapsed = (curr_time.tv_sec - prev_time.tv_sec) + 
                        (curr_time.tv_nsec - prev_time.tv_nsec) / 1e9;
        
        if (collect_metrics(collector.target_pid, &current, &previous, elapsed) == 0) {
            write_metrics_to_csv(output, &current);
            previous = current;
            prev_time = curr_time;
        } else {
            fprintf(stderr, "Failed to collect metrics (process may have died)\n");
            break;
        }
    }
    
    fclose(output);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <pid> <output_csv_file> [sample_interval_sec]\n", argv[0]);
        fprintf(stderr, "Example: %s 12345 metrics.csv 1\n", argv[0]);
        return 1;
    }
    
    collector.target_pid = atoi(argv[1]);
    strncpy(collector.output_file, argv[2], sizeof(collector.output_file) - 1);
    collector.sample_interval = (argc >= 4) ? atoi(argv[3]) : 1;
    collector.running = 1;
    
    if (collector.sample_interval < 1) {
        collector.sample_interval = 1;
    }
    
    // Проверяем существование процесса
    if (kill(collector.target_pid, 0) != 0) {
        fprintf(stderr, "Process %d does not exist or not accessible\n", collector.target_pid);
        return 1;
    }
    
    // Устанавливаем обработчики сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Collecting metrics for PID %d\n", collector.target_pid);
    printf("Output file: %s\n", collector.output_file);
    printf("Sample interval: %d second(s)\n", collector.sample_interval);
    printf("Press Ctrl+C to stop\n\n");
    
    pthread_t thread;
    pthread_create(&thread, NULL, collector_thread, NULL);
    pthread_join(thread, NULL);
    
    printf("\nMetrics collection stopped. Results saved to %s\n", collector.output_file);
    return 0;
}