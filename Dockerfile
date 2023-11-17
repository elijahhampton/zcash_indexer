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
    libboost-system-dev \
    net-tools \
    libboost-filesystem-dev \
    libjsonrpccpp-dev \
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

COPY . .

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh
COPY ./postgresql.conf /var/lib/postgresql/data/postgresql.conf

RUN CXX=clang++ make clean && make

#ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["./syncer", "-printtoconsole"]
