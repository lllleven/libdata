# libdatachannel 跨服务器性能测试指南 (HTTP信令版本)

> **推荐使用Docker**: 如果您想快速开始，请查看 [Docker使用指南](DOCKER_GUIDE.md) 或 [快速开始README](README_PERFORMANCE_TEST.md)

本指南将帮助您使用HTTP信令服务器在两台不同的服务器上运行libdatachannel客户端进行性能测试。

## 架构说明

- **signaling_server.py**: HTTP信令服务器，用于交换WebRTC信令信息
- **test_sender_http.cpp**: 发送端程序，在服务器1上运行
- **test_receiver_http.cpp**: 接收端程序，在服务器2上运行

两个客户端通过HTTP REST API与信令服务器通信，交换SDP和ICE候选者信息。

## 前提条件

1. 两台服务器都已编译libdatachannel库
2. 两台服务器都能访问信令服务器（可以通过网络）
3. Python 3.x 和 Flask（用于信令服务器）
4. libcurl开发库（用于HTTP客户端）

### 安装依赖

**信令服务器依赖:**
```bash
pip3 install flask flask-cors
```

**客户端依赖 (Ubuntu/Debian):**
```bash
sudo apt-get install libcurl4-openssl-dev
```

**客户端依赖 (CentOS/RHEL):**
```bash
sudo yum install libcurl-devel
```

## 编译步骤

### 1. 编译发送端和接收端

在服务器1和服务器2上分别编译：

```bash
# 进入项目目录
cd libdatachannel-0.23.1

# 编译发送端（服务器1）
g++ -std=c++17 test_sender_http.cpp -o test_sender_http \
    -I./include \
    -L./build \
    -ldatachannel \
    -pthread \
    -lssl -lcrypto \
    -lcurl

# 编译接收端（服务器2）
g++ -std=c++17 test_receiver_http.cpp -o test_receiver_http \
    -I./include \
    -L./build \
    -ldatachannel \
    -pthread \
    -lssl -lcrypto \
    -lcurl
```

## 运行测试

### 步骤1: 启动信令服务器

在任意一台可以访问的服务器上启动信令服务器（可以是服务器1、服务器2或第三台服务器）：

```bash
# 安装依赖（如果还没安装）
pip3 install flask flask-cors

# 启动信令服务器
python3 signaling_server.py [端口]

# 示例（使用默认端口9355）
python3 signaling_server.py

# 或指定端口
python3 signaling_server.py 9000
```

信令服务器会显示监听的端口和API端点信息。

### 步骤2: 在服务器2上启动接收端

```bash
./test_receiver_http [信令服务器URL] [会话ID] [预期文件大小(MB)] [STUN服务器]

# 示例
./test_receiver_http http://192.168.1.10:9355 test_session_1 500 stun:stun.l.google.com:19302
```

接收端会等待发送端的Offer。

### 步骤3: 在服务器1上启动发送端

```bash
./test_sender_http [信令服务器URL] [会话ID] [文件大小(MB)] [消息块大小(字节)] [STUN服务器]

# 示例
./test_sender_http http://192.168.1.10:9355 test_session_1 500 65535 stun:stun.l.google.com:19302
```

发送端会创建Offer并开始发送数据。

## 参数说明

### test_sender_http 参数

1. **信令服务器URL**: 信令服务器的HTTP地址（默认: `http://localhost:9355`）
2. **会话ID**: 唯一的会话标识符，发送端和接收端必须使用相同的ID（默认: `test_session_1`）
3. **文件大小**: 待发送文件的大小，单位 MB（默认: 500）
4. **消息块大小**: 每条消息的字节数，1-65535（默认: 65535）
5. **STUN服务器**: STUN服务器地址（可选，格式: `stun:host:port`）

### test_receiver_http 参数

1. **信令服务器URL**: 信令服务器的HTTP地址（默认: `http://localhost:9355`）
2. **会话ID**: 唯一的会话标识符，必须与发送端相同（默认: `test_session_1`）
3. **预期文件大小**: 单位 MB（必须与发送端一致，默认500）
4. **STUN服务器**: STUN服务器地址（可选，格式: `stun:host:port`）

## 使用示例

### 示例1: 基本测试（使用默认STUN服务器）

**启动信令服务器（在服务器3或任意可访问的机器上）:**
```bash
python3 signaling_server.py 9355
```

**服务器2（接收端）:**
```bash
./test_receiver_http http://192.168.1.10:9355 test_session_1 500 stun:stun.l.google.com:19302
```

**服务器1（发送端）:**
```bash
./test_sender_http http://192.168.1.10:9355 test_session_1 500 65535 stun:stun.l.google.com:19302
```

### 示例2: 长时间测试，小消息

**服务器2:**
```bash
./test_receiver_http http://192.168.1.10:9355 test_session_2 1000 stun:stun.l.google.com:19302
```

**服务器1:**
```bash
./test_sender_http http://192.168.1.10:9355 test_session_2 1000 16384 stun:stun.l.google.com:19302
```

### 示例3: 不使用STUN（同一局域网）

**服务器2:**
```bash
./test_receiver_http http://192.168.1.10:9355 test_session_3 200
```

**服务器1:**
```bash
./test_sender_http http://192.168.1.10:9355 test_session_3 200 32768
```

### 示例4: 使用不同的会话ID同时运行多个测试

可以同时运行多个测试，只需使用不同的会话ID：

**测试1:**
- 接收端: `./test_receiver_http http://server:9355 session1 500`
- 发送端: `./test_sender_http http://server:9355 session1 500 65535`

**测试2:**
- 接收端: `./test_receiver_http http://server:9355 session2 750`
- 发送端: `./test_sender_http http://server:9355 session2 750 65535`

## 信令服务器API

信令服务器提供以下REST API端点：

- `POST /session/<id>/offer` - 设置Offer（发送端）
- `GET /session/<id>/offer` - 获取Offer（接收端）
- `POST /session/<id>/answer` - 设置Answer（接收端）
- `GET /session/<id>/answer` - 获取Answer（发送端）
- `POST /session/<id>/candidate/sender` - 添加发送端候选者
- `GET /session/<id>/candidate/sender` - 获取发送端候选者（接收端）
- `POST /session/<id>/candidate/receiver` - 添加接收端候选者
- `GET /session/<id>/candidate/receiver` - 获取接收端候选者（发送端）
- `DELETE /session/<id>` - 清理会话数据
- `GET /health` - 健康检查

## 输出说明

### 发送端输出

- 连接建立时间
- 每5秒的发送统计（消息数、速率）
- 最终统计（总发送量、平均速率）

### 接收端输出

- 连接建立时间
- 每5秒的接收统计（消息数、吞吐量）
- 最终统计（总接收量、平均速率、连接信息）

### 信令服务器输出

- 信令交换日志（哪个会话的Offer/Answer已设置/获取）

## 性能指标

- **吞吐量 (Throughput)**: 数据传输速率（MB/s 和 Mbps）
- **消息速率 (Message Rate)**: 每秒处理的消息数
- **连接延迟**: 从启动到建立连接的时间

## 故障排除

### 问题1: 无法连接到信令服务器

**可能原因:**
- 信令服务器未启动
- 网络连接问题
- 防火墙阻止

**解决方法:**
```bash
# 检查信令服务器是否运行
curl http://信令服务器IP:9355/health

# 检查网络连接
ping 信令服务器IP

# 检查防火墙
sudo ufw status  # Ubuntu
sudo firewall-cmd --list-all  # CentOS
```

### 问题2: 编译错误 - 找不到libcurl

**解决方法:**
```bash
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev

# CentOS/RHEL
sudo yum install libcurl-devel

# macOS
brew install curl
```

### 问题3: Python Flask模块未找到

**解决方法:**
```bash
pip3 install flask flask-cors

# 或使用虚拟环境
python3 -m venv venv
source venv/bin/activate
pip install flask flask-cors
```

### 问题4: 连接失败

**可能原因:**
- 防火墙阻止UDP端口
- 需要STUN/TURN服务器
- 网络配置问题

**解决方法:**
```bash
# 检查防火墙
sudo ufw allow 9300:9400/udp

# 使用TURN服务器（如果需要）
# 修改代码中的iceServers配置
```

### 问题5: 会话ID冲突

如果多个测试使用相同的会话ID，会导致信令混乱。

**解决方法:**
- 为每个测试使用唯一的会话ID
- 测试完成后清理会话: `curl -X DELETE http://server:9355/session/<id>`

## 网络要求

1. **HTTP端口**: 信令服务器需要HTTP端口（默认9355）可访问
2. **UDP端口**: 确保9300-9400端口范围未被占用且未被防火墙阻止
3. **STUN服务器**: 跨网络连接需要STUN服务器
4. **TURN服务器**: 如果NAT穿透失败，需要TURN服务器
5. **带宽**: 确保网络带宽足够支持测试数据量

## 安全注意事项

1. **生产环境**: 信令服务器当前没有认证机制，仅用于测试
2. **HTTPS**: 生产环境建议使用HTTPS（WSS）保护信令
3. **防火墙**: 限制信令服务器的访问来源
4. **会话清理**: 定期清理过期会话数据

## 高级配置

### 使用HTTPS信令服务器

修改`signaling_server.py`以支持HTTPS：

```python
from flask import Flask
import ssl

context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain('cert.pem', 'key.pem')
app.run(host='0.0.0.0', port=8443, ssl_context=context)
```

### 调整端口范围

在代码中修改：

```cpp
config.portRangeBegin = 10000;
config.portRangeEnd = 20000;
```

### 调整MTU

```cpp
config.mtu = 1200;  // 适合某些网络环境
```

## 注意事项

1. **启动顺序**: 建议先启动信令服务器，然后启动接收端，最后启动发送端
2. **会话ID**: 确保发送端和接收端使用相同的会话ID
3. **时间同步**: 确保所有服务器时间同步（使用NTP）
4. **资源监控**: 长时间测试时监控CPU和网络使用情况
5. **日志级别**: 可以通过修改`LogLevel::Warning`调整日志详细程度

## 与文件系统版本对比

| 特性 | 文件系统版本 | HTTP版本 |
|------|------------|---------|
| 设置复杂度 | 需要NFS/共享存储 | 只需HTTP访问 |
| 网络要求 | 需要共享文件系统 | 只需HTTP连接 |
| 可扩展性 | 受文件系统限制 | 易于扩展 |
| 适用场景 | 同一数据中心 | 跨网络环境 |

## 相关资源

- [libdatachannel文档](DOC.md)
- [构建说明](BUILDING.md)
- [项目README](README.md)
- [文件系统版本指南](PERFORMANCE_TEST_GUIDE.md)

