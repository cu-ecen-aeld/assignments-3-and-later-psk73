#!/bin/sh
# Finder script
# Author: Sreekanth P
set -e
set -u

if [ $# -eq  2 ];
then
	FILESDIR=$1
	SEARCHSTR=$2
else
	echo "Usage: $0 <path-to-dir-on-filesystem> <text-string-to-search>"
	exit 1
fi

numfiles=`grep -Rl ${SEARCHSTR} ${FILESDIR}|wc -l`
numlines=`grep -R ${SEARCHSTR} ${FILESDIR}|wc -l`
echo "The number of files are ${numfiles} and the number of matching lines are ${numlines}"
