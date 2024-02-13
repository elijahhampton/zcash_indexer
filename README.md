# Zcash Blockchain Storage Module
A C++ module designed to store the Zcash blockchain based on a custom schema and DB (Postgres currently supported)

# Installation and Running Instructions

This guide provides instructions on how to build, install, and run the application using Docker Compose and a provided Makefile.

## Prerequisites

Before proceeding, ensure you have the following installed on your system:
- Docker
- Docker Compose
- Make (optional, for building the application using the Makefile)

## Configuration

1. Clone the repository to your local machine.

2. Ensure Docker and Docker Compose are installed and running on your system.

3. Set up environment variables for sensitive data such as `DB_PASSWORD`, `RPC_PASSWORD`, and `ZCASHD_RPCPASSWORD`. This can be done by creating a `.env` file in the root directory of the project with the following content:

    ```env
    
    DB_PASSWORD=your_db_password_here
    RPC_PASSWORD=your_rpc_password_here
    BLOCK_CHUNK_PROCESSING_SIZE=desired_block_chunk_processing_size
    
    If you are not running the indexer locally adjust as you see fit:
    DB_HOST=your_db_host_here
    DB_PORT=your_db_port_here
    DB_USER=your_db_user_here
    RPC_USER=your_rpc_username_here
    
    ```
## Building the Application

You can build the application using Docker Compose or the Makefile.

### Using Docker Compose

To build the application with Docker Compose, run the following command in the terminal from the root directory of the project:

```bash
docker-compose build

Using Makefile
To build the application using the Makefile, run the following command in the terminal from the root directory of the project:
make build

