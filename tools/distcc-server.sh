#!/bin/sh
# distcc-server.sh [start|stop|status|logs] — run a distccd that compiles for AmigaOS 4 (PPC),
# for AmiForge's distributed builds (Build > Distributed Builds).
#
# It runs in Docker on the machine that runs QEMU (or any machine the Amiga can reach), with
# the adtools GCC 11 cross-compiler from walkero/amigagccondocker. The Amiga's distcc asks for
# "gcc" / "g++"; here those names are links to ppc-amigaos-gcc / ppc-amigaos-g++ of the same
# GCC version, so the objects are exactly what the Amiga's own compiler would make.
#
# Under QEMU's user networking the Amiga reaches this machine at its gateway address
# (10.0.2.2 by default; 192.168.101.2 etc. with net=192.168.101.0/24), which QEMU forwards to
# this machine's 127.0.0.1 — so the port is only published on localhost. To serve real
# Amigas on the LAN, set DISTCC_LISTEN=0.0.0.0 and DISTCC_ALLOW to your network.
#
# Environment: DISTCC_PORT (3632), DISTCC_LISTEN (127.0.0.1), DISTCC_ALLOW (0.0.0.0/0: the
# connections come from Docker's own network), DISTCC_JOBS (CPU count), PPC_IMAGE.
set -e
PORT=${DISTCC_PORT:-3632}
LISTEN=${DISTCC_LISTEN:-127.0.0.1}
ALLOW=${DISTCC_ALLOW:-0.0.0.0/0}
JOBS=${DISTCC_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
BASE=${PPC_IMAGE:-walkero/amigagccondocker:os4-gcc11}
IMAGE=amiforge-distccd
NAME=amiforge-distccd

build_image() {
    if docker image inspect $IMAGE >/dev/null 2>&1; then return; fi
    echo "Building the $IMAGE image (once)..."
    docker build -t $IMAGE - <<EOF
FROM $BASE
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends distcc && rm -rf /var/lib/apt/lists/*
# The names the Amiga's distcc sends, mapped to the cross-compiler
RUN mkdir -p /opt/amiga-distcc && cd /opt/amiga-distcc && \\
    for n in gcc cc gcc-11 ppc-amigaos-gcc; do ln -sf /opt/ppc-amigaos/bin/ppc-amigaos-gcc \$n; done && \\
    for n in g++ c++ g++-11 ppc-amigaos-g++; do ln -sf /opt/ppc-amigaos/bin/ppc-amigaos-g++ \$n; done
ENV PATH=/opt/amiga-distcc:/opt/ppc-amigaos/bin:\$PATH
# distccd 3.2+ only runs compilers from its masquerade directory unless --enable-tcp-insecure
# is given; that's fine here, the port is only reachable from this machine (and QEMU).
EXPOSE 3632
CMD ["sh", "-c", "exec distccd --daemon --no-detach --log-stderr --log-level info --allow \$DISTCC_ALLOW --jobs \$DISTCC_JOBS --enable-tcp-insecure"]
EOF
}

case ${1:-start} in
start)
    build_image
    docker rm -f $NAME >/dev/null 2>&1 || true
    docker run -d --name $NAME --restart unless-stopped -p $LISTEN:$PORT:3632 \
        -e DISTCC_ALLOW=$ALLOW -e DISTCC_JOBS=$JOBS $IMAGE >/dev/null
    echo "distccd for AmigaOS 4 PPC is listening on $LISTEN:$PORT ($JOBS jobs)."
    echo "In AmiForge: Build > Distributed Builds, hosts: <this machine as the Amiga sees it>, e.g. 10.0.2.2"
    ;;
stop) docker rm -f $NAME >/dev/null 2>&1 && echo stopped || echo "not running" ;;
status) docker ps --filter name=$NAME --format '{{.Names}}  {{.Status}}  {{.Ports}}' ;;
logs) docker logs --tail ${2:-50} $NAME ;;
*) echo "usage: $0 [start|stop|status|logs]"; exit 1 ;;
esac
