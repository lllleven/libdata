# Docker构建故障排除指南

## 网络连接问题

### 问题：无法从Ubuntu仓库下载包

**错误信息**:
```
E: Failed to fetch http://ports.ubuntu.com/... Connection failed
E: Unable to fetch some archives, maybe run apt-get update or try with --fix-missing?
```

**解决方法1: 使用国内镜像源（推荐）**

如果在中国，使用`Dockerfile.cn`：

```bash
# 使用国内镜像源构建
docker build -t libdatachannel-test:latest -f Dockerfile.cn .

# 或使用Makefile
make build-cn

# 或使用脚本
USE_CN_MIRROR=true ./build_and_push.sh
```

**解决方法2: 添加重试和修复选项**

已更新的Dockerfile已包含`--fix-missing`选项，如果仍然失败：

```bash
# 手动重试
docker build --no-cache -t libdatachannel-test:latest -f Dockerfile .
```

**解决方法3: 使用代理**

如果使用代理：

```bash
# 设置Docker代理
mkdir -p ~/.docker
cat > ~/.docker/config.json <<EOF
{
  "proxies": {
    "default": {
      "httpProxy": "http://proxy.example.com:8080",
      "httpsProxy": "http://proxy.example.com:8080"
    }
  }
}
EOF

# 或在构建时指定代理
docker build \
  --build-arg http_proxy=http://proxy.example.com:8080 \
  --build-arg https_proxy=http://proxy.example.com:8080 \
  -t libdatachannel-test:latest -f Dockerfile .
```

**解决方法4: 分步构建**

如果网络不稳定，可以分步构建：

```dockerfile
# 先更新包列表
RUN apt-get update --fix-missing

# 再安装包（可以分批安装）
RUN apt-get install -y build-essential cmake git
RUN apt-get install -y libssl-dev libcurl4-openssl-dev
RUN apt-get install -y python3 python3-pip pkg-config
```

## 其他常见问题

### 问题：构建超时

**解决方法**:
```bash
# 增加构建超时时间
docker build --network=host -t libdatachannel-test:latest -f Dockerfile .

# 或使用BuildKit（更快）
DOCKER_BUILDKIT=1 docker build -t libdatachannel-test:latest -f Dockerfile .
```

### 问题：内存不足

**解决方法**:
```bash
# 限制并行构建任务数
docker build --build-arg BUILDKIT_INLINE_CACHE=1 \
  --build-arg PARALLEL_JOBS=2 \
  -t libdatachannel-test:latest -f Dockerfile .
```

### 问题：架构不匹配

如果构建ARM64镜像但系统是x86_64：

```bash
# 使用buildx进行跨平台构建
docker buildx create --use
docker buildx build --platform linux/arm64 -t libdatachannel-test:latest -f Dockerfile .
```

## 快速修复命令

```bash
# 清理Docker缓存
docker builder prune -a

# 重新构建（不使用缓存）
docker build --no-cache -t libdatachannel-test:latest -f Dockerfile .

# 使用国内镜像源
docker build -t libdatachannel-test:latest -f Dockerfile.cn .

# 查看构建日志
docker build --progress=plain -t libdatachannel-test:latest -f Dockerfile .
```

## 推荐的构建方式

### 在中国大陆

```bash
# 使用国内镜像源
docker build -t libdatachannel-test:latest -f Dockerfile.cn .
```

### 在其他地区

```bash
# 使用标准Dockerfile
docker build -t libdatachannel-test:latest -f Dockerfile .

# 如果网络不稳定，添加重试
docker build --network=host -t libdatachannel-test:latest -f Dockerfile .
```

## 验证构建

构建完成后验证镜像：

```bash
# 检查镜像大小
docker images libdatachannel-test:latest

# 检查镜像内容
docker run --rm libdatachannel-test:latest ls -la /app

# 测试运行
docker run --rm libdatachannel-test:latest /app/test_sender_http --help
```

