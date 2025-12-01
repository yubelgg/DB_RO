#!/bin/bash
#
# run_occ_benchmark.sh - Run OCC benchmarks and capture metrics
#
# This script runs the Enhanced OCC tests and outputs results in CSV format
# for easy analysis and comparison.
#
# Usage:
#   ./scripts/run_occ_benchmark.sh [output_file]
#
# Example:
#   ./scripts/run_occ_benchmark.sh results.csv
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

# Output file
OUTPUT_FILE=${1:-"$PROJECT_ROOT/occ_benchmark_results.csv"}
TIMESTAMP=$(date +"%Y-%m-%d_%H:%M:%S")

# Temporary files for capturing output
TMP_DIR=$(mktemp -d)
trap "rm -rf $TMP_DIR" EXIT

echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}  Enhanced OCC Benchmark Suite${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""
echo -e "Timestamp: ${YELLOW}$TIMESTAMP${NC}"
echo -e "Output:    ${YELLOW}$OUTPUT_FILE${NC}"
echo ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Error: Build directory not found. Run 'cmake' and 'make' first.${NC}"
    exit 1
fi

# Function to extract current parameter values from source
get_current_params() {
    local scheduler_h="$PROJECT_ROOT/src/deptran/occ/scheduler_enhanced.h"
    local scheduler_cc="$PROJECT_ROOT/src/deptran/occ/scheduler_enhanced.cc"
    local tx_h="$PROJECT_ROOT/src/deptran/occ/tx_enhanced.h"
    
    BATCH_SIZE=$(grep -oP 'batch_size_ = \K[0-9]+' "$scheduler_h" || echo "32")
    BATCH_TIMEOUT=$(grep -oP 'batch_timeout_\{\K[0-9]+' "$scheduler_h" || echo "100")
    NUM_WORKERS=$(grep -oP '[0-9]+(?= // num_workers for parallel validation)' "$scheduler_cc" || echo "8")
    CHECK_INTERVAL=$(grep -oP 'check_interval_ = \K[0-9]+' "$tx_h" || echo "10")
}

# Function to run a single test and capture timing
run_test() {
    local test_name=$1
    local output_file="$TMP_DIR/${test_name}.out"
    
    cd "$BUILD_DIR"
    
    # Run test with timing
    local start_time=$(date +%s%N)
    if ctest -R "$test_name" --output-on-failure > "$output_file" 2>&1; then
        local status="PASS"
    else
        local status="FAIL"
    fi
    local end_time=$(date +%s%N)
    
    # Calculate duration in milliseconds
    local duration_ns=$((end_time - start_time))
    local duration_ms=$((duration_ns / 1000000))
    
    # Check if test actually ran (not just "no tests matched")
    if grep -q "No tests were found" "$output_file"; then
        status="SKIP"
        duration_ms=0
    fi
    
    echo "$test_name,$status,$duration_ms"
}

# Function to run all OCC tests
run_all_tests() {
    local tests=(
        "ValidationQueueTest"
        "EarlyAbortDetectorTest"
        "ConflictGraphTest"
        "BloomFilterTest"
        "ConcurrentMapTest"
        "BatchValidatorTest"
        "SchedulerEnhancedTest"
    )
    
    echo ""
    echo -e "${BLUE}Running tests...${NC}"
    echo "----------------------------------------"
    
    local total_pass=0
    local total_fail=0
    local total_skip=0
    local total_time=0
    
    for test in "${tests[@]}"; do
        local result=$(run_test "$test")
        local name=$(echo "$result" | cut -d',' -f1)
        local status=$(echo "$result" | cut -d',' -f2)
        local time=$(echo "$result" | cut -d',' -f3)
        
        total_time=$((total_time + time))
        
        if [ "$status" == "PASS" ]; then
            echo -e "  ${GREEN}✓${NC} $name (${time}ms)"
            ((total_pass++))
        elif [ "$status" == "FAIL" ]; then
            echo -e "  ${RED}✗${NC} $name (${time}ms)"
            ((total_fail++))
        else
            echo -e "  ${YELLOW}○${NC} $name (skipped)"
            ((total_skip++))
        fi
    done
    
    echo "----------------------------------------"
    echo ""
    echo -e "Summary: ${GREEN}$total_pass passed${NC}, ${RED}$total_fail failed${NC}, ${YELLOW}$total_skip skipped${NC}"
    echo -e "Total time: ${total_time}ms"
    echo ""
    
    # Return metrics
    echo "$total_pass,$total_fail,$total_skip,$total_time"
}

# Function to write CSV header
write_csv_header() {
    if [ ! -f "$OUTPUT_FILE" ]; then
        echo "timestamp,batch_size,batch_timeout_us,num_workers,check_interval,tests_passed,tests_failed,tests_skipped,total_time_ms" > "$OUTPUT_FILE"
    fi
}

# Function to append result to CSV
append_csv_result() {
    local metrics=$1
    local passed=$(echo "$metrics" | cut -d',' -f1)
    local failed=$(echo "$metrics" | cut -d',' -f2)
    local skipped=$(echo "$metrics" | cut -d',' -f3)
    local time=$(echo "$metrics" | cut -d',' -f4)
    
    echo "$TIMESTAMP,$BATCH_SIZE,$BATCH_TIMEOUT,$NUM_WORKERS,$CHECK_INTERVAL,$passed,$failed,$skipped,$time" >> "$OUTPUT_FILE"
}

# Main execution
main() {
    # Get current parameter values
    get_current_params
    
    echo -e "Current Parameters:"
    echo -e "  batch_size:     ${YELLOW}$BATCH_SIZE${NC}"
    echo -e "  batch_timeout:  ${YELLOW}$BATCH_TIMEOUT${NC} μs"
    echo -e "  num_workers:    ${YELLOW}$NUM_WORKERS${NC}"
    echo -e "  check_interval: ${YELLOW}$CHECK_INTERVAL${NC}"
    
    # Ensure build is up to date
    echo ""
    echo -e "${BLUE}Checking build...${NC}"
    cd "$BUILD_DIR"
    make -j$(nproc) 2>&1 | tail -3
    
    # Run tests and capture metrics
    metrics=$(run_all_tests | tail -1)
    
    # Write results to CSV
    write_csv_header
    append_csv_result "$metrics"
    
    echo -e "${GREEN}Results appended to: $OUTPUT_FILE${NC}"
    echo ""
    
    # Show recent results
    if [ -f "$OUTPUT_FILE" ]; then
        echo -e "${BLUE}Recent results:${NC}"
        echo "----------------------------------------"
        tail -5 "$OUTPUT_FILE" | column -t -s','
        echo "----------------------------------------"
    fi
}

# Print usage if --help
if [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
    echo "Usage: $0 [output_file]"
    echo ""
    echo "Runs all Enhanced OCC tests and records results to CSV."
    echo ""
    echo "Arguments:"
    echo "  output_file - CSV file for results (default: occ_benchmark_results.csv)"
    echo ""
    echo "The script captures:"
    echo "  - Current parameter values from source files"
    echo "  - Test pass/fail/skip counts"
    echo "  - Total execution time"
    echo ""
    echo "Example:"
    echo "  $0                        # Use default output file"
    echo "  $0 my_results.csv         # Custom output file"
    echo ""
    echo "Combine with tune_occ.sh for parameter sweeps:"
    echo "  ./scripts/tune_occ.sh 64 200 4 20"
    echo "  ./scripts/run_occ_benchmark.sh results.csv"
    exit 0
fi

main

