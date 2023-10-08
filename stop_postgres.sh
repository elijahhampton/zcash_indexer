#!/bin/bash

# Define PostgreSQL credentials
POSTGRES_USER=postgres
POSTGRES_DB=postgres
CONTAINER_NAME=postgres

# Revoke connections and disconnect active sessions from the target database
docker exec -it $CONTAINER_NAME psql -U $POSTGRES_USER -d template1 -c "REVOKE CONNECT ON DATABASE $POSTGRES_DB FROM public;"
docker exec -it $CONTAINER_NAME psql -U $POSTGRES_USER -d template1 -c "SELECT pg_terminate_backend(pg_stat_activity.pid) FROM pg_stat_activity WHERE pg_stat_activity.datname = '$POSTGRES_DB';"

# Drop the target databasexw
docker exec -it $CONTAINER_NAME psql -U $POSTGRES_USER -d template1 -c "DROP DATABASE $POSTGRES_DB;"

# Recreate the target database
docker exec -it $CONTAINER_NAME psql -U $POSTGRES_USER -d template1 -c "CREATE DATABASE $POSTGRES_DB;"

# Stop and remove the PostgreSQL container
docker stop $CONTAINER_NAME
docker rm $CONTAINER_NAME
