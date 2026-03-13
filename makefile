CC = gcc
CFLAGS = -Ilsrp -Wall -Wextra -O2 -pthread
LIBS = -lrrd -lduktape

LSRP_DIR = lsrp
BIN_DIR = bin
EXAMPLES_DIR = examples

SERVER_SRC = src/main.c src/cfg.c src/http.c src/handler.c src/rrd/reader.c src/rrd/cache.c src/rrd/svg.c $(LSRP_DIR)/lsrp_server.c
SERVER_BIN = svgd
HTTP_SRC = src/http.c
GATE_SRC = gate/*.c $(LSRP_DIR)/lsrp_client.c
GATE_BIN = svgd-gate

CLIENT_BIN = $(LSRP_DIR)/bin/lsrp
PORT := $(shell jq -r '.server.tcp_port // "8081"' config.json)
PERIOD = 3600

SVG_FILES = \
	$(EXAMPLES_DIR)/cpu.svg \
	$(EXAMPLES_DIR)/cpu_process_postgres.svg \
	$(EXAMPLES_DIR)/ram.svg \
	$(EXAMPLES_DIR)/ram_process_postgres.svg \
	$(EXAMPLES_DIR)/network.svg \
	$(EXAMPLES_DIR)/disk.svg \
	$(EXAMPLES_DIR)/pgsql.svg

.PHONY: all build build-backend build-tests clean run run-backend generate submodule test test-e2e test-load test-ui install docker-build docker-up docker-down docker-logs docker-test docker-test-ui test-benchmark demo demo-down run-multi down-multi collectd-base svgd-base docker-bases

# Build everything (default)
all: build

# Build both backend and gateway
build: build-backend
	$(CC) -o $(BIN_DIR)/$(GATE_BIN) $(GATE_SRC) -g $(CFLAGS)

# Build only the backend (svgd)
build-backend:
	@mkdir -p $(BIN_DIR)
	$(CC) -o $(BIN_DIR)/$(SERVER_BIN) $(SERVER_SRC) -g $(CFLAGS) $(LIBS)

build-tests: metrics_collector

metrics_collector: ./tests/metrics_collector.c
	$(CC) $(CFLAGS) -o ./tests/bin/metrics_collector ./tests/metrics_collector.c

test-e2e:
	cd tests && go test -v ./e2e/...

test-load:
	cd tests && go test -v ./load/...

test: test-e2e test-load

test-ui:
	./tests/ui/run_tests.sh -v

# Run gateway (main entry point)
run: build
	./$(BIN_DIR)/$(GATE_BIN) 127.0.0.1 $(PORT) 8080 ./gate/static

# Run backend only (for development)
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

clean:
	rm -f $(BIN_DIR)/$(SERVER_BIN) $(BIN_DIR)/$(GATE_BIN) $(CLIENT_BIN) $(SVG_FILES)
	rmdir $(EXAMPLES_DIR) || true

submodule:
	git submodule update --remote

install:
	python -m venv .venv
	.venv/bin/pip install -r tests/ui/requirements.txt

run-test-postgres:
	sudo docker run --name some-postgres -e POSTGRES_PASSWORD=mysecretpassword -p 5432:5432 -d bitnami/postgresql

# Docker targets
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

# Multi-datasource testing
svgd-base:
	docker build -f Dockerfile.base -t svgd-base:latest .

collectd-base:
	docker build -f .infra/collectd/Dockerfile.base -t svgd-collectd-base:latest .infra/collectd/

docker-bases: svgd-base collectd-base

run-multi: svgd-base collectd-base
	docker-compose -f docker-compose.multi.yml up --build

down-multi:
	docker-compose -f docker-compose.multi.yml down

# Benchmark targets
test-benchmark:
	@echo "Running LSRP vs HTTP benchmark..."
	cd tests && go test -v -run Test_HTTPVsLSRP ./e2e/... -timeout 10m

demo:
	@echo "Starting Docker demo with Toxiproxy..."
	cd tests/benchmark/docker && docker-compose up -d
	@echo "Demo started:"
	@echo "  Gateway: http://localhost:8080"
	@echo "  Toxiproxy API: http://localhost:8474"

demo-down:
	@echo "Stopping Docker demo..."
	cd tests/benchmark/docker && docker-compose down

# Cross-system benchmark
.PHONY: bench-comparison bench-charts bench-all

bench-comparison:
	cd tests/comparison && go run . --output results.csv

bench-charts:
	python3 tests/comparison/charts/generate.py tests/comparison/results.csv --output tests/comparison/charts/output/

bench-all: bench-comparison bench-charts
