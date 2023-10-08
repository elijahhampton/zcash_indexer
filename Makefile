# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -g

# Include directories
INCLUDES = -I/usr/local/include \
           -I/usr/local/opt/openssl/include \
           -I/usr/local/include/boost \
           -I/usr/local/include/jsonrpccpp \
           -I/usr/local/include/json

# Library directories
LIBDIRS = -L/usr/local/lib \
          -L/usr/local/opt/openssl/lib

# Libraries to link against
LIBS = -ljsonrpccpp-common \
       -ljsonrpccpp-client \
       -ljsonrpccpp-server \
       -ljsoncpp \
       -ljsonrpccpp-stub \
       -lpthread \
       -lpqxx \
       -lcrypto \
       -lboost_system \
       -lboost_filesystem \
       -lpthread -ldl -lm

# Source files
CXX_SRCS = src/syncer.cpp src/controller.cpp src/database.cpp src/httpclient.cpp src/block.cpp

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
	rm -f $(CXX_OBJS) $(C_OBJS) $(TARGET)
