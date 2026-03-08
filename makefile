CC = gcc
CFLAGS = -Ilsrp -Wall -Wextra -O2 -pthread
LIBS = -lrrd -lduktape

LSRP_DIR = lsrp
BIN_DIR = bin
EXAMPLES_DIR = examples

SERVER_SRC = src/*.c $(LSRP_DIR)/lsrp_server.c
SERVER_BIN = svgd
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

.PHONY: all build build-backend build-tests clean run run-backend generate submodule

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

test:
	cd tests && go test -v ./...

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
