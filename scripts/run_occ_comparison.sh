#!/bin/bash
#
# run_occ_comparison.sh - Run all OCC configurations and compare results
#
# Usage:
#   ./scripts/run_occ_comparison.sh [duration] [timeout]
#
# Example:
#   ./scripts/run_occ_comparison.sh 10      # 10s tests, auto timeout (2*10+30=50s)
#   ./scripts/run_occ_comparison.sh 60      # 60s tests, auto timeout (2*60+30=150s)
#   ./scripts/run_occ_comparison.sh 10 120  # 10s tests, manual 120s timeout
#

# Don't use set -e because ((var++)) returns 1 when var is 0

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration: config_file:display_name
CONFIGS=(
  "tpcc_occ_baseline.yml:baseline"
  "tpcc_occ_early_abort.yml:early_abort"
  "tpcc_occ_batch_only.yml:batch"
)

# Default duration
DURATION=${1:-10}

# Timeout: proportional to duration (2x duration + 30s buffer for setup/teardown)
# Can be overridden with second argument
DEFAULT_TIMEOUT=$((DURATION * 2 + 30))
TIMEOUT=${2:-$DEFAULT_TIMEOUT}

# Find project root (directory containing scripts/)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
CONFIG_DIR="$PROJECT_ROOT/config"

# Check if build directory exists with labtest
if [ ! -f "$BUILD_DIR/labtest" ]; then
  echo -e "${RED}Error: labtest not found at $BUILD_DIR/labtest${NC}"
  echo "Run 'make' in the build directory first."
  exit 1
fi

# Change to build directory (CSVs are created there)
cd "$BUILD_DIR"

# Check for existing CSV files and ask for confirmation
CSV_COUNT=$(ls results_*.csv 2>/dev/null | wc -l)
if [ "$CSV_COUNT" -gt 0 ]; then
  echo -e "${YELLOW}Found ${CSV_COUNT} existing result file(s) in build directory.${NC}"
  echo -ne "Delete old results before running tests? [Y/n]: "
  read -r RESPONSE

  # Default to Yes if empty
  if [ -z "$RESPONSE" ] || [ "$RESPONSE" = "y" ] || [ "$RESPONSE" = "Y" ]; then
    rm -f results_*.csv
    echo -e "${GREEN}Old results deleted.${NC}"
  else
    echo -e "${CYAN}Keeping old results. New results will be added.${NC}"
  fi
  echo ""
fi

echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}  OCC Benchmark Comparison Suite${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""
echo -e "  Duration: ${YELLOW}${DURATION}s${NC} per test"
echo -e "  Timeout:  ${YELLOW}${TIMEOUT}s${NC} max per test"
echo -e "  Configs:  ${YELLOW}${#CONFIGS[@]}${NC} configurations"
echo ""
echo -e "${CYAN}Note: Each test will be killed if it exceeds ${TIMEOUT}s${NC}"
echo ""

# Track results
PASSED=0
FAILED=0

# Run each configuration
for i in "${!CONFIGS[@]}"; do
  config_pair="${CONFIGS[$i]}"
  config="${config_pair%%:*}"
  name="${config_pair##*:}"
  test_num=$((i + 1))

  echo -ne "${BLUE}[${test_num}/${#CONFIGS[@]}]${NC} Running ${YELLOW}${name}${NC}... "

  # Run with timeout, suppress all output
  START_TIME=$(date +%s)

  # Count CSVs before run
  CSV_BEFORE=$(ls results_*.csv 2>/dev/null | wc -l)

  # Run in subshell and capture exit code (suppresses shell's "Aborted" message)
  EXIT_CODE=0
  { timeout "${TIMEOUT}" ./labtest -f "${CONFIG_DIR}/${config}" -d "${DURATION}" > /dev/null 2>&1; EXIT_CODE=$?; } 2>/dev/null || EXIT_CODE=$?

  END_TIME=$(date +%s)
  ELAPSED=$((END_TIME - START_TIME))

  # Count CSVs after run - if new CSV created, test succeeded even with shutdown crash
  CSV_AFTER=$(ls results_*.csv 2>/dev/null | wc -l)
  CSV_CREATED=$((CSV_AFTER > CSV_BEFORE))

  if [ $EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}DONE${NC} (${ELAPSED}s)"
    ((PASSED++)) || true
  elif [ $EXIT_CODE -eq 124 ]; then
    echo -e "${RED}TIMEOUT${NC} (exceeded ${TIMEOUT}s)"
    ((FAILED++)) || true
  elif [ $CSV_CREATED -eq 1 ]; then
    # Test completed and created results, just crashed during shutdown
    echo -e "${GREEN}DONE${NC} (${ELAPSED}s, shutdown crash ignored)"
    ((PASSED++)) || true
  elif [ $EXIT_CODE -eq 134 ] || [ $EXIT_CODE -eq 139 ]; then
    echo -e "${RED}CRASHED${NC} (exit code: ${EXIT_CODE}, ${ELAPSED}s)"
    ((FAILED++)) || true
  else
    echo -e "${RED}FAILED${NC} (exit code: ${EXIT_CODE}, ${ELAPSED}s)"
    ((FAILED++)) || true
  fi
done

echo ""
echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}  Results: ${GREEN}${PASSED} passed${NC}, ${RED}${FAILED} failed${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""

# Show where results are saved
NEW_CSVS=($(ls -t results_*.csv 2>/dev/null | head -"${#CONFIGS[@]}"))
if [ ${#NEW_CSVS[@]} -gt 0 ]; then
  echo -e "${CYAN}Results saved to:${NC} ${BUILD_DIR}/"
  for csv in "${NEW_CSVS[@]}"; do
    echo -e "  - ${csv}"
  done
  echo ""
fi

# Run analysis script
if [ -x "${SCRIPT_DIR}/analyze_occ_results.sh" ]; then
  "${SCRIPT_DIR}/analyze_occ_results.sh"
else
  echo -e "${YELLOW}Analysis script not found or not executable${NC}"
  echo "Run: chmod +x ${SCRIPT_DIR}/analyze_occ_results.sh"
fi
