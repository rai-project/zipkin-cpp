FROM gcc:7
MAINTAINER Abdul Dakkak <dakkak@illinois.edu>

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        automake \
        autoconf \
        bison \
        flex \
        libevent-dev \
        libboost-all-dev \
        libssl-dev \
        libcurl4-openssl-dev \
        libdouble-conversion-dev \
        libgoogle-glog-dev \
        libgflags-dev \
        libjemalloc-dev \
        libssl-dev \
        cmake \
        thrift-compiler  && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*


ENV FOLLY_SRC=/src/folly
RUN git clone https://github.com/facebook/folly $FOLLY_SRC && cd $FOLLY_SRC && git fetch --all
WORKDIR $FOLLY_SRC/folly
RUN autoreconf -ivf && \
    ./configure --prefix=/usr/local && \
    make && \
    make -j`nproc` install


ENV LIBRDKAFKA_SRC=/src/librdkafka
RUN git clone https://github.com/edenhill/librdkafka $LIBRDKAFKA_SRC && cd $LIBRDKAFKA_SRC && git fetch --all
WORKDIR $LIBRDKAFKA_SRC
RUN ./configure --prefix=/usr/local && \
    make && \
    make -j`nproc` install

ENV ZIPKIN_CPP=/src/zipkin-cpp
ENV ZIPKIN_CPP_BUILD=/opt/zipkin-cpp
WORKDIR $ZIPKIN_CPP

ADD . $ZIPKIN_CPP

WORKDIR $ZIPKIN_CPP_BUILD

RUN cmake $ZIPKIN_CPP -DBUILD_DOCS=OFF

RUN make 