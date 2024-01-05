#!/bin/bash

# Wait for one minute
echo "Waiting for one minute..."
sleep 60

# Start the main process
exec "$@"
