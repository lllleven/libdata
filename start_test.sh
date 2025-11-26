#!/usr/bin/env bash
set -euo pipefail

print_usage() {
    cat <<'EOF'
Usage:
  start_test.sh [mode] [args...]

Modes:
  server [port]                 启动 signaling_server.py（默认 127.0.0.1:9227，可通过 SIGNALING_PORT 覆盖）
  sender [signaling_url ...]    启动打包好的发送端，可通过环境变量覆盖
  receiver [signaling_url ...]  启动接收端，可通过环境变量覆盖

Examples:
  ./start_test.sh server 9227
  ./start_test.sh sender http://127.0.0.1:9227 my_session 500 65535 stun:stun.l.google.com:19302
  ./start_test.sh receiver http://127.0.0.1:9227 my_session 500 stun:stun.l.google.com:19302
EOF
}

if [ $# -lt 1 ]; then
    echo "必须指定模式: server|sender|receiver"
    print_usage
    exit 1
fi

MODE=$1
shift

case "$MODE" in
    server)
        PORT=${1:-${SIGNALING_PORT:-9227}}
        exec python3 /app/signaling_server.py "$PORT"
        ;;
    sender)
        if [ "$#" -gt 0 ]; then
            exec /app/test_sender_http "$@"
        fi

        SIGNALING_URL=${SIGNALING_SERVER_URL:-http://127.0.0.1:9227}
        SESSION_ID=${SESSION_ID:-test_session_1}
        FILE_SIZE_MB=${FILE_SIZE_MB:-500}
        CHUNK_SIZE=${CHUNK_SIZE:-65535}

        echo "[启动] 发送端 -> $SIGNALING_URL 会话 $SESSION_ID 文件 ${FILE_SIZE_MB}MB chunk $CHUNK_SIZE"

        CMD=(/app/test_sender_http "$SIGNALING_URL" "$SESSION_ID" "$FILE_SIZE_MB" "$CHUNK_SIZE")
        if [ -n "${STUN_SERVER:-}" ]; then
            CMD+=("$STUN_SERVER")
        fi
        exec "${CMD[@]}"
        ;;
    receiver)
        if [ "$#" -gt 0 ]; then
            exec /app/test_receiver_http "$@"
        fi

        SIGNALING_URL=${SIGNALING_SERVER_URL:-http://127.0.0.1:9227}
        SESSION_ID=${SESSION_ID:-test_session_1}
        EXPECTED_MB=${EXPECTED_FILE_MB:-500}

        echo "[启动] 接收端 -> $SIGNALING_URL 会话 $SESSION_ID 预计 ${EXPECTED_MB}MB"

        CMD=(/app/test_receiver_http "$SIGNALING_URL" "$SESSION_ID" "$EXPECTED_MB")
        if [ -n "${STUN_SERVER:-}" ]; then
            CMD+=("$STUN_SERVER")
        fi
        exec "${CMD[@]}"
        ;;
    help|-h|--help)
        print_usage
        exit 0
        ;;
    *)
        echo "Unknown mode: $MODE"
        print_usage
        exit 1
        ;;
esac

