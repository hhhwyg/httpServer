FROM ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    bash build-essential cmake ninja-build g++ liburing-dev libssl-dev \
    default-libmysqlclient-dev && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
CMD ["bash"]
