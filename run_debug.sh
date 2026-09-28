#!/usr/bin/env bash
set -e

# A log is never deleted. run.log is appended to when its last run of this
# config never reached "display end" - this run resumes it - and archived to
# run.<first run's start>.log otherwise. --keep appends regardless, --fresh
# archives regardless, --force lets --keep append across a config change.
# Consumed here: main() takes no argv.
keep=""
fresh=""
force=""
args=()
for arg in "$@"; do
    case "$arg" in
        --keep) keep=1 ;;
        --fresh) fresh=1 ;;
        --force) force=1 ;;
        *) args+=("$arg") ;;
    esac
done

# "Same config" for --keep means an unchanged src/main.c: pi()'s parameters
# are literals in main().
config_id="$(cksum < src/main.c | awk '{print $1"-"$2}')"

log=thread_log/run.log
mkdir -p thread_log
append=""
if [ -s "$log" ]; then
    prev="$(sed -n 's/^=== run .* | main\.c \(.*\) ===$/\1/p' "$log" | tail -1)"
    finished="$(awk '/^=== run /{f=0} /display end/{f=1} END{print f+0}' "$log")"
    if [ -n "$fresh" ]; then
        append=""
    elif [ -n "$keep" ]; then
        append=1
        if [ -z "$prev" ]; then
            echo "run_debug.sh: existing log carries no run marker; appending without a config check" >&2
        elif [ "$prev" != "$config_id" ] && [ -z "$force" ]; then
            echo "run_debug.sh: src/main.c changed since the log's last run ($prev -> $config_id)." >&2
            echo "              --keep stacks runs of one config; drop it to archive the log, or --force to append anyway." >&2
            exit 1
        fi
    elif [ "$finished" = 0 ] && [ "$prev" = "$config_id" ]; then
        append=1
    fi
fi

make dbg

if [ -s "$log" ] && [ -z "$append" ]; then
    started="$(sed -n 's/^=== run \(.*\) | main\.c .* ===$/\1/p' "$log" | head -1 | tr ' :' '_-')"
    archive="thread_log/run.${started:-$(date '+%Y-%m-%d_%H-%M-%S')}.log"
    [ -e "$archive" ] && archive="${archive%.log}.$$.log"
    mv "$log" "$archive"
    echo "run_debug.sh: archived the previous log to $archive" >&2
fi

# The run boundary dashboard.py resets its parser on. Leading newline: a
# record ends in a tab, so the marker would otherwise glue onto the last one.
printf '\n=== run %s | main.c %s ===\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$config_id" >> "$log"

time ./src/debug.out "${args[@]}" PI 2> >(tee -a "$log" >&2)
