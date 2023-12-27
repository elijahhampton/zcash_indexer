GCR_HOST = gcr.io
PROJECT_ID = sigma-scheduler-405523
REPO_NAME = be-sync
IMAGE_TAG = latest

IMAGE = $(GCR_HOST)/$(PROJECT_ID)/$(REPO_NAME):$(IMAGE_TAG)

# Compiler
CXX = clang++

# Compiler flags
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -g

# Adjust the order of include paths so that the correct version of jsoncpp is included first
# If libjsonrpccpp brings its own version, you may need to specify that path instead of the general /usr/local/include/json
INCLUDES = -I/usr/local/include \
           -I/usr/local/opt/openssl/include \
           -I/usr/local/include/jsonrpccpp \
           -I/usr/local/include/json \
           -I/usr/local/include/boost

# Library directories
LIBDIRS = -L/usr/local/lib \
          -L/usr/local/opt/openssl/lib

# Libraries to link against
LIBS = -ljsonrpccpp-common \
       -ljsonrpccpp-client \
       -ljsonrpccpp-server \
       -lboost_serialization \
       -ljsoncpp \
       -ljsonrpccpp-stub \
       -lpthread \
       -lpqxx \
       -lcrypto \
       -lboost_system \
       -lboost_filesystem \
       -lpthread -ldl -lm

# Source files
CXX_SRCS = src/syncer.cpp src/controller.cpp src/database.cpp src/httpclient.cpp

# Object files
CXX_OBJS = $(CXX_SRCS:.cpp=.o)

# Executable name
TARGET = syncer

# Build rule
$(TARGET): $(CXX_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(CXX_OBJS) $(LIBDIRS) $(LIBS)

# Object file compilation rule for C++
.cpp.o:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean rule
clean:
	rm -f $(CXX_OBJS) $(TARGET)

build: 
	docker build -t $(IMAGE) .

tag:
	docker tag $(IMAGE) $(IMAGE)

push:
	docker push $(IMAGE)

run:
	docker run $(IMAGE)

