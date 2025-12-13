#!/bin/sh

SHARED_DIR="${SHARED_DIR:-/shared}"
LOCKFILE="$SHARED_DIR/.lockfile"
MAX_FILES=1000

mkdir -p "$SHARED_DIR" 2>/dev/null || {
    echo "ERROR: Cannot create $SHARED_DIR" >&2
    exit 1
}

exec 9>>"$LOCKFILE" 2>/dev/null || {
    echo "ERROR: Cannot open lockfile" >&2
    exit 1
}

CONTAINER_ID="$(LC_ALL=C tr -dc 'A-Za-z0-9' </dev/urandom 2>/dev/null | head -c 8)"
[ -z "$CONTAINER_ID" ] && CONTAINER_ID="fallbackID"

echo "Container started, ID = $CONTAINER_ID"
echo "Shared dir: $SHARED_DIR"

FILE_SEQ=0

cleanup() {
    flock -u 9 2>/dev/null || true
    rm -f "$TEMP_FILE" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

while true; do
    flock 9

    i=1

    if flock -w 5 9; then
        for (i=1; i<MAX_FILES; i++); do
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

    else
        echo "WARNING: Could not acquire lock, retrying..." >$2
        sleep 0.5
        continue
    fi

    FILE_SEQ=$(( FILE_SEQ + 1 ))

    sleep 1
    rm -f "$FILE_PATH"
    sleep 1
done
