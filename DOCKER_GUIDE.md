# libdatachannel Docker 使用指南

本指南说明如何使用Docker镜像进行libdatachannel性能测试。

## 快速开始

### 方式1: 使用docker-compose（推荐）

一键启动所有服务（信令服务器、接收端、发送端）：

```bash
# 构建镜像
docker-compose build

# 启动所有服务
docker-compose up

# 后台运行
docker-compose up -d

# 查看日志
docker-compose logs -f

# 停止服务
docker-compose down
```

### 方式2: 手动运行各个容器

#### 步骤1: 构建镜像

```bash
# 构建完整镜像（包含所有组件）
docker build -t libdatachannel-test:latest .

# 或分别构建
docker build -f Dockerfile.signaling -t libdatachannel-signaling:latest .
docker build -f Dockerfile.client -t libdatachannel-client:latest .
```

#### 步骤2: 启动信令服务器

```bash
docker run -d \
  --name signaling-server \
  -p 9355:9355 \
  libdatachannel-test:latest \
  python3 /app/signaling_server.py 9355
```

#### 步骤3: 启动接收端

```bash
docker run -it \
  --name receiver \
  --network host \
  libdatachannel-test:latest \
  /app/test_receiver_http \
    http://localhost:9355 \
    test_session_1 \
    500 \
    stun:stun.l.google.com:19302
```

#### 步骤4: 启动发送端（在另一个终端）

```bash
docker run -it \
  --name sender \
  --network host \
  libdatachannel-test:latest \
  /app/test_sender_http \
    http://localhost:9355 \
    test_session_1 \
    500 \
    65535 \
    stun:stun.l.google.com:19302
```

## 跨服务器部署

### 场景1: 信令服务器在独立服务器上

**在服务器A上启动信令服务器：**

```bash
docker run -d \
  --name signaling-server \
  -p 9355:9355 \
  libdatachannel-test:latest \
  python3 /app/signaling_server.py 9355
```

**在服务器B上启动接收端：**

```bash
docker run -it \
  --name receiver \
  --network host \
  libdatachannel-test:latest \
  /app/test_receiver_http \
    http://服务器A的IP:9355 \
    test_session_1 \
    500 \
    stun:stun.l.google.com:19302
```

**在服务器C上启动发送端：**

```bash
docker run -it \
  --name sender \
  --network host \
  libdatachannel-test:latest \
  /app/test_sender_http \
    http://服务器A的IP:9355 \
    test_session_1 \
    500 \
    65535 \
    stun:stun.l.google.com:19302
```

### 场景2: 使用Docker网络

创建Docker网络：

```bash
docker network create webrtc-test
```

启动信令服务器：

```bash
docker run -d \
  --name signaling \
  --network webrtc-test \
  -p 9355:9355 \
  libdatachannel-test:latest \
  python3 /app/signaling_server.py 9355
```

启动接收端：

```bash
docker run -it \
  --name receiver \
  --network webrtc-test \
  libdatachannel-test:latest \
  /app/test_receiver_http \
    http://signaling:9355 \
    test_session_1 \
    500 \
    stun:stun.l.google.com:19302
```

启动发送端：

```bash
docker run -it \
  --name sender \
  --network webrtc-test \
  libdatachannel-test:latest \
  /app/test_sender_http \
    http://signaling:9355 \
    test_session_1 \
    500 \
    65535 \
    stun:stun.l.google.com:19302
```

## 自定义配置

### 修改docker-compose.yml

编辑`docker-compose.yml`文件来修改测试参数：

```yaml
receiver:
  command: >
    /app/test_receiver_http 
      http://signaling:9355 
      test_session_1 
      500          # 预期文件大小
      stun:stun.l.google.com:19302

sender:
  command: >
    /app/test_sender_http 
      http://signaling:9355 
      test_session_1 
      500          # 文件大小（MB）
      16384        # 消息块大小
      stun:stun.l.google.com:19302
```

### 使用环境变量

创建`.env`文件：

```env
SIGNALING_URL=http://signaling:9355
SESSION_ID=test_session_1
FILE_SIZE_MB=500
CHUNK_SIZE=65535
STUN_SERVER=stun:stun.l.google.com:19302
```

修改`docker-compose.yml`使用环境变量：

```yaml
receiver:
  command: >
    /app/test_receiver_http 
      ${SIGNALING_URL}
      ${SESSION_ID}
      ${DURATION}
      ${STUN_SERVER}
```

## 查看日志

### docker-compose

```bash
# 查看所有日志
docker-compose logs

# 查看特定服务日志
docker-compose logs signaling
docker-compose logs receiver
docker-compose logs sender

# 实时跟踪日志
docker-compose logs -f
```

### docker run

```bash
# 查看容器日志
docker logs signaling-server
docker logs receiver
docker logs sender

# 实时跟踪
docker logs -f receiver
```

## 清理

```bash
# 停止并删除容器
docker-compose down

# 删除镜像
docker rmi libdatachannel-test:latest

# 清理所有相关资源
docker-compose down --rmi all --volumes --remove-orphans
```

## 故障排除

### 问题1: 容器无法连接到信令服务器

**解决方法：**
```bash
# 检查网络连接
docker network ls
docker network inspect webrtc-test

# 检查容器IP
docker inspect signaling | grep IPAddress

# 测试连接
docker exec receiver curl http://signaling:9355/health
```

### 问题2: 端口被占用

**解决方法：**
```bash
# 检查端口占用
netstat -tulpn | grep 9355

# 修改端口映射
docker run -p 9500:9355 ...
```

### 问题3: 权限问题

**解决方法：**
```bash
# 使用root用户运行
docker run --user root ...

# 或修复权限
docker exec container chmod +x /app/test_sender_http
```

### 问题4: 库文件找不到

**解决方法：**
```bash
# 检查库文件
docker exec container ls -la /usr/local/lib/ | grep datachannel

# 更新库路径
docker exec container ldconfig
```

## 性能优化

### 使用多阶段构建

Dockerfile已经使用多阶段构建，减小了最终镜像大小。

### 构建缓存

```bash
# 使用构建缓存
docker build --cache-from libdatachannel-test:latest .

# 不使用缓存（强制重新构建）
docker build --no-cache .
```

### 并行构建

```bash
# 使用多核构建
docker build --build-arg BUILDKIT_INLINE_CACHE=1 .
```

## 生产环境建议

1. **使用特定标签**：不要使用`latest`标签
2. **健康检查**：添加健康检查到docker-compose.yml
3. **资源限制**：设置CPU和内存限制
4. **日志管理**：配置日志驱动
5. **安全**：使用非root用户运行

示例docker-compose.yml改进：

```yaml
services:
  signaling:
    image: libdatachannel-test:v1.0.0
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:9355/health"]
      interval: 30s
      timeout: 10s
      retries: 3
    deploy:
      resources:
        limits:
          cpus: '0.5'
          memory: 256M
    logging:
      driver: "json-file"
      options:
        max-size: "10m"
        max-file: "3"
```

## 相关文件

- `Dockerfile` - 完整镜像（包含所有组件）
- `Dockerfile.signaling` - 仅信令服务器
- `Dockerfile.client` - 仅客户端
- `docker-compose.yml` - Docker Compose配置
- `.dockerignore` - Docker构建忽略文件

## 参考

- [Docker文档](https://docs.docker.com/)
- [Docker Compose文档](https://docs.docker.com/compose/)
- [HTTP信令版本使用指南](PERFORMANCE_TEST_HTTP_GUIDE.md)

