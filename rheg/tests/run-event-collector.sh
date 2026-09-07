#!/usr/bin/env bash
# Runs collector contracts and checks lossless timing and identities in trace JSON.
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/../.." && pwd)"
collector_tmp_dir="$(mktemp -d /tmp/rhodium-event-collector.XXXXXX)"
trap 'rm -rf "$collector_tmp_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  "$repo_dir/rheg/tests/event-collector-test.cpp" \
  "$repo_dir/rheg/runtime/rheg.cc" \
  -o "$collector_tmp_dir/event-collector-test"
"$collector_tmp_dir/event-collector-test" > "$collector_tmp_dir/trace.json"
"$repo_dir/tools/run-racket.sh" -e '
  (require json)
  (define trace (call-with-input-file (vector-ref (current-command-line-arguments) 0) read-json))
  (define manifest (hash-ref trace (quote manifest)))
  (define occurrences (hash-ref trace (quote occurrences)))
  (define timing (hash-ref trace (quote timing)))
  (unless (and (equal? (hash-ref trace (quote format)) "rhodium-event-trace")
               (= (hash-ref trace (quote version)) 1)
               (equal? (hash-ref timing (quote clock_frequency_hz)) "9007199254740993")
               (equal? (hash-ref timing (quote epoch_id)) "9007199254740993")
               (equal? (hash-ref timing (quote origin)) "cycle-zero")
               (equal? (hash-ref manifest (quote format)) "rhodium-event-graph")
               (equal? (hash-ref occurrences (quote format)) "rhodium-event-occurrences")
               (= (length (hash-ref manifest (quote sites))) 2)
               (= (length (hash-ref occurrences (quote nodes))) 3)
               (= (length (hash-ref occurrences (quote edges))) 2)
               (equal? (hash-ref (car (hash-ref occurrences (quote nodes))) (quote sequence)) "9007199254740993"))
    (error (quote event-trace) "invalid bundled export"))
' "$collector_tmp_dir/trace.json"
