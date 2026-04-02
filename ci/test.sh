#!/usr/bin/env bash
set -euo pipefail

CFRA=./build/cfra
PASS=0
FAIL=0

check() {
  local name=$1 graph=$2 grammar=$3 expected=$4 algo=$5
  local sedges
  sedges=$($CFRA --grammar "$grammar" --graph "$graph" --algo "$algo" --cpu \
    | grep '#SEdges' | awk '{print $2}')

  if [ "$sedges" = "$expected" ]; then
    echo "  PASS  $name [$algo] #SEdges=$sedges"
    PASS=$((PASS + 1))
  else
    echo "  FAIL  $name [$algo] #SEdges=$sedges (expected $expected)"
    FAIL=$((FAIL + 1))
  fi
}

check an_bn  test_data/an_bn/graph.txt  test_data/an_bn/grammar.cnf   20 base
check an_bn  test_data/an_bn/graph.txt  test_data/an_bn/grammar.cnf   20 incremental
check an_bn  test_data/an_bn/graph.txt  test_data/an_bn/grammar.cnf   20 lazy

check transitive test_data/transitive_loop/graph.txt test_data/transitive_loop/grammar.cnf 21 base
check transitive test_data/transitive_loop/graph.txt test_data/transitive_loop/grammar.cnf 21 incremental
check transitive test_data/transitive_loop/graph.txt test_data/transitive_loop/grammar.cnf 21 lazy

echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]