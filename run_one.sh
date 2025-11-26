#!/usr/bin/env bash
set -euo pipefail

SESSION_ID=${SESSION_ID:-mysession}
SIGNALING_URL=${SIGNALING_SERVER_URL:-http://127.0.0.1:9227}
FILE_MB=${FILE_MB:-500}
CHUNK=${CHUNK:-65535}
RUN_ID=$(date +%s)

cleanup() {
    docker stop ldcsignal >/dev/null 2>&1 || true
    docker rm ldcsignal >/dev/null 2>&1 || true
}

trap cleanup EXIT

echo "[${RUN_ID}] 清理旧会话 ${SESSION_ID}"
curl -sSf -X DELETE "${SIGNALING_URL}/session/${SESSION_ID}" >/dev/null || true

echo "[${RUN_ID}] 启动信令服务器"
docker run -d --name ldcsignal --network host libdatachannel-test python3 /app/signaling_server.py 9227

echo "[${RUN_ID}] 启动接收端"
docker run --rm --network host libdatachannel-test \
    /app/test_receiver_http "${SIGNALING_URL}" "${SESSION_ID}" "${FILE_MB}" &
RECEIVER_PID=$!

sleep 2

echo "[${RUN_ID}] 启动发送端"
docker run --rm --network host libdatachannel-test \
    /app/test_sender_http "${SIGNALING_URL}" "${SESSION_ID}" "${FILE_MB}" "${CHUNK}"

wait "${RECEIVER_PID}"

