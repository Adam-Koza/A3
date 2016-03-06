#!/bin/sh
# Purpose: re-compile sys161 from source, run in /SRC directory. (assumes that ostree=$HOME/csc369/root already configured.)
set -e;
cd kern/conf;
./config ASST3;
cd ../compile/ASST3;
bmake depend;
bmake;
bmake install;
cd ../../..;
bmake;
bmake install;
