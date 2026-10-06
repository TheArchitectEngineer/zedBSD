#!/bin/sh
# ws169-p003: builds and runs the host test of Mail's backend (host-mail-backend.c with
# userland/desktop/mailer/{tls,conn,imap,smtp,mime,compose,code}.c, the host's OpenSSL loaded as Mail loads the
# package's) under ASan and UBSan, against fake-mail-server.py with a CA and a certificate for localhost made here.
#   sh plan/ws169/tests/run-host-mail-backend.sh [OUTDIR]   (default build/ws169/host-mail-backend; a fresh run
#   directory under it each time, which Q1's cleaning removes)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
base=${1:-build/ws169/host-mail-backend}
mkdir -p "$base"
out=$(mktemp -d "$base/run.XXXXXX")
M=userland/desktop/mailer
${CC:-cc} -std=gnu99 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -pthread \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -I. \
	plan/ws169/tests/host-mail-backend.c $M/tls.c $M/conn.c $M/imap.c $M/smtp.c $M/mime.c $M/compose.c $M/code.c \
	-ldl -o "$out/host-mail-backend"
# The tests' CA and the server's certificate for localhost.
openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj /CN=ws169-test-ca -keyout "$out/ca.key" -out "$out/ca.pem" \
	-addext basicConstraints=critical,CA:TRUE -addext keyUsage=critical,keyCertSign >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -subj /CN=localhost -keyout "$out/server.key" -out "$out/server.csr" >/dev/null 2>&1
printf 'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\n' > "$out/server.ext"
openssl x509 -req -in "$out/server.csr" -CA "$out/ca.pem" -CAkey "$out/ca.key" -CAcreateserial -days 2 \
	-extfile "$out/server.ext" -out "$out/server.pem" >/dev/null 2>&1
python3 plan/ws169/tests/fake-mail-server.py "$out/server.pem" "$out/server.key" "$out" > "$out/ports" &
server=$!
trap 'kill $server 2>/dev/null' EXIT
tries=0
while ! grep -q PORTS "$out/ports" 2>/dev/null; do
	tries=$((tries + 1))
	[ $tries -lt 50 ] || { echo "fake-mail-server did not start"; exit 1; }
	sleep 0.1
done
set -- $(cat "$out/ports")
timeout 60 "$out/host-mail-backend" "$out/ca.pem" "$2" "$3" "$4" "$5" "$out"
