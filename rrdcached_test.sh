#!/bin/zsh

# Предполагаем, что бинарники with_rrdcached (с rrdcached) и without_rrdcached (прямой доступ) уже скомпилированы.
# Конфиг: config.ini с rrdcached_addr для svg.
# Клиент: ./lsrp/bin/lsrp
# Тестируем на 10 запросах для статистики.
# Измеряем время с time, выводим среднее.

# Параметры
PORT=8080
REQUESTS=10
ENDPOINT="endpoint=cpu&period=3600"  # Пример эндпоинта, измените если нужно
OUTPUT_DIR=temp_svgs
mkdir -p $OUTPUT_DIR

test_server() {
    BINARY=$1
    LOG_PREFIX=$2

    echo "Starting $LOG_PREFIX server with $BINARY..."

    ./bin/$BINARY > ${LOG_PREFIX}_server.log 2>&1 &
    SERVER_PID=$!
    sleep 2 

    # Проверка, запущен ли сервер
    if ! ps -p $SERVER_PID > /dev/null; then
        echo "Failed to start $LOG_PREFIX server!"
        return 1
    fi

    TOTAL_TIME=0
    for i in $(seq 1 $REQUESTS); do
        START=$(date +%s%N)
        ./lsrp/bin/lsrp localhost:$PORT "$ENDPOINT" -o $OUTPUT_DIR/test_$i.svg
        END=$(date +%s%N)
        ELAPSED=$(( (END - START) / 1000000 ))  # в мс
        echo "Request $i: $ELAPSED ms"
        TOTAL_TIME=$((TOTAL_TIME + ELAPSED))
    done

    AVG_TIME=$((TOTAL_TIME / REQUESTS))
    echo "Average time for $LOG_PREFIX: $AVG_TIME ms"

    # Очистка
    kill $SERVER_PID
    rm -f $OUTPUT_DIR/*.svg
}

test_server "with_rrdcached" "with_rrdcached"

test_server "without_rrdcached" "without_rrdcached"

rmdir $OUTPUT_DIR
