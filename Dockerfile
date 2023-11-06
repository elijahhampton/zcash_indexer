# Use an official Ubuntu base image
FROM ubuntu:20.04

# Set the working directory in the container
WORKDIR /usr/src/app

# Avoid interactive dialogue with tzdata
ENV DEBIAN_FRONTEND=noninteractive

# Install basic build dependencies, clang compiler, and libraries
# Removed libjsoncpp-dev since we're installing it from vcpkg
RUN apt-get update && apt-get install -y \
    postgresql-client \
    clang \
    libpqxx-dev \
    libboost-system-dev \
    net-tools \
    libboost-filesystem-dev \
    libjsonrpccpp-dev \
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

# # Clone the vcpkg repository and install jsoncpp
# RUN git clone https://github.com/Microsoft/vcpkg.git /vcpkg \
#     && /vcpkg/bootstrap-vcpkg.sh \
#     && /vcpkg/vcpkg install jsoncpp

# # Set the environment variables for vcpkg so that CMake can find the installed libraries
# ENV VCPKG_ROOT=/vcpkg
# ENV PATH="${VCPKG_ROOT}/bin:${PATH}"
# ENV CMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"

# Copy the current directory contents into the container at /usr/src/app
COPY . .

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh
COPY ./postgresql.conf /var/lib/postgresql/data/postgresql.conf

# Set the environment variable for include directories
# Assuming vcpkg installs libraries to /vcpkg/installed/x64-linux/include
# ENV INCLUDES="-I${VCPKG_ROOT}/installed/x64-linux/include"

# Compile the application using clang and the Makefile
# Ensure your Makefile or build script is adjusted to use clang and the vcpkg toolchain file
# You might need to update the Makefile to include the custom INCLUDES environment variable
RUN make clean && CC=clang CXX=clang++ make INCLUDES="$INCLUDES"

#ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
# Run the application with the provided arguments when the container launches
# You will need to replace the placeholders with your actual arguments
CMD ["./syncer", "-printtoconsole"]
