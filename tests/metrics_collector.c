#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>

#define SAMPLE_INTERVAL 1  // интервал сбора метрик в секундах
#define MAX_LINE_LENGTH 512

typedef struct {
    char output_file[256];
    pid_t target_pid;
    volatile int running;
} MetricsCollector;

// Структура для хранения метрик
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
    
    unsigned long utime, stime, vsize, rss;
    int num_threads;
    
    sscanf(ptr, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %d %*d %*u %lu %ld",
           &utime, &stime, &num_threads, &vsize, &rss);
    
    metrics->cpu_time_user = utime;
    metrics->cpu_time_system = stime;
    metrics->mem_vsz_kb = vsize / 1024;
    metrics->mem_rss_kb = rss * (sysconf(_SC_PAGESIZE) / 1024);
    metrics->num_threads = num_threads;
    
    return 0;
}

// Вычисление процента CPU
static double calculate_cpu_percent(unsigned long prev_total, unsigned long curr_total, 
                                    double elapsed_sec) {
    unsigned long delta = curr_total - prev_total;
    long hz = sysconf(_SC_CLK_TCK);
    return (delta * 100.0) / (hz * elapsed_sec);
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

// Подсчет открытых файловых дескрипторов
static int count_fds(pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/fd", pid);
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "ls %s 2>/dev/null | wc -l", path);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;
    
    int count = 0;
    fscanf(fp, "%d", &count);
    pclose(fp);
    return count;
}

// Сбор всех метрик
static int collect_metrics(pid_t pid, ProcessMetrics *metrics, 
                          ProcessMetrics *prev_metrics, double elapsed_sec) {
    metrics->timestamp = time(NULL);
    
    if (get_cpu_mem_metrics(pid, metrics) != 0) return -1;
    if (get_io_metrics(pid, metrics) != 0) return -1;
    
    metrics->num_fds = count_fds(pid);
    
    // Вычисление процента CPU
    if (prev_metrics) {
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
    fprintf(f, "%ld,%.2f,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%d,%d\n",
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
            metrics->num_fds);
    fflush(f);
}

// Обработчик сигналов для graceful shutdown
static void signal_handler(int _) {
    collector.running = 0;
}

// Основной цикл сбора метрик
static void *collector_thread(void *_) {
    FILE *output = fopen(collector.output_file, "w");
    if (!output) {
        fprintf(stderr, "Failed to open output file: %s\n", collector.output_file);
        return NULL;
    }
    
    // Записываем заголовок CSV
    fprintf(output, "timestamp,cpu_percent,cpu_user,cpu_system,mem_rss_kb,mem_vsz_kb,"
                   "io_read_bytes,io_write_bytes,io_read_ops,io_write_ops,threads,fds\n");
    fflush(output);
    
    ProcessMetrics current, previous;
    memset(&previous, 0, sizeof(previous));
    int first_sample = 1;
    
    struct timespec prev_time, curr_time;
    clock_gettime(CLOCK_MONOTONIC, &prev_time);
    
    while (collector.running) {
        sleep(SAMPLE_INTERVAL);
        
        clock_gettime(CLOCK_MONOTONIC, &curr_time);
        double elapsed = (curr_time.tv_sec - prev_time.tv_sec) + 
                        (curr_time.tv_nsec - prev_time.tv_nsec) / 1e9;
        
        if (collect_metrics(collector.target_pid, &current, 
                           first_sample ? NULL : &previous, elapsed) == 0) {
            write_metrics_to_csv(output, &current);
            previous = current;
            first_sample = 0;
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
        fprintf(stderr, "Usage: %s <pid> <output_csv_file>\n", argv[0]);
        fprintf(stderr, "Example: %s 12345 metrics.csv\n", argv[0]);
        return 1;
    }
    
    collector.target_pid = atoi(argv[1]);
    strncpy(collector.output_file, argv[2], sizeof(collector.output_file) - 1);
    collector.running = 1;
    
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
    printf("Sample interval: %d second(s)\n", SAMPLE_INTERVAL);
    printf("Press Ctrl+C to stop\n\n");
    
    pthread_t thread;
    pthread_create(&thread, NULL, collector_thread, NULL);
    pthread_join(thread, NULL);
    
    printf("\nMetrics collection stopped. Results saved to %s\n", collector.output_file);
    return 0;
}