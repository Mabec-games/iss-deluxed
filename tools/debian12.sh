#!/bin/sh
# Run a command inside a Debian 12 build root with bubblewrap, rootless, so the
# AppImage links against an older glibc and runs on Debian 12+, Ubuntu 22.04+
# and similar. The project is mounted at /src. The root is the one prepared
# for the ISS 2000 recomp unless DEBIAN12_ROOT points elsewhere.
# Usage: tools/debian12.sh <command...>
ROOT=$(cd "$(dirname "$0")/.." && pwd)
R="${DEBIAN12_ROOT:-$ROOT/../../iss2000/ext/debian12/root}"
[ -d "$R/usr" ] || { echo "Debian 12 root not found at $R (set DEBIAN12_ROOT)" >&2; exit 1; }
exec bwrap --unshare-user --uid 0 --gid 0 --unshare-pid --unshare-ipc \
    --bind "$R" / --dev /dev --proc /proc --tmpfs /tmp \
    --ro-bind /etc/resolv.conf /etc/resolv.conf \
    --bind "$ROOT" /src --chdir /src \
    --setenv HOME /root --setenv PATH /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    --setenv DEBIAN_FRONTEND noninteractive \
    "$@"
