#!/bin/sh
# Builds the hotpatch fixtures (run inside the walkero OS4 GCC image). $1 = output dir.
set -e
src=$(cd "$(dirname "$0")" && pwd); out=$1
F="-mcrt=newlib -mcpu=440fp -O0 -gstabs+ -Wall"
mkdir -p $out/v1 $out/v2 $out/newvar $out/grow $out/newlib $out/cpp1 $out/cpp2
cp $src/compute.c $out/v1/compute.c; cp $src/main.c $out/v1/main.c
cp $src/compute_v2.c $out/v2/compute.c
cp $src/compute_newvar.c $out/newvar/compute.c
cp $src/compute_grow.c $out/grow/compute.c
cp $src/compute_newlib.c $out/newlib/compute.c
for d in v1 v2 newvar grow newlib; do (cd $out/$d && ppc-amigaos-gcc $F -c compute.c -o compute.o); done
(cd $out/v1 && ppc-amigaos-gcc $F -c main.c -o main.o && ppc-amigaos-gcc -mcrt=newlib -o prog main.o compute.o -lauto)
cp $src/shape.h $src/shapes_main.cpp $out/cpp1/; cp $src/shape.cpp $out/cpp1/shape.cpp
cp $src/shape.h $out/cpp2/; cp $src/shape_v2.cpp $out/cpp2/shape.cpp
(cd $out/cpp1 && ppc-amigaos-g++ $F -c shape.cpp -o shape.o && ppc-amigaos-g++ $F -c shapes_main.cpp -o main.o &&
  ppc-amigaos-g++ -mcrt=newlib -athread=native -o prog main.o shape.o -lauto)
(cd $out/cpp2 && ppc-amigaos-g++ $F -c shape.cpp -o shape.o)
touch $out/.fixtures
