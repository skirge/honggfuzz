FROM i386/ubuntu

RUN apt-get -y update && apt-get install -y \
    gcc \
    git \
    make \
    pkg-config \
	libipt-dev \
	libunwind8-dev \
	binutils-dev \
&& rm -rf /var/lib/apt/lists/* && rm -rf /honggfuzz

RUN git clone -n --depth=1 https://github.com/skirge/honggfuzz.git

WORKDIR /honggfuzz

RUN	git checkout 85e7a43a02d50f3a04b36d766346481332168532 && make && cp /honggfuzz/honggfuzz /bin

