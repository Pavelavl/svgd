# ============================================================
# SVGD Makefile
# ============================================================

REPO_ROOT := $(shell pwd)

# === Configuration ===
# Архитектура: можно переопределить через make ARCH=arm64 build
ARCH ?= $(shell uname -m)

ifeq ($(ARCH),aarch64)
    CC = aarch64-linux-gnu-gcc
else ifeq ($(ARCH),arm64)
    CC = aarch64-linux-gnu-gcc
else
    CC = gcc
endif

CFLAGS   = -Ilsrp -Wall -Wextra -O2 -g -rdynamic -pthread -fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wformat -Werror=format-security
LIBS     = -lrrd -lduktape
GATE_LIBS = -lcrypto -lssl

LSRP_DIR    = lsrp
BIN_DIR     = bin
EXAMPLES_DIR = examples

SERVER_SRC = src/main.c src/cfg.c src/http.c src/handler.c src/path_util.c src/metric_source.c src/proc_source.c src/rrd/reader.c src/rrd/cache.c src/rrd/svg.c $(LSRP_DIR)/lsrp_server.c
SERVER_BIN = svgd
GATE_SRC   = gate/*.c gate/auth/*.c $(LSRP_DIR)/lsrp_client.c
GATE_BIN   = svgd-gate
CLIENT_BIN = $(LSRP_DIR)/bin/lsrp

# === GitHub Container Registry ===
REPO_OWNER := $(shell git config --get user.name 2>/dev/null || echo "your-username")
IMAGE_NAME ?= ghcr.io/$(REPO_OWNER)/svgd
IMAGE_TAG ?= latest
FULL_IMAGE ?= $(IMAGE_NAME):$(IMAGE_TAG)

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
.PHONY: test test-all test-c test-e2e test-load test-ui test-ui-browser test-comparison
.PHONY: report generate-report generate-charts clean-results
.PHONY: docker-build docker-up docker-down docker-logs docker-test docker-test-ui
.PHONY: docker-bases svgd-base collectd-base
.PHONY: run-multi down-multi
.PHONY: bench-svgd-only bench-comparison bench-charts bench-all bench-quick bench-clean
.PHONY: bench-docker-build bench-docker-up bench-docker-down
.PHONY: demo demo-detached demo-logs demo-down submodule
.PHONY: docker-login docker-push docker-pull run-from-ghcr
.PHONY: deploy deploy-local deploy-setup

# ============================================================
# BUILD
# ============================================================

all: build

# Create scripts symlink for local development (scripts -> src/scripts)
scripts:
	@ln -sf src/scripts scripts

build: scripts build-backend
	$(CC) -o $(BIN_DIR)/$(GATE_BIN) $(GATE_SRC) -g $(CFLAGS) $(GATE_LIBS)

build-backend: scripts
	@mkdir -p $(BIN_DIR)
	$(CC) -o $(BIN_DIR)/$(SERVER_BIN) $(SERVER_SRC) -g $(CFLAGS) $(LIBS)

clean:
	rm -f $(BIN_DIR)/$(SERVER_BIN) $(BIN_DIR)/$(GATE_BIN) $(CLIENT_BIN) $(SVG_FILES)
	rm -f scripts  # Remove symlink
	rmdir $(EXAMPLES_DIR) 2>/dev/null || true

# ============================================================
# INSTALL
# ============================================================

# Install paths. Override with `make install PREFIX=/opt/svgd DESTDIR=/tmp/pkg`.
PREFIX      ?= /usr/local
DESTDIR     ?=
INST_BIN     = $(DESTDIR)$(PREFIX)/bin
INST_DATA    = $(DESTDIR)$(PREFIX)/share/svgd
INST_SYSTEMD = $(DESTDIR)$(PREFIX)/lib/systemd/system
INST_SYSCONF = $(DESTDIR)/etc/svgd

# Install binaries, web UI, JS renderer, configs, and systemd units.
# Configs under /etc/svgd are installed only if absent — existing configs are
# preserved across re-installs. Run as root (or via a package manager) so that
# /etc, /var/lib, /var/log, and the systemd dir are writable.
# Post-install steps: see .infra/install-notes.md.
install: build
	@echo "=== Installing svgd into $(PREFIX) (DESTDIR=$(DESTDIR)) ==="
	install -d $(INST_BIN)
	install -m 0755 $(BIN_DIR)/$(SERVER_BIN) $(INST_BIN)/
	install -m 0755 $(BIN_DIR)/$(GATE_BIN)  $(INST_BIN)/
	@# --- web UI ---
	install -d $(INST_DATA)/static
	install -m 0644 gate/static/* $(INST_DATA)/static/
	@# --- JS rendering script ---
	install -d $(INST_DATA)/scripts
	install -m 0644 src/scripts/generate_svg.js $(INST_DATA)/scripts/
	@# --- runtime directories ---
	install -d $(INST_SYSCONF) $(DESTDIR)/var/lib/svgd $(DESTDIR)/var/log/svgd
	@# --- configs (never clobber existing files) ---
	[ -f $(INST_SYSCONF)/config.json      ] || install -m 0644 config.sample.json      $(INST_SYSCONF)/config.json
	[ -f $(INST_SYSCONF)/datasources.json ] || install -m 0644 datasources.sample.json $(INST_SYSCONF)/datasources.json
	[ -f $(INST_SYSCONF)/auth.json        ] || install -m 0640 auth.example.json       $(INST_SYSCONF)/auth.json
	@# --- systemd units (default PREFIX paths are baked into the unit files) ---
	install -d $(INST_SYSTEMD)
	install -m 0644 .infra/systemd/svgd.service      $(INST_SYSTEMD)/
	install -m 0644 .infra/systemd/svgd-gate.service $(INST_SYSTEMD)/
	@echo "=== Install complete. Post-install steps: .infra/install-notes.md ==="
	@echo "    systemctl daemon-reload && systemctl enable --now svgd svgd-gate"

# ============================================================
# DIST (source tarball for packagers)
# ============================================================

# Version - single source of truth for releases. Prefer `git describe` on the
# most recent reachable semver tag (e.g. v0.1.0, or v0.1.0-5-g<sha> for commits
# past the tag); for untagged dev builds it falls back to the short commit hash.
# Override with `make VERSION=foo dist` to force a value.
VERSION ?= $(shell git describe --tags --always 2>/dev/null)
ifeq ($(strip $(VERSION)),)
VERSION := 0.0.0-dev
endif

# Tarballs use the bare semver (strip a leading 'v').
DIST_VERSION := $(VERSION:v%=%)
DIST_NAME    := svgd-$(DIST_VERSION)
DIST_ROOT    := dist
DIST_DIR     := $(DIST_ROOT)/$(DIST_NAME)
DIST_TARBALL := $(DIST_ROOT)/$(DIST_NAME).tar.gz

.PHONY: dist dist-clean

# Produce a distributable source tarball `svgd-<version>.tar.gz` for packagers
# (AUR / distro maintainers). Stages a clean snapshot of git-tracked files via
# `git archive` (so gitignored secrets like auth.json never leak in), flattens
# the lsrp submodule into the tree (submodules can't be `git submodule init`'d
# from a tarball), records the version, and tars it under a top-level
# svgd-<version>/ directory. svgd-collect is intentionally excluded (separate
# project with its own release cycle).
dist:
	@echo "=== Staging $(DIST_NAME) (version $(VERSION)) ==="
	@rm -rf $(DIST_DIR)
	@mkdir -p $(DIST_DIR)
	@# 1. Clean snapshot of tracked files (excludes auth.json, config.json, *.o).
	@git archive --format=tar HEAD | tar -x -C $(DIST_DIR)
	@# 2. Flatten the lsrp submodule (git archive does not recurse into submodules).
	@rm -rf $(DIST_DIR)/lsrp
	@mkdir -p $(DIST_DIR)/lsrp
	@(cd $(LSRP_DIR) && git archive --format=tar HEAD) | tar -x -C $(DIST_DIR)/lsrp
	@# 3. Drop stale submodule pointer and the empty svgd-collect/ gitlink dir
	@#    (lsrp is now a regular dir; svgd-collect isn't shipped - separate project).
	@rm -f $(DIST_DIR)/.gitmodules
	@rm -rf $(DIST_DIR)/svgd-collect
	@# 4. Record the version for downstream consumers (PKGBUILD, makepkg, etc.).
	@printf '%s\n' "$(VERSION)" > $(DIST_DIR)/VERSION
	@# 5. Reproducible-ish tar (root ownership, sorted) under a top-level dir.
	@tar --owner=0 --group=0 --sort=name -czf $(DIST_TARBALL) -C $(DIST_ROOT) $(DIST_NAME)
	@echo "=== Created $(DIST_TARBALL) ==="
	@echo "    Contents (first 25 entries):"
	@tar tzf $(DIST_TARBALL) | head -25 | sed 's/^/      /'

dist-clean:
	rm -rf $(DIST_ROOT)

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

# C unit-тесты идут первыми: они быстрые и не требуют бинарников/RRD-данных,
# поэтому служат ранним шлюзом регрессии чистой логики до запуска интеграционных
# Go-тестов (которым нужны скомпилированные bin/svgd и RRD-файлы).
test: test-c test-e2e test-load

test-all: test-e2e test-load test-comparison
	@echo "All tests completed"

# ============================================================
# C UNIT TESTS (tests/c/)
# ============================================================

# Компилирует и прогоняет C unit-тесты чистой логики (select_step_from_rras,
# find_metric_config, build_rrd_path/extract_param_from_path, load_config).
# Харнес: tests/c/minitest.h (в духе svgd-collect), раннер — tests/c/run.sh.
# Сборка через $(CC); флаги согласованы с основными CFLAGS (без -rdynamic/-O2,
# с -O0 -g для удобной отладки тестов). Не затрагивает bin/svgd.
test-c:
	@bash tests/c/run.sh

test-e2e:
	REPO_ROOT="$(REPO_ROOT)" sh -c 'cd tests && go test -v ./internal/e2e/...'

test-load:
	REPO_ROOT="$(REPO_ROOT)" sh -c 'cd tests && go test -v ./internal/load/...'

test-comparison: test-bench

test-ui:
	bash tests/internal/ui/run_tests.sh -v

test-ui-browser:
	bash tests/internal/ui/run_tests.sh --browser -v

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
	cd tests/shared/system && go run ./cmd/merge $(SOURCES) ../../results

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
	cd tests/shared/system && go run ./cmd/reportgen

# Clean all results
clean-results:
	rm -f tests/results/*.csv tests/results/report.md
	rm -f tests/results/machines/*.json
	rm -f tests/results/charts/output/*.png

# ============================================================
# CROSS-SYSTEM BENCHMARK
# ============================================================

test-bench-svgd:
	REPO_ROOT="$(REPO_ROOT)" go run -C tests/internal/comparison .

test-bench: build-backend bench-docker-build bench-docker-up
	@echo "=== Cross-System Benchmark (svgd vs RRDtool vs Graphite) ==="
	REPO_ROOT="$(REPO_ROOT)" go run -C tests/internal/comparison -tags docker .

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

# ============================================================
# DEMO (one-command, no collectd / no rrdcached)
# ============================================================

# Brings up a self-contained dashboard on http://localhost:8080 with
# synthetic demo data. See docker-compose.demo.yml and demo/.
demo:
	@docker image inspect svgd-base:latest >/dev/null 2>&1 \
		|| { echo "=== Building svgd-base (one-time) ==="; $(MAKE) svgd-base; }
	docker compose -f docker-compose.demo.yml up --build

# Detached variant: start in the background.
demo-detached:
	docker compose -f docker-compose.demo.yml up -d --build

demo-logs:
	docker compose -f docker-compose.demo.yml logs -f

# Stop the demo (keeps the generated RRD volume; pass -v to reset data).
demo-down:
	docker compose -f docker-compose.demo.yml down

# ============================================================
# GITHUB CONTAINER REGISTRY
# ============================================================

docker-login:
	@echo "Login to GHCR with: docker login ghcr.io -u $(REPO_OWNER)"

docker-push: docker-build
	@echo "Pushing $(FULL_IMAGE)..."
	docker tag svgd:latest $(FULL_IMAGE)
	docker push $(FULL_IMAGE)

docker-push-latest:
	$(MAKE) docker-push IMAGE_TAG=latest

docker-pull:
	@echo "Pulling $(FULL_IMAGE)..."
	docker pull $(FULL_IMAGE)

run-from-ghcr:
	@echo "Running services from $(FULL_IMAGE)..."
	IMAGE_SOURCE=$(IMAGE_NAME) docker compose up -d

# ============================================================
# DEPLOY
# ============================================================

# Хост для деплоя (можно переопределить: make deploy DEPLOY_HOST=user@host)
DEPLOY_HOST ?= pvl@192.168.1.208

deploy: build
	@echo "=== Деплой на $(DEPLOY_HOST) ==="
	./deploy.sh $(DEPLOY_HOST)

deploy-setup:
	@echo "Запустите этот скрипт на сервере:"
	@echo "curl -sL https://raw.githubusercontent.com/Pavelavl/svgd/master/scripts/setup-server.sh | bash -s"
