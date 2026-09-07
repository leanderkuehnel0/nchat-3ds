#!/bin/sh
set -e
cd "$(dirname "$0")"
cc -std=c99 -D_GNU_SOURCE -Wall -Wextra -I ../source -I . \
	-o uitest uitest.c stub.c fakebvg.c
./uitest