#!/bin/bash

# Start a new PostgreSQL container
docker run --name postgres -e POSTGRES_PASSWORD=mysecretpassword -p 5432:5432 -d postgres