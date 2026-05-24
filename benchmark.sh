#!/bin/bash
# benchmark.sh

VERSIONS=("tiny_sequential" "tiny_process" "tiny_thread" "tiny_pool" "tiny_epoll")
CONCURRENCIES=(1 10 100)
REQUESTS=1000
PORT=8080
RESULT_FILE="benchmark_results.txt"

echo "Tiny Server Benchmark — $(date)" > $RESULT_FILE
echo "========================================" >> $RESULT_FILE

for version in "${VERSIONS[@]}"; do
    echo ""
    echo ">>> 測試 $version"

    # 啟動 server
    ./$version &
    SERVER_PID=$!
    sleep 1  # 等 server 起來

    echo "" >> $RESULT_FILE
    echo "[ $version ]" >> $RESULT_FILE

    for c in "${CONCURRENCIES[@]}"; do
        echo "  並發 $c ..."
        echo "  -n $REQUESTS -c $c" >> $RESULT_FILE

        ab -n $REQUESTS -c $c -s 10 -q http://localhost:$PORT/ 2>&1 \
            | grep -E "Requests per second|Time per request|Failed requests" \
            >> $RESULT_FILE

        echo "" >> $RESULT_FILE
        sleep 0.3
    done

    # 關掉 server
    kill $SERVER_PID 2>/dev/null
    wait $SERVER_PID 2>/dev/null
    for i in $(seq 1 10); do
        if ! lsof -i :$PORT > /dev/null 2>&1; then
            break
        fi
        sleep 0.5
    done
    sleep 1  # 等 port 釋放
done

echo ""
echo "完成！結果存在 $RESULT_FILE"
cat $RESULT_FILE