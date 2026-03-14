# ============================================================
# SVGD Makefile
# ============================================================

# === Configuration ===
CC       = gcc
CFLAGS   = -Ilsrp -Wall -Wextra -O2 -pthread
LIBS     = -lrrd -lduktape

LSRP_DIR    = lsrp
BIN_DIR     = bin
EXAMPLES_DIR = examples

SERVER_SRC = src/main.c src/cfg.c src/http.c src/handler.c src/rrd/reader.c src/rrd/cache.c src/rrd/svg.c $(LSRP_DIR)/lsrp_server.c
SERVER_BIN = svgd
GATE_SRC   = gate/*.c $(LSRP_DIR)/lsrp_client.c
GATE_BIN   = svgd-gate
CLIENT_BIN = $(LSRP_DIR)/bin/lsrp

PORT   := $(shell jq -r '.server.tcp_port // "8081"' config.json)
PERIOD = 3600

SVG_FILES = \
	$(EXAMPLES_DIR)/cpu.svg \
	$(EXAMPLES_DIR)/cpu_process_postgres.svg \
	$(EXAMPLES_DIR)/ram.svg \
	$(EXAMPLES_DIR)/ram_process_postgres.svg \
	$(EXAMPLES_DIR)/network.svg \
	$(EXAMPLES_DIR)/disk.svg \
	$(EXAMPLES_DIR)/pgsql.svg

# === Phony Targets ===
.PHONY: all build build-backend clean install
.PHONY: run run-backend generate
.PHONY: test test-all test-e2e test-load test-ui test-comparison
.PHONY: report generate-report generate-charts clean-results
.PHONY: docker-build docker-up docker-down docker-logs docker-test docker-test-ui
.PHONY: docker-bases svgd-base collectd-base
.PHONY: run-multi down-multi
.PHONY: bench-svgd-only bench-comparison bench-charts bench-all bench-quick bench-clean
.PHONY: bench-docker-build bench-docker-up bench-docker-down
.PHONY: demo demo-down submodule

# ============================================================
# BUILD
# ============================================================

all: build

build: build-backend
	$(CC) -o $(BIN_DIR)/$(GATE_BIN) $(GATE_SRC) -g $(CFLAGS)

build-backend:
	@mkdir -p $(BIN_DIR)
	$(CC) -o $(BIN_DIR)/$(SERVER_BIN) $(SERVER_SRC) -g $(CFLAGS) $(LIBS)

clean:
	rm -f $(BIN_DIR)/$(SERVER_BIN) $(BIN_DIR)/$(GATE_BIN) $(CLIENT_BIN) $(SVG_FILES)
	rmdir $(EXAMPLES_DIR) 2>/dev/null || true

# ============================================================
# RUN
# ============================================================

run: build
	./$(BIN_DIR)/$(GATE_BIN) 127.0.0.1 $(PORT) 8080 ./gate/static

run-backend: build-backend
	./$(BIN_DIR)/$(SERVER_BIN) ./config.json

generate:
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=cpu&period=$(PERIOD)" > examples/cpu.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=cpu/process/systemd&period=$(PERIOD)" > examples/cpu_process_systemd.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=ram&period=$(PERIOD)" > examples/ram.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=ram/process/systemd&period=$(PERIOD)" > examples/ram_process_systemd.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=network/wlp2s0&period=$(PERIOD)" > examples/network.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=disk/nvme0n1&period=$(PERIOD)" > examples/disk.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=postgresql/connections&period=$(PERIOD)" > examples/pgsql.svg

# ============================================================
# TEST
# ============================================================

test: test-e2e test-load

test-all: test-e2e test-load test-comparison
	@echo "All tests completed"

test-e2e:
	cd tests && go test -v ./internal/e2e/...

test-load:
	cd tests && go test -v ./internal/load/...

test-comparison: test-bench

test-ui:
	./tests/internal/ui/run_tests.sh -v

test-deps:
	@if [ ! -d "tests/venv" ]; then \
		echo "Creating virtual environment..."; \
		python3 -m venv tests/venv; \
		tests/venv/bin/pip install -q -r tests/requirements.txt; \
	fi

# ============================================================
# REPORT GENERATION
# ============================================================

# Full report cycle: tests -> charts -> markdown
report: test-all generate-charts generate-report
	@echo "Report generated: tests/results/report.md"
	@echo "Charts: tests/results/charts/output/"

# Merge results from multiple machines
# Usage: make merge-results SOURCES="machine-a/results machine-b/results"
merge-results:
	@if [ -z "$(SOURCES)" ]; then \
		echo "Usage: make merge-results SOURCES=\"machine-a/results machine-b/results\""; \
		exit 1; \
	fi
	cd tests/pkg/system && go run ./cmd/merge $(SOURCES) ../../results

# Generate charts (Python)
generate-charts:
	@if [ ! -d "tests/venv" ]; then \
		echo "Creating virtual environment..."; \
		python3 -m venv tests/venv; \
		tests/venv/bin/pip install -q -r tests/requirements.txt; \
	fi
	tests/venv/bin/python tests/results/charts/generate.py

# Generate markdown report (Go)
generate-report:
	cd tests/pkg/system && go run ./cmd/reportgen

# Clean all results
clean-results:
	rm -f tests/results/*.csv tests/results/report.md
	rm -f tests/results/machines/*.json
	rm -f tests/results/charts/output/*.png

# ============================================================
# CROSS-SYSTEM BENCHMARK (Legacy)
# ============================================================

test-bench: build-backend bench-docker-build bench-docker-up
	@echo "=== Cross-System Benchmark (svgd vs RRDtool vs Graphite) ==="
	cd tests/internal/comparison && go run -tags docker .

bench-docker-build:
	docker build -t benchmark-rrdtool tests/internal/comparison/docker/rrdtool/
	docker build -t benchmark-graphite tests/internal/comparison/docker/graphite/

bench-docker-up:
	docker rm -f benchmark-rrdtool benchmark-graphite 2>/dev/null || true
	docker run -d --name benchmark-rrdtool -p 8083:8080 \
		-v /opt/collectd/var/lib/collectd/rrd:/var/lib/collectd/rrd:ro benchmark-rrdtool
	docker run -d --name benchmark-graphite -p 8082:80 benchmark-graphite

bench-docker-down:
	docker rm -f benchmark-rrdtool benchmark-graphite 2>/dev/null || true

bench-all: bench-comparison bench-charts

# --- Charts ---
bench-charts: generate-charts
	@echo "Charts generated in tests/results/charts/output/"

# --- Clean ---
bench-clean: bench-docker-down clean-results

# ============================================================
# DOCKER (Basic)
# ============================================================

docker-build:
	docker compose build

docker-up:
	docker compose up -d

docker-down:
	docker compose down

docker-logs:
	docker compose logs -f

docker-test:
	docker compose --profile test run --rm test-runner make test

docker-test-ui:
	docker compose --profile test run --rm test-runner make test-ui

# ============================================================
# DOCKER (Multi-Datasource Demo)
# ============================================================

svgd-base:
	docker build -f Dockerfile.base -t svgd-base:latest .

collectd-base:
	docker build -f .infra/collectd/Dockerfile.base -t svgd-collectd-base:latest .infra/collectd/

docker-bases: svgd-base collectd-base

run-multi: docker-bases
	docker-compose -f docker-compose.multi.yml up --build

down-multi:
	docker-compose -f docker-compose.multi.yml down
