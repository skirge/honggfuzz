FROM ubuntu

ENV DEBIAN_FRONTEND noninteractive

RUN apt-get -y update && apt-get install -y \
build-essential zlib1g-dev pkg-config libglib2.0-dev binutils-dev libboost-all-dev autoconf libtool libssl-dev libpixman-1-dev libpython-dev python-pip python-capstone virtualenv \
	    gcc \
	    git \
	    make \
	    pkg-config \
	    libipt-dev \
	    libunwind8-dev \
	    binutils-dev \
	    gdb \
	    linux-tools-common \ 
	    linux-tools-generic \
	    libglib2.0-dev \
	    libglib2.0-dev-bin \
	    && rm -rf /var/lib/apt/lists/* && rm -rf /honggfuzz

RUN git clone -n --depth=1 https://github.com/skirge/honggfuzz.git && \
	cd /honggfuzz && \
	git checkout 956d022474310f6150165633745d302f8841f289

WORKDIR /honggfuzz

RUN	make && \
	make install  && \
	cd qemu_mode && \
	make && \
	sed -i -e 's:^#define HFUZZ_FORKSERVER$://#define HFUZZ_FORKSERVER:g' honggfuzz-qemu/fuzz/config.h && \
	cd honggfuzz-qemu && \
	make -j17

