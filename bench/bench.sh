#!/usr/bin/env bash
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
            echo "Использование: $0 --config <файл> [--runs N] [--timeout сек] [--output файл.csv]"
            exit 0 ;;
        *) echo "Неизвестный аргумент: $1"; exit 1 ;;
    esac
done

if [[ -z "$CONFIG_FILE" ]]; then
    echo "Ошибка: укажите --config <файл>"
    exit 1
fi

source "$CONFIG_FILE"

if [[ -z "${DATASETS+x}" ]]; then
    echo "Ошибка: DATASETS не задан в конфиге"
    exit 1
fi
if [[ -z "${SOLVERS+x}" ]]; then
    echo "Ошибка: SOLVERS не задан в конфиге"
    exit 1
fi

echo "solver,graph,grammar,run,time_sec,sedges,status" > "$OUTPUT_CSV"
echo "Результаты → $OUTPUT_CSV"
echo "Повторов: $RUNS, таймаут: ${TIMEOUT}с"
echo ""

total_solvers=${#SOLVERS[@]}
total_datasets=${#DATASETS[@]}
total_runs=$((total_solvers * total_datasets * RUNS))
current=0

for solver_spec in "${SOLVERS[@]}"; do
    solver_name="${solver_spec%%|*}"
    solver_cmd="${solver_spec#*|}"

    echo "═══════════════════════════════════════════"
    echo " Решатель: $solver_name"
    echo "═══════════════════════════════════════════"

    for dataset_spec in "${DATASETS[@]}"; do
        IFS='|' read -r ds_name ds_graph ds_grammar <<< "$dataset_spec"

        echo ""
        echo "  Датасет: $ds_name"
        echo "    граф:      $ds_graph"
        echo "    грамматика: $ds_grammar"

        for run in $(seq 1 "$RUNS"); do
            current=$((current + 1))
            echo -n "    прогон $run/$RUNS ($current/$total_runs) ... "

            cmd="${solver_cmd//\{graph\}/$ds_graph}"
            cmd="${cmd//\{grammar\}/$ds_grammar}"

            start_time=$(date +%s%N)
            output=""
            status="ok"

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

            sedges=$(echo "$output" | grep -oP '#SEdges[:\t]\s*\K[0-9]+' | tail -1 || echo "")
            reported_time=$(echo "$output" | grep -oP 'AnalysisTime[:\t]\s*\K[0-9.]+' | tail -1 || echo "")

            if [[ -n "$reported_time" ]]; then
                time_val="$reported_time"
            else
                time_val="$elapsed"
            fi

            echo "${time_val}с ($status) ${sedges:+#SEdges=$sedges}"

            echo "$solver_name,$ds_name,$ds_grammar,$run,$time_val,$sedges,$status" >> "$OUTPUT_CSV"
        done
    done
done

echo ""
echo "════════════════════════════════════"
echo " Готово! Результаты в $OUTPUT_CSV"
echo "════════════════════════════════════"