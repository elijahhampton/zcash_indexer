GCR_HOST = gcr.io
PROJECT_ID = sigma-scheduler-405523
REPO_NAME = be-sync
IMAGE_TAG = latest

IMAGE = $(GCR_HOST)/$(PROJECT_ID)/$(REPO_NAME):$(IMAGE_TAG)

CXX = clang++

CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -g

INCLUDES = -I/usr/local/include \
           -I/usr/local/opt/openssl/include \
           -I/usr/local/include/jsonrpccpp \
           -I/usr/local/include/json \
           -I/usr/local/include/boost

LIBDIRS = -L/usr/local/lib \
          -L/usr/local/opt/openssl/lib

LIBS = -ljsonrpccpp-common \
       -ljsonrpccpp-client \
       -ljsonrpccpp-server \
       -lboost_serialization \
       -ljsoncpp \
       -ljsonrpccpp-stub \
       -lpqxx \
       -lcrypto \
       -lboost_filesystem \
       -lboost_thread-mt \
       -lboost_system \
       -lpthread -ldl -lm

CXX_SRCS = src/syncer.cpp src/logger.cpp src/thread_pool.cpp src/controller.cpp src/database.cpp src/httpclient.cpp

CXX_OBJS = $(CXX_SRCS:.cpp=.o)

TARGET = syncer

$(TARGET): $(CXX_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(CXX_OBJS) $(LIBDIRS) $(LIBS)

.cpp.o:
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

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

