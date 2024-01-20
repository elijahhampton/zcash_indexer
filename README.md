# Zcash Blockchain Syncing Module
A C++ module designed for exploratory synchronization of the Zcash blockchain.

## Installation

## Installing Dependencies

Before building the project, ensure you have all the necessary libraries installed. If you are on macOS, you can use Homebrew to install these dependencies. Below are the commands to install the libraries listed in the Makefile:

Install openssl, json-rpc-cpp, jsoncpp, libpqxx, and the Boost libraries:
```bash
brew install openssl json-rpc-cpp jsoncpp libpqxx boost

# As json-rpc-cpp is not a common library, it might not be available via Homebrew. You might need to install it from source or find an alternative method.
```

Note:
The json-rpc-cpp, libpqxx, and jsoncpp libraries are specified here for installation via Homebrew, but please note that not all libraries may be available via Homebrew, or the library names may be different in Homebrew.
For libraries not available via Homebrew, you may need to download and install them manually from their respective websites or repositories.
If you're on a different operating system, you'll need to use its respective package manager or install the libraries manually.
Once you have installed all the necessary libraries, you can proceed to build the project as described in the previous section.

## Configuration and Local Setup

**Running PostgreSQL with Docker**

To run PostgreSQL locally using Docker, follow these steps:

**Install Docker**

If you haven't already, install Docker from the official website.

**Run PostgreSQL Container and Connect to PostgreSQL**
```bash
docker pull postgres
docker run --name some-postgres -e POSTGRES_PASSWORD=mysecretpassword -d postgres

```

## Building the Executable
To build the executable, simply navigate to the project directory in your terminal and run:
```bash
make
```
To clean up the build artificats, run:
```bash
make clean 
```

## Executing the program
```bash
./syncer
```

## Debugging with LLDB
### Launching LLDB with Target Executable:
This command launches LLDB and loads syncer as the target executable. LLDB should now be awaiting further commands.
```bash
lldb syncer
```

### Running the executable
This command tells LLDB to run the syncer executable. Execution will proceed until a breakpoint is hit, or the program exits or crashes.
```bash
run
```


