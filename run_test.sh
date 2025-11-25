#!/bin/bash
# libdatachannel 性能测试启动脚本

set -e

# 默认参数
SESSION_ID=${SESSION_ID:-test_session_1}
FILE_SIZE_MB=${FILE_SIZE_MB:-500}
CHUNK_SIZE=${CHUNK_SIZE:-65535}
STUN_SERVER=${STUN_SERVER:-stun:stun.l.google.com:19302}
SIGNALING_PORT=${SIGNALING_PORT:-9355}

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}libdatachannel 性能测试${NC}"
echo -e "${GREEN}========================================${NC}"

# 检查Docker
if ! command -v docker &> /dev/null; then
    echo -e "${RED}错误: 未找到Docker，请先安装Docker${NC}"
    exit 1
fi

# 检查docker-compose
if ! command -v docker-compose &> /dev/null && ! docker compose version &> /dev/null; then
    echo -e "${RED}错误: 未找到docker-compose${NC}"
    exit 1
fi

# 构建镜像
echo -e "${YELLOW}[1/4] 构建Docker镜像...${NC}"
docker-compose build || docker compose build

# 启动信令服务器
echo -e "${YELLOW}[2/4] 启动信令服务器...${NC}"
docker-compose up -d signaling || docker compose up -d signaling

# 等待信令服务器就绪
echo -e "${YELLOW}等待信令服务器启动...${NC}"
sleep 5

# 检查信令服务器健康状态
if curl -f http://localhost:${SIGNALING_PORT}/health &> /dev/null; then
    echo -e "${GREEN}信令服务器已就绪${NC}"
else
    echo -e "${YELLOW}警告: 无法连接到信令服务器，但继续执行...${NC}"
fi

# 启动接收端（后台）
echo -e "${YELLOW}[3/4] 启动接收端...${NC}"
docker-compose up -d receiver || docker compose up -d receiver

# 等待接收端准备
sleep 5

# 启动发送端（前台，显示日志）
echo -e "${YELLOW}[4/4] 启动发送端并开始测试...${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "测试参数:"
echo -e "  会话ID: ${SESSION_ID}"
echo -e "  文件大小: ${FILE_SIZE_MB}MB"
echo -e "  消息块大小: ${CHUNK_SIZE}字节"
echo -e "  STUN服务器: ${STUN_SERVER}"
echo -e "${GREEN}========================================${NC}"
echo ""

# 启动发送端并显示所有日志
docker-compose up sender || docker compose up sender

# 显示最终统计
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}测试完成${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "查看完整日志:"
echo "  docker-compose logs"
echo ""
echo "停止所有服务:"
echo "  docker-compose down"
echo ""

