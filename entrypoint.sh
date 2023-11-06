#!/bin/bash
set -e

# Wait for PostgreSQL to be ready
while ! pg_isready -h "$POSTGRES_HOST" -p "$POSTGRES_PORT" -U "$POSTGRES_USER"; do
    echo "Waiting for PostgreSQL to be ready..."
    sleep 1
done

# Check if the 'postgres' database exists, and create it if it doesn't.
psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" <<-EOSQL
    SELECT 'CREATE DATABASE postgres'
    WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'postgres')\gexec
EOSQL

echo "PostgreSQL is ready and the 'postgres' database is set up."
