#!/bin/sh

a="result_"
for i in $(seq 1 300)
do
   b=""
   b=$a$i
   make clean
   make

   cd build
   make check >> ../$b

   cd .. 
done
