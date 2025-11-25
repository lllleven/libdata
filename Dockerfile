# libdatachannel 性能测试 Docker镜像
# 包含信令服务器、发送端和接收端

FROM ubuntu:22.04 AS builder

# 避免交互式提示
ENV DEBIAN_FRONTEND=noninteractive

# 配置APT镜像源（可选，如果需要可以使用国内镜像）
# RUN sed -i 's|http://archive.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list && \
#     sed -i 's|http://ports.ubuntu.com|http://mirrors.aliyun.com|g' /etc/apt/sources.list || true

# 安装构建依赖（添加重试机制和错误处理）
RUN apt-get update --fix-missing && \
    apt-get install -y --fix-missing --no-install-recommends \
    build-essential \
    cmake \
    git \
    libssl-dev \
    libcurl4-openssl-dev \
    python3 \
    python3-pip \
    pkg-config \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# 安装Python依赖
RUN pip3 install flask flask-cors

# 设置工作目录
WORKDIR /app

# 复制源代码
COPY . /app/

# 初始化子模块
RUN git submodule update --init --recursive --depth 1 || true

# 构建libdatachannel（关闭示例以避免缺失目录）
RUN cmake -B build \
    -DUSE_GNUTLS=0 \
    -DUSE_NICE=0 \
    -DNO_EXAMPLES=1 \
    -DCMAKE_BUILD_TYPE=Release \
    && cd build && make -j$(nproc)

# 编译测试程序
RUN g++ -std=c++17 test_sender_http.cpp -o test_sender_http \
    -I./include \
    -L./build \
    -Wl,-rpath,/usr/local/lib \
    -ldatachannel \
    -pthread \
    -lssl -lcrypto \
    -lcurl \
    && g++ -std=c++17 test_receiver_http.cpp -o test_receiver_http \
    -I./include \
    -L./build \
    -Wl,-rpath,/usr/local/lib \
    -ldatachannel \
    -pthread \
    -lssl -lcrypto \
    -lcurl

# 运行时镜像
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# 安装运行时依赖（添加重试机制）
RUN apt-get update --fix-missing && \
    apt-get install -y --fix-missing --no-install-recommends \
    libssl3 \
    libcurl4 \
    python3 \
    python3-pip \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# 安装Python依赖
RUN pip3 install flask flask-cors

# 设置工作目录
WORKDIR /app

# 从构建阶段复制文件
COPY --from=builder /app/signaling_server.py /app/
COPY --from=builder /app/test_sender_http /app/
COPY --from=builder /app/test_receiver_http /app/

# 复制库文件
COPY --from=builder /app/build/libdatachannel.so* /usr/local/lib/

# 更新库路径
RUN ldconfig

# 设置可执行权限
RUN chmod +x /app/signaling_server.py /app/test_sender_http /app/test_receiver_http

# 默认启动信令服务器
EXPOSE 9355
CMD ["python3", "/app/signaling_server.py", "9355"]

