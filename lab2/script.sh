#!/bin/sh

SHARED_DIR="${SHARED_DIR:-/shared}"
LOCKFILE="$SHARED_DIR/.lockfile"

mkdir -p "$SHARED_DIR"

command -v flock >/dev/null 2>&1 || {
    echo "Error: 'flock' not found. Install util-linux." >&2
    exit 1
}

exec 9>>"$LOCKFILE"

CONTAINER_ID="$(LC_ALL=C tr -dc 'A-Za-z0-9' </dev/urandom 2>/dev/null | head -c 8)"
[ -z "$CONTAINER_ID" ] && CONTAINER_ID="fallbackID"

echo "Container started, ID = $CONTAINER_ID"
echo "Shared dir: $SHARED_DIR"

FILE_SEQ=0

while true; do
    flock 9

    i=1
    while true; do
        FILE_NAME=$(printf '%03d' "$i")
        FILE_PATH="$SHARED_DIR/$FILE_NAME"

        if [ ! -e "$FILE_PATH" ]; then
            : > "$FILE_PATH"
            break
        fi

        i=$(( i + 1 ))
    done

    flock -u 9

    FILE_SEQ=$(( FILE_SEQ + 1 ))

    {
        printf 'CONTAINER_ID=%s\n' "$CONTAINER_ID"
        printf 'FILE_SEQ=%s\n' "$FILE_SEQ"
    } > "$FILE_PATH"

    sleep 1
    rm -f "$FILE_PATH"
    sleep 1
done
