# Makefile for libdatachannel Docker镜像构建和推送

HARBOR_HOST = harbor.changyinlive.com
HARBOR_PROJECT = web3_capell
IMAGE_NAME = libdatachannel-test
FULL_IMAGE_NAME = $(HARBOR_HOST)/$(HARBOR_PROJECT)/$(IMAGE_NAME)
TAG ?= latest

.PHONY: help build tag push login pull clean

help:
	@echo "libdatachannel Docker镜像管理"
	@echo ""
	@echo "可用命令:"
	@echo "  make build          - 构建Docker镜像"
	@echo "  make tag            - 标记镜像（默认tag: latest）"
	@echo "  make push            - 推送镜像到Harbor"
	@echo "  make build-push      - 构建并推送镜像"
	@echo "  make login           - 登录Harbor"
	@echo "  make pull            - 从Harbor拉取镜像"
	@echo "  make clean           - 清理本地镜像"
	@echo ""
	@echo "环境变量:"
	@echo "  TAG=版本号          - 指定镜像标签（默认: latest）"
	@echo ""
	@echo "示例:"
	@echo "  make build-push TAG=v1.0.0"
	@echo "  make push TAG=latest"

build:
	@echo "构建Docker镜像..."
	docker build -t $(IMAGE_NAME):$(TAG) -f Dockerfile .

build-cn:
	@echo "构建Docker镜像（使用国内镜像源）..."
	docker build -t $(IMAGE_NAME):$(TAG) -f Dockerfile.cn .

tag:
	@echo "标记镜像..."
	docker tag $(IMAGE_NAME):$(TAG) $(FULL_IMAGE_NAME):$(TAG)

push: tag
	@echo "推送镜像到Harbor..."
	docker push $(FULL_IMAGE_NAME):$(TAG)
	@echo ""
	@echo "推送成功！镜像地址: $(FULL_IMAGE_NAME):$(TAG)"

build-push: build push
	@echo "构建并推送完成！"

login:
	@echo "登录Harbor..."
	docker login $(HARBOR_HOST)

pull:
	@echo "从Harbor拉取镜像..."
	docker pull $(FULL_IMAGE_NAME):$(TAG)

clean:
	@echo "清理本地镜像..."
	-docker rmi $(IMAGE_NAME):$(TAG) 2>/dev/null || true
	-docker rmi $(FULL_IMAGE_NAME):$(TAG) 2>/dev/null || true
