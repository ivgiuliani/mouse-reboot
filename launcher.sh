#!/bin/bash
pgrep tracktrack
RETVAL=$?

if [ $RETVAL -ne 0 ]; then
	./tracktrack &
fi
