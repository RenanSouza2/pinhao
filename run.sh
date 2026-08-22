#!/usr/bin/env bash
set -e
mkdir -p thread_log
rm -rf thread_log/*
make build
time ./src/main.out $@ PI 2> >(tee thread_log/run.log >&2)
