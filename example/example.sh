#! /bin/bash

echo "What is your name?"
echo "Enter your name: "
read
echo "Hi" ${REPLY}
sleep 1
>&2 echo "Sample error"
