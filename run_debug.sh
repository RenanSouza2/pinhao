#!/usr/bin/env bash
set -e

# --keep appends to the existing log instead of wiping it; --force appends
# even when the config changed. Consumed here: main() takes no argv.
keep=""
force=""
args=()
for arg in "$@"; do
    case "$arg" in
        --keep) keep=1 ;;
        --force) force=1 ;;
        *) args+=("$arg") ;;
    esac
done

# "Same config" for --keep means an unchanged src/main.c: pi()'s parameters
# are literals in main().
config_id="$(cksum < src/main.c | awk '{print $1"-"$2}')"

mkdir -p thread_log
if [ -n "$keep" ] && [ -s thread_log/run.log ]; then
    prev="$(sed -n 's/^=== run .* | main\.c \(.*\) ===$/\1/p' thread_log/run.log | tail -1)"
    if [ -z "$prev" ]; then
        echo "run_debug.sh: existing log carries no run marker; appending without a config check" >&2
    elif [ "$prev" != "$config_id" ] && [ -z "$force" ]; then
        echo "run_debug.sh: src/main.c changed since the log's last run ($prev -> $config_id)." >&2
        echo "              --keep stacks runs of one config; drop it for a fresh log, or --force to append anyway." >&2
        exit 1
    fi
else
    rm -rf thread_log/*
fi

# The run boundary dashboard.py resets its parser on. Leading newline: a
# record ends in a tab, so the marker would otherwise glue onto the last one.
printf '\n=== run %s | main.c %s ===\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$config_id" >> thread_log/run.log

make dbg
time ./src/debug.out "${args[@]}" PI 2> >(tee -a thread_log/run.log >&2)
