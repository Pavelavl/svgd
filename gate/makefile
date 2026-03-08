CC = gcc
CFLAGS = -Ilsrp -Wall -Wextra -O2

LSRP_DIR = ../lsrp

GATE_SRC = ./*.c $(LSRP_DIR)/lsrp_client.c
SERVER_BIN = svgd-gate

DEF_HOST = 127.0.0.1
DEF_SVGD_PORT = 8081
DEF_HTTP_PORT = 8080
DEF_STATIC_PATH = ./static

.PHONY: build run

run:
	../bin/svgd-gate $(DEF_HOST) $(DEF_SVGD_PORT) $(DEF_HTTP_PORT) $(DEF_STATIC_PATH)

build:
	$(CC) -o ../bin/$(SERVER_BIN) $(GATE_SRC) -g $(CFLAGS)
