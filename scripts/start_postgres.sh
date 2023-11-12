#!/bin/bash

# Start a new PostgreSQL container
docker run --name postgres -e POSTGRES_PASSWORD=mysecretpassword -p 5432:5432 -d postgres

# Optionally, you can also specify a version like this
# docker run --name my_postgres -e POSTGRES_PASSWORD=mysecretpassword -p 5432:5432 -d postgres:11
