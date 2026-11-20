#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# 上传已签名的 OTA 产物，先安装镜像，再原子切换清单。
# Deploy signed OTA artifacts: install verified images, then atomically switch the feed.
set -euo pipefail
if (( $# < 2 )); then
    echo "Usage: $0 BUNDLE_DIRECTORY [SSH_OPTIONS] USER@HOST" >&2
    exit 2
fi
bundle=$1
shift

remote_images=$(cat <<'REMOTE'
set -eu
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
tar --no-same-owner -xf - -C "$stage"
cd "$stage"
sha256sum --strict --check SHA256SUMS
sudo -n bash -se -- "$stage" <<'INSTALL'
set -euo pipefail
stage=$1
root=/var/www/ifink.revcloud.top/ota
test -f /var/www/ifink.revcloud.top/index.html
install -d -m 0755 "$root" "$root/images"
exec 9>/run/lock/ifink-ota.lock
flock 9
shopt -s nullglob
temp=''
trap 'if [[ -n "$temp" ]]; then rm -f -- "$temp"; fi' EXIT
for file in "$stage"/images/*.bin; do
    name=${file##*/}
    [[ "$name" =~ ^[0-9][0-9A-Za-z.-]*\.bin$ ]]
    if [[ -e "$root/images/$name" ]]; then
        cmp "$file" "$root/images/$name"
    else
        temp=$(mktemp "$root/images/.image.XXXXXXXX")
        install -m 0644 "$file" "$temp"
        mv -T "$temp" "$root/images/$name"
        temp=''
    fi
done
if [[ -f "$root/feeds.v1" ]]; then
    sha256sum "$root/feeds.v1" | awk '{print "Active feed:", $1}'
else
    echo 'Active feed: absent'
fi
INSTALL
REMOTE
)

receipt=$(COPYFILE_DISABLE=1 tar -C "$bundle" -cf - feeds.v1 images SHA256SUMS | ssh "$@" "$remote_images")
printf '%s\n' "$receipt"
active=$(printf '%s\n' "$receipt" | awk '/^Active feed: / {print $3}')
[[ "$active" == absent || "$active" =~ ^[0-9a-f]{64}$ ]]

verify_public() {
    local expected=$1 path=$2 actual
    actual=$(curl -fsS --connect-timeout 8 --max-time 90 \
        -H 'Cache-Control: no-cache' "https://ifink.revcloud.top/ota/$path" | shasum -a 256)
    if [[ "${actual%% *}" != "$expected" ]]; then
        echo "Public SHA-256 mismatch: $path" >&2
        exit 1
    fi
    printf 'Public HTTPS verified: %s\n' "$path"
}

feed_sha=''
while read -r expected path; do
    [[ "$expected" =~ ^[0-9a-f]{64}$ ]]
    if [[ "$path" == feeds.v1 ]]; then
        feed_sha=$expected
    else
        [[ "$path" =~ ^images/[0-9][0-9A-Za-z.-]*\.bin$ ]]
        verify_public "$expected" "$path"
    fi
done < "$bundle/SHA256SUMS"
[[ -n "$feed_sha" ]]

remote_feed=$(cat <<'REMOTE'
set -eu
stage=$(mktemp)
trap 'rm -f "$stage"' EXIT
cat > "$stage"
printf '%s  %s\n' "$2" "$stage" | sha256sum --strict --check
sudo -n bash -se -- "$stage" "$1" <<'ACTIVATE'
set -euo pipefail
root=/var/www/ifink.revcloud.top/ota
exec 9>/run/lock/ifink-ota.lock
flock 9
current=absent
if [[ -f "$root/feeds.v1" ]]; then
    current=$(sha256sum "$root/feeds.v1" | awk '{print $1}')
fi
if [[ "$current" != "$2" ]]; then
    echo 'Active mirror changed during upload; check both channels before retrying' >&2
    exit 1
fi
temp=$(mktemp "$root/.feeds.XXXXXXXX")
trap 'rm -f "$temp"' EXIT
install -m 0644 "$1" "$temp"
mv -T "$temp" "$root/feeds.v1"
ACTIVATE
REMOTE
)

ssh "$@" "set -- '$active' '$feed_sha'; $remote_feed" < "$bundle/feeds.v1"
verify_public "$feed_sha" feeds.v1
