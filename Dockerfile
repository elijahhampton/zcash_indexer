# Use an official Ubuntu base image
FROM ubuntu:20.04

# Set the working directory in the container
WORKDIR /usr/src/app

# Avoid interactive dialogue with tzdata
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    postgresql-client \
    clang \
    libpqxx-dev \
    libboost-all-dev \
    libboost-system-dev \
    net-tools \
    libyaml-cpp-dev \
    libboost-filesystem-dev \
    libjsonrpccpp-dev \
    libboost-thread-dev \ 
    libboost-serialization-dev \
    postgresql \
    postgresql-contrib \
    libjsonrpccpp-tools \
    libssl-dev \
    make \
    git \
    cmake \
    curl \
    zip \
    && rm -rf /var/lib/apt/lists/*

# Install spdlog
RUN git clone --branch v1.9.2 --depth 1 https://github.com/gabime/spdlog.git /usr/local/src/spdlog \
    && cd /usr/local/src/spdlog \
    && mkdir build && cd build \
    && cmake .. && make -j$(nproc) install

RUN ldconfig -p | grep boost_log

COPY . .

COPY ./postgresql.conf /var/lib/postgresql/data/postgresql.conf

RUN CXX=clang++ make clean && make

CMD ["./syncer", "-printtoconsole"]
