# Build environment for openwrt-dir650in (works on x86-64 and ARM64 hosts, e.g. Apple Silicon).
#   docker build -t dir650in-build .
#   docker volume create dir650in
#   docker run --rm -it -v dir650in:/work -v "$PWD":/src:ro dir650in-build \
#       sh -c 'cp -r /src /work/src 2>/dev/null; cd /work/src && JOBS=$(nproc) ./build.sh /work/build'
# Use a Docker volume (case-sensitive ext4) for /work - not a macOS/Windows folder.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential flex bison g++ gawk gettext git libncurses-dev libssl-dev \
      python3 python3-setuptools python3-dev rsync swig unzip zlib1g-dev file wget \
      zstd xsltproc xxd ca-certificates patch diffutils \
    && rm -rf /var/lib/apt/lists/*
RUN useradd -m bld && mkdir /work && chown bld: /work
USER bld
WORKDIR /work
