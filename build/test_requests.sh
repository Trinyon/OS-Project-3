#!/bin/bash

echo "Sending requests on port 10000"

# send all 15 requests to port 10000
for i in {1..15}; do
    curl -s http://localhost:10000/files/test$i.html &
done


wait

echo "All 15 requests completed."