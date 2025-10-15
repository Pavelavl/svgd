CC = gcc
CFLAGS = -Iinih -Ilsrp
LIBS = -lrrd -lduktape

LSRP_DIR = lsrp
BIN_DIR = $(LSRP_DIR)/bin
EXAMPLES_DIR = examples

SERVER_SRC = src/*.c $(LSRP_DIR)/lsrp_server.c inih/ini.c
SERVER_BIN = svgd

CLIENT_BIN = $(BIN_DIR)/lsrp

SVG_FILES = \
	$(EXAMPLES_DIR)/cpu.svg \
	$(EXAMPLES_DIR)/cpu_process_postgres.svg \
	$(EXAMPLES_DIR)/ram.svg \
	$(EXAMPLES_DIR)/ram_process_postgres.svg \
	$(EXAMPLES_DIR)/network.svg \
	$(EXAMPLES_DIR)/disk.svg \
	$(EXAMPLES_DIR)/pgsql.svg

.PHONY: all build generate clean

run:
	./bin/svgd

all: build generate

build:
	$(CC) -o bin/$(SERVER_BIN) $(SERVER_SRC) -g $(CFLAGS) $(LIBS)

generate:
	$(CLIENT_BIN) localhost:8080 "endpoint=cpu" > examples/cpu.svg && \
	$(CLIENT_BIN) localhost:8080 "endpoint=cpu/process/postgres" > examples/cpu_process_postgres.svg && \
	$(CLIENT_BIN) localhost:8080 "endpoint=ram" > examples/ram.svg && \
	$(CLIENT_BIN) localhost:8080 "endpoint=ram/process/postgres" > examples/ram_process_postgres.svg && \
	$(CLIENT_BIN) localhost:8080 "endpoint=network/eth0" > examples/network.svg && \
	$(CLIENT_BIN) localhost:8080 "endpoint=disk/sdc" > examples/disk.svg && \
	$(CLIENT_BIN) localhost:8080 "endpoint=postgresql/connections" > examples/pgsql.svg

clean:
	rm -f $(SERVER_BIN) $(CLIENT_BIN) $(SVG_FILES)
	rmdir $(EXAMPLES_DIR) || true

submodule:
	git submodule update --remote 
