CC = gcc
CFLAGS = -Ilsrp

LSRP_DIR = ../lsrp

CLIENT_SRC = ./*.c $(LSRP_DIR)/lsrp_client.c
SERVER_BIN = svgd-gate

DEF_HOST = 127.0.0.1
DEF_SVGD_PORT = 8081
DEF_HTTP_PORT = 8080

.PHONY: build run

run:
	../bin/svgd-gate $(DEF_HOST) $(DEF_SVGD_PORT) $(DEF_HTTP_PORT)

build:
	$(CC) -o ../bin/$(SERVER_BIN) $(CLIENT_SRC) -g $(CFLAGS)
