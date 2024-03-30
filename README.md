# Zcash Blockchain Indexer
Index the Zcash blockchain

## Prerequisites

Before proceeding, ensure you have the following installed on your system:
- Docker
- Docker Compose

## Configuration

1. Clone the repository to your local machine.

2. Ensure Docker and Docker Compose are installed and running on your system.

3. Declare the following environment variables: `CONFIG_PATH`
    ```env
    
    CONFIG_PATH=path_to_yaml_config
    ```

The config file should declare the following values:
```yaml

database:
  host: host.docker.internal
  port: "5432"
  name: postgres
  user: postgres
  password: password

rpc:
  url: host.docker.internal:18232
  username: user
  password: password

configuration:
  block_chunk_processing_size: 1000
  allow_multiple_threads: true


```
<br />

### Running with Docker Compose

This project can be run using Docker Compose, which simplifies the process of setting up and running the application along with its dependencies.
Prerequisites

Docker: Make sure you have Docker installed on your machine. You can download and install Docker from the official website: https://www.docker.com
Configuration

The docker-compose.yml file defines the services required to run the application. It includes the following services:

**app:**
<br />
The main application service, built using the provided Dockerfile. It sets the CONFIG_PATH environment variable to specify the location of the configuration file.

**zcash_zcashd:**
<br />
The Zcash daemon service, using the electriccoinco/zcashd image. It mounts volumes for data and parameters, and sets various environment variables for configuration. It also exposes ports for communication.

**postgres:**
<br />
The PostgreSQL database service, using the postgres:16 image. It mounts a volume for data persistence and sets environment variables for database configuration. It exposes the default PostgreSQL port.
<br />


### Building the application

``
docker-compose build
``

### Starting the Application

To start the application using Docker Compose, follow these steps:
Navigate to the project directory in your terminal.
Run the following command to start the services defined in the docker-compose.yml file:
``
docker-compose up
``

This command will build the necessary images (if not already built) and start the containers for each service.
Wait for the services to start up. You should see log output from each service in the terminal.

Once the services are up and running, you can access the application based on the exposed ports and configurations.
<br />

### Stopping the Application

To stop the application and remove the containers, press Ctrl+C in the terminal where Docker Compose is running. Alternatively, you can run the following command in a separate terminal:
bash

``
docker-compose down
``

This command will stop and remove the containers defined in the docker-compose.yml file.

<br />

### Current and Planned Features 
Currently the indexer supports and will support in the future the following features:
- ~~Basic chain indexing~~
- ~~Support for PostgresSQL~~
- ~~Multithreading Support~~
- Custom DB schema for indexing
