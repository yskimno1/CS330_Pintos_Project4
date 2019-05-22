#!/bin/sh
make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
make check
cd ..
make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
make check
cd ..
make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
make check
cd ..
make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
make check
cd ..
make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
make check
cd ..
make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
make check
cd ..
make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
make check
cd ..
make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
make check
cd ..
make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
make check
cd ..
make clean
make
cd build
pintos-mkdisk fs.dsk 2
pintos -f -q
make check
cd ..

