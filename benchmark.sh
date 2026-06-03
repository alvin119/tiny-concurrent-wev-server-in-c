#!/bin/bash
# remote_benchmark.sh
# 在筆電執行，RPi 那邊要手動啟動對應的 server

RPI_IP="10.184.167.218"
PORT=8080
REQUESTS=1000
CONCURRENCIES=(1 10 100)
RESULT_FILE="remote_benchmark_results.txt"

echo "Remote Benchmark — $(date)" > $RESULT_FILE
echo "RPi IP: $RPI_IP" >> $RESULT_FILE
echo "========================================" >> $RESULT_FILE

run_bench() {
    local version=$1
    echo "" >> $RESULT_FILE
    echo "[ $version ]" >> $RESULT_FILE

    for c in "${CONCURRENCIES[@]}"; do
        echo "  -n $REQUESTS -c $c" >> $RESULT_FILE
        ab -n $REQUESTS -c $c -q http://$RPI_IP:$PORT/ 2>&1 \
            | grep -E "Requests per second|Time per request|Failed requests" \
            >> $RESULT_FILE
        echo "" >> $RESULT_FILE
        sleep 0.5
    done
}

echo "請在 RPi 上啟動 server，然後按 Enter 繼續..."
echo ""

for version in tiny_sequential tiny_process tiny_thread tiny_pool tiny_epoll; do
    echo ">>> 請在 RPi 上執行：./$version"
    echo "    啟動後按 Enter 開始測試，測完會提示你換下一個"
    read -p "    Ready? " 
    
    run_bench $version
    
    echo "    測試完成，請在 RPi 上 Ctrl+C 關掉 server"
    read -p "    關掉後按 Enter 繼續下一個..."
done

echo ""
echo "全部完成！"
cat $RESULT_FILE