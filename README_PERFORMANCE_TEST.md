# libdatachannel 性能测试

使用Docker进行libdatachannel跨服务器性能测试的快速指南。

> **Harbor镜像**: 镜像已推送到 `harbor.changyinlive.com/web3_capell/libdatachannel-test:latest`  
> 查看 [Harbor推送指南](HARBOR_GUIDE.md) 了解如何构建和推送镜像

## 快速开始

### 方式1: 使用启动脚本（最简单）

```bash
# 使用默认参数
./run_test.sh

# 或设置自定义参数
SESSION_ID=my_test FILE_SIZE_MB=800 CHUNK_SIZE=32768 ./run_test.sh
```

### 方式2: 使用Docker Compose（推荐）

```bash
# 1. 构建镜像
docker-compose build

# 2. 启动所有服务（信令服务器、接收端、发送端）
docker-compose up

# 3. 查看日志
docker-compose logs -f

# 4. 停止服务
docker-compose down
```

### 自定义参数

创建`.env`文件或设置环境变量：

```bash
# .env文件
SESSION_ID=my_test_session
FILE_SIZE_MB=500        # 传输500MB文件
CHUNK_SIZE=65535        # 每个消息块65535字节
STUN_SERVER=stun:stun.l.google.com:19302
```

然后运行：

```bash
docker-compose up
```

## 手动运行（跨服务器）

### 步骤1: 构建镜像或从Harbor拉取

**从Harbor拉取（推荐）:**
```bash
docker login harbor.changyinlive.com
docker pull harbor.changyinlive.com/web3_capell/libdatachannel-test:latest
docker tag harbor.changyinlive.com/web3_capell/libdatachannel-test:latest libdatachannel-test:latest
```

**或本地构建:**
```bash
docker build -t libdatachannel-test:latest .
```

### 步骤2: 在服务器A上启动信令服务器

```bash
docker run -d \
  --name signaling \
  -p 9355:9355 \
  libdatachannel-test:latest \
  python3 /app/signaling_server.py 9355
```

### 步骤3: 在服务器B上启动接收端

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

### 步骤4: 在服务器C上启动发送端

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

## 参数说明

### test_sender_http
- 信令服务器URL: `http://host:port`
- 会话ID: 唯一标识符（必须与接收端相同）
- 文件大小: MB数（例如：500表示500MB）
- 消息块大小: 字节数（1-65535，默认65535）
- STUN服务器: `stun:host:port`（可选）

### test_receiver_http
- 信令服务器URL: `http://host:port`
- 会话ID: 唯一标识符（必须与发送端相同）
- 预期文件大小: MB数（必须与发送端相同）
- STUN服务器: `stun:host:port`（可选）

## 输出说明

测试会显示：
- 连接建立时间
- 文件传输进度（每10MB或每10秒显示一次）
- 实时传输速率
- 最终统计结果（总传输量、平均速率、文件完整性验证等）

## 故障排除

### 无法连接到信令服务器

```bash
# 检查信令服务器是否运行
curl http://信令服务器IP:9355/health

# 检查容器日志
docker logs signaling
```

### WebRTC连接失败

- 确保UDP端口9300-9400未被防火墙阻止
- 跨网络时需要使用STUN/TURN服务器
- 检查网络配置和NAT设置

## 更多信息

- [Docker使用指南](DOCKER_GUIDE.md) - 详细的Docker使用说明
- [HTTP信令版本指南](PERFORMANCE_TEST_HTTP_GUIDE.md) - 详细的配置和故障排除

