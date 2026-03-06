#!/usr/bin/env bash
# bench.sh - runs solvers on (graph, grammar) pairs, collects time and peak memory into CSV.
# After all runs, validates that #SEdges match across solvers.
#
# Usage:
#   ./bench.sh --config bench_config.sh
#
# Config file defines:
#   DATASETS   - array of "name|graph|grammar" strings
#   SOLVERS    - array of "name|command" strings
#   RUNS       - number of repetitions (default 3)
#   TIMEOUT    - timeout in seconds (default 600)
#   OUTPUT_CSV - output file (default results.csv)

set -euo pipefail

RUNS=3
TIMEOUT=600
OUTPUT_CSV="results.csv"
CONFIG_FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config) CONFIG_FILE="$2"; shift 2 ;;
        --runs)   RUNS="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --output) OUTPUT_CSV="$2"; shift 2 ;;
        --help|-h)
            echo "Usage: $0 --config <file> [--runs N] [--timeout sec] [--output file.csv]"
            exit 0 ;;
        *) echo "Unknown argument: $1"; exit 1 ;;
    esac
done

if [[ -z "$CONFIG_FILE" ]]; then
    echo "Error: specify --config <file>"
    exit 1
fi

source "$CONFIG_FILE"

if [[ -z "${DATASETS+x}" ]]; then
    echo "Error: DATASETS not defined in config"
    exit 1
fi
if [[ -z "${SOLVERS+x}" ]]; then
    echo "Error: SOLVERS not defined in config"
    exit 1
fi

HAS_GNU_TIME=false
if /usr/bin/time --version 2>&1 | grep -q "GNU"; then
    HAS_GNU_TIME=true
fi

echo "solver,graph,grammar,run,time_sec,sedges,peak_memory_kb,status" > "$OUTPUT_CSV"
echo "Output: $OUTPUT_CSV"
echo "Runs: $RUNS, timeout: ${TIMEOUT}s, memory measurement: $HAS_GNU_TIME"
echo ""

TIME_STDERR=$(mktemp)
trap "rm -f $TIME_STDERR" EXIT

total_solvers=${#SOLVERS[@]}
total_datasets=${#DATASETS[@]}
total_runs=$((total_solvers * total_datasets * RUNS))
current=0

for solver_spec in "${SOLVERS[@]}"; do
    solver_name="${solver_spec%%|*}"
    solver_cmd="${solver_spec#*|}"

    echo "==========================================="
    echo " Solver: $solver_name"
    echo "==========================================="

    for dataset_spec in "${DATASETS[@]}"; do
        IFS='|' read -r ds_name ds_graph ds_grammar <<< "$dataset_spec"

        echo ""
        echo "  Dataset: $ds_name"
        echo "    graph:   $ds_graph"
        echo "    grammar: $ds_grammar"

        for run in $(seq 1 "$RUNS"); do
            current=$((current + 1))
            echo -n "    run $run/$RUNS ($current/$total_runs) ... "

            cmd="${solver_cmd//\{graph\}/$ds_graph}"
            cmd="${cmd//\{grammar\}/$ds_grammar}"

            start_time=$(date +%s%N)
            output=""
            status="ok"
            peak_mem=""

            if $HAS_GNU_TIME; then
                if output=$(timeout "${TIMEOUT}s" /usr/bin/time -v bash -c "$cmd" 2>"$TIME_STDERR"); then
                    end_time=$(date +%s%N)
                    elapsed=$(echo "scale=6; ($end_time - $start_time) / 1000000000" | bc)
                else
                    exit_code=$?
                    end_time=$(date +%s%N)
                    elapsed=$(echo "scale=6; ($end_time - $start_time) / 1000000000" | bc)
                    if [[ $exit_code -eq 124 ]]; then
                        status="timeout"
                    else
                        status="error($exit_code)"
                    fi
                fi
                peak_mem=$(grep -oP 'Maximum resident set size \(kbytes\): \K[0-9]+' "$TIME_STDERR" 2>/dev/null || echo "")
            else
                if output=$(timeout "${TIMEOUT}s" bash -c "$cmd" 2>&1); then
                    end_time=$(date +%s%N)
                    elapsed=$(echo "scale=6; ($end_time - $start_time) / 1000000000" | bc)
                else
                    exit_code=$?
                    end_time=$(date +%s%N)
                    elapsed=$(echo "scale=6; ($end_time - $start_time) / 1000000000" | bc)
                    if [[ $exit_code -eq 124 ]]; then
                        status="timeout"
                    else
                        status="error($exit_code)"
                    fi
                fi
            fi

            sedges=$(echo "$output" | grep -oP '#SEdges[:\t]\s*\K[0-9]+' | tail -1 || echo "")
            reported_time=$(echo "$output" | grep -oP 'AnalysisTime[:\t]\s*\K[0-9.]+' | tail -1 || echo "")

            if [[ -n "$reported_time" ]]; then
                time_val="$reported_time"
            else
                time_val="$elapsed"
            fi

            mem_str=""
            if [[ -n "$peak_mem" ]]; then
                mem_mb=$(echo "scale=1; $peak_mem / 1024" | bc)
                mem_str=" mem=${mem_mb}MB"
            fi

            echo "${time_val}s ($status)${sedges:+ #SEdges=$sedges}${mem_str}"

            echo "$solver_name,$ds_name,$ds_grammar,$run,$time_val,$sedges,$peak_mem,$status" >> "$OUTPUT_CSV"
        done
    done
done

echo ""
echo "===================================="
echo " All runs completed"
echo "===================================="

echo ""
echo "==========================================="
echo " #SEdges Validation"
echo "==========================================="

has_mismatch=false
graphs=$(tail -n +2 "$OUTPUT_CSV" | cut -d',' -f2 | sort -u)

for graph in $graphs; do
    pairs=$(grep ",$graph," "$OUTPUT_CSV" | grep ",ok$" \
        | awk -F',' '{if ($6 != "") print $1 ":" $6}' | sort -u)

    if [[ -z "$pairs" ]]; then
        echo "  ? $graph: no #SEdges data"
        continue
    fi

    sedges_values=$(echo "$pairs" | cut -d: -f2 | sort -u)
    n_values=$(echo "$sedges_values" | wc -l)

    if [[ "$n_values" -gt 1 ]]; then
        has_mismatch=true
        echo "  MISMATCH on graph '$graph':"
        echo "$pairs" | while IFS=':' read -r solver sedges_val; do
            echo "      $solver -> $sedges_val"
        done
    else
        echo "  OK $graph: #SEdges=$sedges_values ($(echo "$pairs" | wc -l) solvers)"
    fi
done

echo ""
if $has_mismatch; then
    echo "WARNING: #SEdges mismatches detected!"
    echo "  Check solver correctness."
else
    echo "All #SEdges match across solvers."
fi

echo ""
echo "===================================="
echo " Done! Results in $OUTPUT_CSV"
echo "===================================="