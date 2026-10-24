#!/bin/zsh
# build.sh [make args] — cross-compile AmiForge for AmigaOS 4.1 PPC in the walkero GCC 11 Docker image.
root=${0:A:h:h}
exec docker run --rm -v $root:/src -w /src ${PPC_IMAGE:-walkero/amigagccondocker:os4-gcc11} make -j${JOBS:-8} "$@"
