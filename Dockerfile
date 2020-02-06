FROM i386/ubuntu

RUN apt-get -y update && apt-get install -y \
    gcc \
    git \
    make \
    pkg-config \
	libipt-dev \
	libunwind8-dev \
	binutils-dev \
	gdb \
	linux-tools-common linux-tools-generic \
&& rm -rf /var/lib/apt/lists/* && rm -rf /honggfuzz

RUN git clone -n --depth=1 https://github.com/skirge/honggfuzz.git

WORKDIR /honggfuzz

RUN	git checkout && \
	make && \
	make install 

