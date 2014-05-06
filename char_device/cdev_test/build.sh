#!/bin/bash

file_path="cdev_test"

if [ -f $file_path ]; then
	rm -f $file_path
	echo "delete file" $file_path
else
	echo "file" $file_path "not found"
fi
#rm -f cdev_test

echo "start to build" $file_path
gcc -o $file_path cdev_test.c
echo "build end of" $file_path
