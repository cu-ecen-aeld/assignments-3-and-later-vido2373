#!/bin/bash

# Check num of args
if [ $# -ne 2 ]
then
	echo Incorrect number of arguments: 2 required
	exit 1
elif [ ! -d $1 ]
then
	echo $1 is not a directory
	exit 1
fi

x=$(find $1 -mindepth 1 | wc -l)
y=$(grep -r $2 $1 | wc -l)

echo "The number of files are $x and the number of matching lines are $y"
