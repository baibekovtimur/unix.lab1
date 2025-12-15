#!/bin/sh

SHARED_DIR="${SHARED_DIR:-/shared}"
LOCKFILE="$SHARED_DIR/.lockfile"

mkdir -p "$SHARED_DIR" 2>/dev/null

exec 9>>"$LOCKFILE" 2>/dev/null

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
            {
                printf 'CONTAINER_ID=%s\n' "$CONTAINER_ID"
                printf 'FILE_SEQ=%s\n' "$FILE_SEQ"
                printf 'TIMESTAMP%s\n' "$(date +%s)"
            } > "$FILE_PATH"

            if [$? -ne 0 ]; then
                rm -f "$FILE_PATH" 2>/dev/null
            fi
            break
        fi
    done
    
    flock -u 9

    FILE_SEQ=$(( FILE_SEQ + 1 ))

    sleep 1
    rm -f "$FILE_PATH"
    sleep 1
done
