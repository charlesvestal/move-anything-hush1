FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    build-essential \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --break-system-packages pytest numpy

WORKDIR /build
