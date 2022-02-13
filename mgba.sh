#!/bin/bash

(
	sleep 4
	echo "started"
)&

/mnt/c/"Program Files"/mGBA/mGBA.exe $1 -g