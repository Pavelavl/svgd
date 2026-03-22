#!/bin/bash
# Локальный скрипт деплоя на сервер
# Использование: ./deploy.sh [user@host]

set -e

HOST="${1:-svgd-deploy@192.168.1.208}"

echo "=== Деплой svgd на $HOST ==="

# Сборка
echo "Сборка binaries..."
make build

# Создание пакета
echo "Создание пакета..."
mkdir -p .deploy/package
cp bin/svgd .deploy/package/
cp bin/svgd-gate .deploy/package/
cp -r gate/static .deploy/package/
cp -r src/scripts .deploy/package/
cp config.json .deploy/package/
cd .deploy && tar czf svgd-deploy.tar.gz package/ && cd ..

# Деплой
echo "Отправка файлов на сервер..."
scp .deploy/svgd-deploy.tar.gz $HOST:/tmp/

echo "Установка на сервере..."
ssh $HOST bash -s <<'ENDSSH'
set -e

# Остановка сервисов
sudo systemctl stop svgd svgd-gate || true

# Бэкап
sudo mkdir -p /opt/svgd/backup
sudo cp -r /opt/svgd/bin /opt/svgd/backup/ 2>/dev/null || true
sudo cp /opt/svgd/config.json /opt/svgd/backup/ 2>/dev/null || true

# Распаковка
cd /opt/svgd && sudo tar xzf /tmp/svgd-deploy.tar.gz
rm /tmp/svgd-deploy.tar.gz

# Установка
sudo mkdir -p /opt/svgd/bin
sudo mkdir -p /opt/svgd/scripts
sudo mkdir -p /opt/svgd/gate/static
sudo mkdir -p /opt/svgd/rrd

sudo cp package/svgd /opt/svgd/bin/
sudo cp package/svgd-gate /opt/svgd/bin/
sudo rm -rf /opt/svgd/gate/static/*
sudo cp -r package/static/* /opt/svgd/gate/static/
sudo rm -rf /opt/svgd/scripts
sudo cp -r package/scripts /opt/svgd/
sudo cp package/config.json /opt/svgd/

sudo chmod +x /opt/svgd/bin/svgd /opt/svgd/bin/svgd-gate

sudo chown -R svgd:svgd /opt/svgd

# Перезапуск
sudo systemctl start svgd svgd-gate

echo "=== Деплой завершен ==="
sudo systemctl status svgd --no-pager
sudo systemctl status svgd-gate --no-pager
ENDSSH

# Очистка
rm -rf .deploy

echo "=== Готово! ==="
echo "Web UI: http://${HOST##*@}:8080"
echo "Health: curl http://${HOST##*@}:8080/api/health"
