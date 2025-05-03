#!/bin/bash

echo "Starting concurrent requests for test1.html to test15.html on port 10000..."

# Send all 15 requests concurrently to port 10000
for i in {1..15}; do
    curl -s http://localhost:10000/files/test$i.html &
done

# Wait for all background requests to finish
wait

echo "All 15 requests completed."