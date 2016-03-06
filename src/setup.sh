#!/bin/sh
set -e;
rm -rf ~/csc369;
mkdir ~/csc369;
mkdir ~/csc369/root;
chmod +x configure;
./configure;
cp run.sh ~/csc369/root;
cp .gdbinit ~/csc369/root;
cp sys161.conf ~/csc369/root;
chmod +x compile.sh;
chmod +x user/lib/libc/syscalls/gensyscalls.sh;
chmod +x user/testbin/randcall/gencalls.sh;
chmod +x mk/installheaders.sh
./compile.sh;
cd ~/csc369/root;
hostbin/host-mksfs hd0 MYFS;
