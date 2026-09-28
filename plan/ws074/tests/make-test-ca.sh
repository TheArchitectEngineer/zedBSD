#!/bin/sh
# ws074-p017: makes the HTTPS test server's own CA and two server certificates with the host's openssl.
# Nothing here is committed; the files are made again for each run.  They start a day before now (a guest whose
# clock is a little behind the host's would otherwise find them not yet valid) and last 30 days.
#
#   sh plan/ws074/tests/make-test-ca.sh [DIR]      (default build/ws074-tls)
#
#   DIR/ca.pem                the CA (browser --ca-file=DIR/ca.pem trusts it)
#   DIR/good.pem, good.key    for localhost, 127.0.0.1 and 10.0.2.2 (the host as the guest sees it)
#   DIR/wrong.pem, wrong.key  for wrong.example only (a name that does not match)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
dir=${1:-build/ws074-tls}
mkdir -p "$dir"
start=$(date -u -d '1 day ago' +%Y%m%d%H%M%SZ)
end=$(date -u -d '30 days' +%Y%m%d%H%M%SZ)
openssl req -x509 -newkey rsa:2048 -nodes -not_before "$start" -not_after "$end" -keyout "$dir/ca.key" -out "$dir/ca.pem" \
    -subj "/CN=browser test CA" -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
for name in good wrong; do
	case $name in
	good) names="subjectAltName=DNS:localhost,IP:127.0.0.1,IP:10.0.2.2" ;;
	wrong) names="subjectAltName=DNS:wrong.example" ;;
	esac
	printf '%s\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n' "$names" > "$dir/$name.ext"
	openssl req -newkey rsa:2048 -nodes -keyout "$dir/$name.key" -out "$dir/$name.csr" \
	    -subj "/CN=browser test $name" 2>/dev/null
	openssl x509 -req -in "$dir/$name.csr" -CA "$dir/ca.pem" -CAkey "$dir/ca.key" -CAcreateserial -not_before "$start" -not_after "$end" \
	    -extfile "$dir/$name.ext" -out "$dir/$name.pem" 2>/dev/null
done
echo "make-test-ca: $dir"
