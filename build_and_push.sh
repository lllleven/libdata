#!/bin/bash
# 构建并推送Docker镜像到Harbor

set -e

# Harbor配置
HARBOR_HOST="harbor.changyinlive.com"
HARBOR_PROJECT="web3_capell"
IMAGE_NAME="libdatachannel-test"
FULL_IMAGE_NAME="${HARBOR_HOST}/${HARBOR_PROJECT}/${IMAGE_NAME}"

# 默认标签
TAG=${1:-latest}

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}构建并推送Docker镜像到Harbor${NC}"
echo -e "${GREEN}========================================${NC}"
echo -e "Harbor地址: ${HARBOR_HOST}"
echo -e "项目名称: ${HARBOR_PROJECT}"
echo -e "镜像名称: ${FULL_IMAGE_NAME}:${TAG}"
echo -e "${GREEN}========================================${NC}"
echo ""

# 检查Docker是否运行
if ! docker info &> /dev/null; then
    echo -e "${RED}错误: Docker未运行，请先启动Docker${NC}"
    exit 1
fi

# 检查是否已登录Harbor
echo -e "${YELLOW}[1/4] 检查Harbor登录状态...${NC}"
if ! docker info | grep -q "${HARBOR_HOST}"; then
    echo -e "${YELLOW}未检测到Harbor登录，请先登录：${NC}"
    echo -e "  docker login ${HARBOR_HOST}"
    echo ""
    read -p "是否现在登录? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        docker login ${HARBOR_HOST}
    else
        echo -e "${RED}取消操作${NC}"
        exit 1
    fi
fi

# 构建镜像
echo -e "${YELLOW}[2/4] 构建Docker镜像...${NC}"
echo -e "提示: 如果网络较慢，可以使用 Dockerfile.cn（国内镜像源）"
echo -e "      docker build -t ${IMAGE_NAME}:${TAG} -f Dockerfile.cn ."
echo ""

# 检查是否使用国内镜像源
if [ -f "Dockerfile.cn" ] && [ "$USE_CN_MIRROR" = "true" ]; then
    echo -e "${YELLOW}使用国内镜像源构建...${NC}"
    docker build -t ${IMAGE_NAME}:${TAG} -f Dockerfile.cn .
else
    docker build -t ${IMAGE_NAME}:${TAG} -f Dockerfile .
fi

if [ $? -ne 0 ]; then
    echo -e "${RED}构建失败${NC}"
    exit 1
fi

echo -e "${GREEN}构建成功${NC}"

# 标记镜像
echo -e "${YELLOW}[3/4] 标记镜像...${NC}"
docker tag ${IMAGE_NAME}:${TAG} ${FULL_IMAGE_NAME}:${TAG}

# 推送镜像
echo -e "${YELLOW}[4/4] 推送镜像到Harbor...${NC}"
docker push ${FULL_IMAGE_NAME}:${TAG}

if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}推送成功！${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo -e "镜像地址: ${FULL_IMAGE_NAME}:${TAG}"
    echo ""
    echo -e "使用方法:"
    echo -e "  docker pull ${FULL_IMAGE_NAME}:${TAG}"
    echo ""
else
    echo -e "${RED}推送失败${NC}"
    exit 1
fi

