# svgd

<img src="examples/menu.png" width="600"/>

Легковесная система мониторинга с динамической конфигурацией метрик.

**Компоненты:**
- **svgd** — LSRP-сервер для генерации SVG-графиков из RRD-файлов
- **svgd-gate** — HTTP-шлюз с веб-интерфейсом
- **collectd** — сбор системных метрик (внешний компонент)

---

## Установка

### Требования

- **librrd-dev** — библиотека для работы с RRD-файлами
- **libduktape-dev** — JS-движок Duktape для генерации SVG
- **gcc** — компилятор C
- **collectd** — сбор системных метрик
- **jq** — (опционально) для парсинга config.json в Makefile

### Установка зависимостей

```bash
sudo apt update
sudo apt install librrd-dev libduktape-dev gcc jq
```

### Сборка

```bash
# Сборка всех компонентов
make build

# Только backend (LSRP-сервер)
make build-backend
```

### Запуск

```bash
# HTTP-шлюз с веб-интерфейсом (порт 8080)
make run

# Только LSRP-сервер (порт из config.json, по умолчанию 8081)
make run-backend
```

После запуска откройте http://localhost:8080 в браузере.

---

## Конфигурация

### Структура config.json

```json
{
  "server": {
    "tcp_port": 8081,
    "protocol": "lsrp",
    "allowed_ips": "127.0.0.1",
    "rrdcached_addr": "",
    "thread_pool_size": 4,
    "cache_ttl_seconds": 5,
    "verbose": 0
  },
  "rrd": {
    "base_path": "/opt/collectd/var/lib/collectd/rrd/localhost"
  },
  "js": {
    "script_path": "./src/scripts/generate_cpu_svg.js"
  },
  "metrics": [ /* массив метрик */ ]
}
```

### Параметры сервера

| Параметр | Описание | По умолчанию |
|----------|----------|--------------|
| `tcp_port` | Порт LSRP-сервера | 8081 |
| `allowed_ips` | Разрешённые IP (через запятую) | 127.0.0.1 |
| `rrdcached_addr` | Адрес rrdcached (unix:/path или host:port) | "" |
| `thread_pool_size` | Размер пула потоков | 4 |
| `cache_ttl_seconds` | TTL кэша RRD-данных | 5 |
| `verbose` | Уровень логирования | 0 |

### Конфигурация метрик

#### Обязательные параметры

| Параметр | Описание |
|----------|----------|
| `endpoint` | URL путь для доступа |
| `rrd_path` | Путь к RRD-файлу (относительно base_path) |

#### Опциональные параметры

| Параметр | Описание |
|----------|----------|
| `requires_param` | Требуется ли параметр в URL |
| `param_name` | Имя параметра (для UI) |
| `title` | Заголовок графика (поддерживает `%s`) |
| `y_label` | Подпись оси Y |
| `is_percentage` | Метрика в процентах (0-100%) |
| `transform_type` | Тип трансформации: `none`, `divide`, `sum` |
| `transform_divisor` | Делитель для трансформации |
| `value_format` | Формат вывода (printf-style) |

#### Примеры метрик

**CPU (простая метрика):**
```json
{
  "endpoint": "cpu",
  "rrd_path": "cpu-total/percent-active.rrd",
  "title": "CPU Utilization",
  "y_label": "Usage (%)",
  "is_percentage": true,
  "value_format": "%.1f"
}
```

**Память процесса (с параметром и трансформацией):**
```json
{
  "endpoint": "ram/process",
  "rrd_path": "processes-%s/ps_rss.rrd",
  "requires_param": true,
  "param_name": "process_name",
  "title": "Memory Usage",
  "y_label": "Memory (MB)",
  "transform_type": "divide",
  "transform_divisor": 1048576,
  "value_format": "%.1f"
}
```

**Сеть (с параметром):**
```json
{
  "endpoint": "network",
  "rrd_path": "interface-%s/if_octets.rrd",
  "requires_param": true,
  "param_name": "interface",
  "title": "Network Traffic",
  "y_label": "Traffic (Mbit/s)",
  "transform_type": "divide",
  "transform_divisor": 125000,
  "value_format": "%.2f"
}
```

---

## API

### HTTP REST API (svgd-gate)

**Получить список метрик:**
```bash
GET http://localhost:8080/_config/metrics
```

**Получить SVG-график:**
```bash
GET http://localhost:8080/<endpoint>?period=<seconds>
```

Параметр `period` — временной диапазон в секундах (по умолчанию 3600).

**Примеры:**
```bash
# CPU за последний час
curl http://localhost:8080/cpu

# CPU за 24 часа
curl http://localhost:8080/cpu?period=86400

# Память процесса
curl http://localhost:8080/ram/process/postgres

# Сетевой трафик
curl http://localhost:8080/network/eth0?period=7200
```

### LSRP Protocol API

```bash
./lsrp/bin/lsrp localhost:8081 "endpoint=<endpoint>&period=<seconds>"
```

**Примеры:**
```bash
./lsrp/bin/lsrp localhost:8081 "endpoint=cpu&period=3600"
./lsrp/bin/lsrp localhost:8081 "endpoint=ram/process/systemd&period=7200"
```

---

## Доступные метрики

| Endpoint | Описание | Параметр | Пример |
|----------|----------|----------|--------|
| `cpu` | Загрузка CPU (%) | — | [cpu.svg](examples/cpu.svg) |
| `cpu/process/<name>` | CPU time процесса | process_name | [cpu_process_systemd.svg](examples/cpu_process_systemd.svg) |
| `ram` | Использование памяти (%) | — | [ram.svg](examples/ram.svg) |
| `ram/process/<name>` | Память процесса (MB) | process_name | [ram_process_systemd.svg](examples/ram_process_systemd.svg) |
| `network/<iface>` | Сетевой трафик (Mbit/s) | interface | [network.svg](examples/network.svg) |
| `disk/<disk>` | Дисковые операции (ops/s) | disk | [disk.svg](examples/disk.svg) |
| `postgresql/connections` | Подключения к PostgreSQL | — | [pgsql.svg](examples/pgsql.svg) |
| `system/load` | Load average (1/5/15min) | — | — |
| `system/uptime` | Uptime системы (hours) | — | — |
| `swap/bytes` | Использование swap (MB) | — | — |
| `filesystem/<mount>` | Использование ФС (GB) | mount_point | — |

---

## Веб-интерфейс

### Возможности

- Динамическое добавление панелей из списка метрик
- Настройка временного диапазона (5 мин — 7 дней)
- Автообновление с настраиваемым интервалом
- Поиск и фильтрация панелей
- Полноэкранный режим
- Экспорт SVG-графиков
- Светлая и тёмная тема
- Экспорт/импорт конфигурации дашборда
- Снимки дашборда (HTML со всеми графиками)

### Горячие клавиши

- `Esc` — закрыть модальные окна

---

## Производительность

### Результаты нагрузочных тестов

**Конфигурация теста:** 1000 запросов, период 3600 сек (1 час данных)

#### Последовательные запросы (concurrency=1)

| Протокол | RRDCached | RPS | Latency (avg) | P95 | Память |
|----------|-----------|-----|---------------|-----|--------|
| LSRP | No | 130 | 7.7 ms | 10.0 ms | 11.6 MB |
| HTTP | No | 123 | 8.1 ms | 10.4 ms | 11.6 MB |
| LSRP | Yes | 123 | 8.1 ms | 10.4 ms | 12.1 MB |
| HTTP | Yes | 131 | 7.6 ms | 9.7 ms | 11.2 MB |

#### Параллельные запросы (concurrency=10)

| Протокол | RRDCached | RPS | Latency (avg) | P95 | Память |
|----------|-----------|-----|---------------|-----|--------|
| LSRP | No | 156 | 64 ms | 74 ms | 12.3 MB |
| HTTP | No | 149 | 67 ms | 85 ms | 11.4 MB |
| LSRP | Yes | 174 | 57 ms | 65 ms | 12.2 MB |
| HTTP | Yes | 165 | 60 ms | 73 ms | 11.3 MB |

### E2E тесты (100 запросов)

| Протокол | Режим | Latency (median) | Min | Max |
|----------|-------|------------------|-----|-----|
| HTTP | sync | 7 ms | 6 ms | 29 ms |
| HTTP | parallel | 311 ms | 26 ms | 648 ms |
| LSRP | sync | 6 ms | 5 ms | 10 ms |
| LSRP | parallel | 308 ms | 15 ms | 621 ms |

### Выводы

- LSRP показывает на 5-15% лучшую пропускную способность при параллельной нагрузке
- RRDCached даёт прирост ~10-15% при параллельных запросах
- Потребление памяти стабильно: 11-12 MB
- CPU: ~45-50% при максимальной нагрузке (c=10)

---

## rrdcached

Для повышения производительности при работе с RRD-файлами:

```bash
sudo rrdcached -p /var/run/rrdcached.pid \
               -l unix:/var/run/rrdcached.sock \
               -B -F \
               -b /opt/collectd/var/lib/collectd/rrd \
               -j /var/lib/rrdcached/journal \
               -f 3600 -w 1800 -z 900
```

В `config.json`:
```json
"rrdcached_addr": "unix:/var/run/rrdcached.sock"
```

---

## collectd

Настройка collectd по [инструкции](https://github.com/Pavelavl/cpu-http-monitor).

Примеры конфигураций в [.infra/collectd/](.infra/collectd/):

```
collectd/
├── collectd.conf
└── collectd.conf.d/
    ├── cpu.conf
    ├── df.conf
    ├── disk.conf
    ├── load.conf
    ├── memory.conf
    ├── network.conf
    ├── postgresql.conf
    ├── processes.conf
    ├── swap.conf
    └── uptime.conf
```

---

## Тестирование

```bash
# Все тесты
make test

# E2E тесты
make test-e2e

# Нагрузочные тесты
make test-load

# UI тесты (требуется Python)
make test-ui
```

---

## Docker

```bash
# Сборка
make docker-build

# Запуск
make docker-up

# Логи
make docker-logs

# Остановка
make docker-down

# Тесты в Docker
make docker-test
```

---

## Структура проекта

```
svgd/
├── bin/                    # Бинарные файлы
│   ├── svgd                # LSRP-сервер
│   └── svgd-gate           # HTTP-шлюз
├── gate/                   # HTTP-шлюз
│   ├── main.c
│   └── static/             # Веб-интерфейс
│       ├── index.html
│       └── script.js
├── include/                # Заголовки
│   ├── cfg.h
│   ├── handler.h
│   ├── http.h
│   ├── rrd_r.h
│   └── rrd/
│       ├── cache.h
│       ├── reader.h
│       └── svg.h
├── src/                    # Исходники backend
│   ├── cfg.c
│   ├── handler.c
│   ├── http.c
│   ├── main.c
│   ├── rrd/
│   │   ├── cache.c
│   │   ├── reader.c
│   │   └── svg.c
│   └── scripts/
│       └── generate_cpu_svg.js
├── tests/                  # Тесты
│   ├── e2e/
│   ├── load/
│   ├── ui/
│   └── metrics_collector.c
├── lsrp/                   # LSRP протокол (submodule)
├── examples/               # Примеры графиков
├── .infra/                 # Инфраструктура
├── config.json             # Конфигурация
├── makefile
├── Dockerfile
└── docker-compose.yml
```

---

## Troubleshooting

### Сервер не запускается

```bash
# Проверка портов
ss -tulpn | grep -E '8080|8081'

# Проверка прав к RRD
ls -la /opt/collectd/var/lib/collectd/rrd/

# Запуск с логами
./bin/svgd ./config.json
```

### Графики не отображаются

```bash
# Проверка RRD-файлов
ls /opt/collectd/var/lib/collectd/rrd/localhost/

# Проверка API
curl http://localhost:8080/_config/metrics
curl http://localhost:8080/cpu
```

### Низкая производительность

1. Включите rrdcached в config.json
2. Увеличьте `thread_pool_size`
3. Уменьшите интервал автообновления в UI

---

## Связанные проекты

- [LSRP](https://github.com/pavelavl/lsrp) — Lightweight Simple Request Protocol
- [collectd](https://github.com/collectd/collectd) — System metrics collection
- [Duktape](https://github.com/svaarala/duktape) — Embedded JavaScript engine
