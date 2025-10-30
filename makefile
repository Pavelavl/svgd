CC = gcc
CFLAGS = -Ilsrp -Wall -Wextra -O2 -pthread
LIBS = -lrrd -lduktape

LSRP_DIR = lsrp
BIN_DIR = $(LSRP_DIR)/bin
EXAMPLES_DIR = examples

SERVER_SRC = src/*.c $(LSRP_DIR)/lsrp_server.c
SERVER_BIN = svgd

CLIENT_BIN = $(BIN_DIR)/lsrp
PORT := $(shell jq -r '.server.tcp_port // "8080"' config.json)
PERIOD = 3600

SVG_FILES = \
	$(EXAMPLES_DIR)/cpu.svg \
	$(EXAMPLES_DIR)/cpu_process_postgres.svg \
	$(EXAMPLES_DIR)/ram.svg \
	$(EXAMPLES_DIR)/ram_process_postgres.svg \
	$(EXAMPLES_DIR)/network.svg \
	$(EXAMPLES_DIR)/disk.svg \
	$(EXAMPLES_DIR)/pgsql.svg

.PHONY: all build generate clean

run-gw:
	cd gate && make run

run:
	./bin/svgd ./config.json

all: build generate

build:
	$(CC) -o bin/$(SERVER_BIN) $(SERVER_SRC) -g $(CFLAGS) $(LIBS)

build-gw:
	cd gate && make build

build-tests: metrics_collector

metrics_collector: ./tests/metrics_collector.c
	$(CC) $(CFLAGS) -o ./tests/bin/metrics_collector ./tests/metrics_collector.c

test:
	cd tests && go test -v ./... 

generate:
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=cpu&period=$(PERIOD)" > examples/cpu.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=cpu/process/systemd&period=$(PERIOD)" > examples/cpu_process_systemd.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=ram&period=$(PERIOD)" > examples/ram.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=ram/process/postgres&period=$(PERIOD)" > examples/ram_process_postgres.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=network/eth0&period=$(PERIOD)" > examples/network.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=disk/sdc&period=$(PERIOD)" > examples/disk.svg && \
	$(CLIENT_BIN) localhost:$(PORT) "endpoint=postgresql/connections&period=$(PERIOD)" > examples/pgsql.svg

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(SVG_FILES)
	rmdir $(EXAMPLES_DIR) || true

submodule:
	git submodule update --remote 
