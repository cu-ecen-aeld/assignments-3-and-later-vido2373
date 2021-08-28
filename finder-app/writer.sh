#!/bin/bash

if [ $# -ne 2 ]
then
	echo Incorrect number of arguments: 2 needed
	exit 1
elif [ ! -e $1 ]
then
	mkdir -p $( dirname "$1" ) && touch "$1"
fi

touch $1

echo $2 > $1
