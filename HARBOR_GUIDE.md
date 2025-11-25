# Harbor镜像推送指南

本指南说明如何将libdatachannel测试镜像构建并推送到Harbor私有仓库。

## Harbor信息

- **Harbor地址**: `harbor.changyinlive.com`
- **项目名称**: `web3_capell`
- **完整镜像路径**: `harbor.changyinlive.com/web3_capell/libdatachannel-test:tag`

## 快速开始

### 方式1: 使用脚本（推荐）

```bash
# 使用默认标签（latest）
./build_and_push.sh

# 指定标签
./build_and_push.sh v1.0.0
```

### 方式2: 使用Makefile

```bash
# 构建并推送（使用默认标签latest）
make build-push

# 指定标签
make build-push TAG=v1.0.0

# 分步执行
make build          # 构建镜像
make tag TAG=v1.0.0 # 标记镜像
make push TAG=v1.0.0 # 推送镜像
```

### 方式3: 手动执行

```bash
# 1. 登录Harbor
docker login harbor.changyinlive.com

# 2. 构建镜像
docker build -t libdatachannel-test:latest -f Dockerfile .

# 3. 标记镜像
docker tag libdatachannel-test:latest \
  harbor.changyinlive.com/web3_capell/libdatachannel-test:latest

# 4. 推送镜像
docker push harbor.changyinlive.com/web3_capell/libdatachannel-test:latest
```

## 详细步骤

### 步骤1: 登录Harbor

首次使用需要登录Harbor：

```bash
docker login harbor.changyinlive.com
```

输入您的Harbor用户名和密码。

### 步骤2: 构建镜像

```bash
docker build -t libdatachannel-test:latest -f Dockerfile .
```

构建过程可能需要几分钟，取决于网络速度和系统性能。

### 步骤3: 标记镜像

将本地镜像标记为Harbor仓库格式：

```bash
docker tag libdatachannel-test:latest \
  harbor.changyinlive.com/web3_capell/libdatachannel-test:latest
```

### 步骤4: 推送镜像

```bash
docker push harbor.changyinlive.com/web3_capell/libdatachannel-test:latest
```

## 使用不同标签

### 使用版本号

```bash
# 构建并推送v1.0.0版本
./build_and_push.sh v1.0.0

# 或使用Makefile
make build-push TAG=v1.0.0
```

### 使用日期标签

```bash
TAG=$(date +%Y%m%d)
./build_and_push.sh $TAG
```

### 使用Git提交哈希

```bash
TAG=$(git rev-parse --short HEAD)
./build_and_push.sh $TAG
```

## 从Harbor拉取镜像

在其他服务器上使用镜像：

```bash
# 登录Harbor
docker login harbor.changyinlive.com

# 拉取镜像
docker pull harbor.changyinlive.com/web3_capell/libdatachannel-test:latest

# 运行容器
docker run -it --rm \
  harbor.changyinlive.com/web3_capell/libdatachannel-test:latest \
  /app/test_sender_http http://server:9355 test_session_1 500 65535
```

## 更新docker-compose.yml使用Harbor镜像

修改`docker-compose.yml`：

```yaml
services:
  signaling:
    image: harbor.changyinlive.com/web3_capell/libdatachannel-test:latest
    # ... 其他配置

  receiver:
    image: harbor.changyinlive.com/web3_capell/libdatachannel-test:latest
    # ... 其他配置

  sender:
    image: harbor.changyinlive.com/web3_capell/libdatachannel-test:latest
    # ... 其他配置
```

然后直接运行：

```bash
docker-compose pull  # 拉取镜像
docker-compose up    # 启动服务
```

## 故障排除

### 问题1: 登录失败

**错误**: `unauthorized: authentication required`

**解决方法**:
```bash
# 检查用户名和密码
docker login harbor.changyinlive.com

# 如果使用HTTPS，可能需要配置证书
# 或者使用HTTP（如果Harbor配置允许）
```

### 问题2: 推送被拒绝

**错误**: `denied: requested access to the resource is denied`

**解决方法**:
1. 确认您有`web3_capell`项目的推送权限
2. 检查镜像名称是否正确
3. 联系Harbor管理员分配权限

### 问题3: 网络连接问题

**错误**: `dial tcp: lookup harbor.changyinlive.com`

**解决方法**:
```bash
# 检查DNS解析
nslookup harbor.changyinlive.com

# 检查网络连接
ping harbor.changyinlive.com

# 如果使用代理，配置Docker代理
```

### 问题4: 镜像大小限制

如果镜像太大，Harbor可能有大小限制：

```bash
# 检查镜像大小
docker images libdatachannel-test:latest

# 优化镜像大小（使用多阶段构建，已在Dockerfile中实现）
```

## 最佳实践

1. **使用语义化版本**: `v1.0.0`, `v1.0.1`等
2. **保留latest标签**: 始终推送latest标签指向最新版本
3. **定期清理**: 删除不再使用的旧版本镜像
4. **使用CI/CD**: 在CI/CD流水线中自动构建和推送

## 自动化脚本示例

### GitHub Actions

```yaml
name: Build and Push to Harbor

on:
  push:
    tags:
      - 'v*'

jobs:
  build-and-push:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      
      - name: Login to Harbor
        run: |
          echo ${{ secrets.HARBOR_PASSWORD }} | docker login \
            harbor.changyinlive.com \
            -u ${{ secrets.HARBOR_USERNAME }} \
            --password-stdin
      
      - name: Build and Push
        run: |
          docker build -t libdatachannel-test:${{ github.ref_name }} .
          docker tag libdatachannel-test:${{ github.ref_name }} \
            harbor.changyinlive.com/web3_capell/libdatachannel-test:${{ github.ref_name }}
          docker tag libdatachannel-test:${{ github.ref_name }} \
            harbor.changyinlive.com/web3_capell/libdatachannel-test:latest
          docker push harbor.changyinlive.com/web3_capell/libdatachannel-test:${{ github.ref_name }}
          docker push harbor.changyinlive.com/web3_capell/libdatachannel-test:latest
```

## 相关文件

- `build_and_push.sh` - 自动化构建和推送脚本
- `Makefile` - Make命令定义
- `Dockerfile` - Docker镜像构建文件
- `docker-compose.yml` - Docker Compose配置

## 参考

- [Harbor文档](https://goharbor.io/docs/)
- [Docker Registry文档](https://docs.docker.com/registry/)

