#!/bin/bash
#
# sweep_occ_params.sh - Automated parameter sweep for Enhanced OCC
#
# This script systematically tests different parameter values and records
# all results to a CSV file for analysis.
#
# Usage:
#   ./scripts/sweep_occ_params.sh [parameter_name]
#
# Examples:
#   ./scripts/sweep_occ_params.sh              # Run full sweep (all parameters)
#   ./scripts/sweep_occ_params.sh batch_size   # Sweep only batch_size
#   ./scripts/sweep_occ_params.sh num_workers  # Sweep only num_workers
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

# Output file
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
OUTPUT_FILE="$PROJECT_ROOT/sweep_results_$TIMESTAMP.csv"

# Source files
SCHEDULER_H="$PROJECT_ROOT/src/deptran/occ/scheduler_enhanced.h"
SCHEDULER_CC="$PROJECT_ROOT/src/deptran/occ/scheduler_enhanced.cc"
TX_ENHANCED_H="$PROJECT_ROOT/src/deptran/occ/tx_enhanced.h"

# Default values (will be restored after sweep)
DEFAULT_BATCH_SIZE=32
DEFAULT_BATCH_TIMEOUT=100
DEFAULT_NUM_WORKERS=8
DEFAULT_CHECK_INTERVAL=10

# Parameter ranges to test
BATCH_SIZE_VALUES=(8 16 32 64 128)
BATCH_TIMEOUT_VALUES=(10 50 100 200 500)
NUM_WORKERS_VALUES=(1 2 4 8 16)
CHECK_INTERVAL_VALUES=(1 5 10 20 50)

# Current values (start with defaults)
CURRENT_BATCH_SIZE=$DEFAULT_BATCH_SIZE
CURRENT_BATCH_TIMEOUT=$DEFAULT_BATCH_TIMEOUT
CURRENT_NUM_WORKERS=$DEFAULT_NUM_WORKERS
CURRENT_CHECK_INTERVAL=$DEFAULT_CHECK_INTERVAL

echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}  Enhanced OCC Parameter Sweep${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""

# Function to modify a single parameter
modify_param() {
    local param=$1
    local value=$2
    
    case $param in
        batch_size)
            sed -i "s/size_t batch_size_ = [0-9]*;/size_t batch_size_ = $value;/" "$SCHEDULER_H"
            CURRENT_BATCH_SIZE=$value
            ;;
        batch_timeout)
            sed -i "s/batch_timeout_{[0-9]*}/batch_timeout_{$value}/" "$SCHEDULER_H"
            CURRENT_BATCH_TIMEOUT=$value
            ;;
        num_workers)
            sed -i "s/[0-9]* \/\/ num_workers for parallel validation/$value \/\/ num_workers for parallel validation/" "$SCHEDULER_CC"
            CURRENT_NUM_WORKERS=$value
            ;;
        check_interval)
            sed -i "s/size_t check_interval_ = [0-9]*;/size_t check_interval_ = $value;/" "$TX_ENHANCED_H"
            CURRENT_CHECK_INTERVAL=$value
            ;;
    esac
}

# Function to restore defaults
restore_defaults() {
    echo -e "\n${YELLOW}Restoring default values...${NC}"
    modify_param "batch_size" $DEFAULT_BATCH_SIZE
    modify_param "batch_timeout" $DEFAULT_BATCH_TIMEOUT
    modify_param "num_workers" $DEFAULT_NUM_WORKERS
    modify_param "check_interval" $DEFAULT_CHECK_INTERVAL
    echo -e "${GREEN}Defaults restored${NC}"
}

# Trap to restore defaults on exit
trap restore_defaults EXIT

# Function to rebuild and run tests
run_benchmark() {
    local label=$1
    
    # Rebuild only the test targets we need (avoid unrelated build errors)
    cd "$BUILD_DIR"
    make txlog test_validation_queue_janus test_early_abort_detector_janus test_conflict_graph_janus test_bloom_filter_janus test_concurrent_map_janus test_batch_validator_janus test_scheduler_enhanced_janus -j$(nproc) 2>&1 | tail -1
    
    # Run tests with timing
    local start_time=$(date +%s%N)
    local test_output=$(ctest -R "BloomFilter|ConcurrentMap|ConflictGraph|BatchValidator|SchedulerEnhanced|ValidationQueue|EarlyAbortDetector" --output-on-failure 2>&1)
    local end_time=$(date +%s%N)
    
    # Calculate duration
    local duration_ns=$((end_time - start_time))
    local duration_ms=$((duration_ns / 1000000))
    
    # Parse results - look for "X tests passed" in the output
    local passed=$(echo "$test_output" | grep -oE '[0-9]+ tests passed' | grep -oE '^[0-9]+' || echo "0")
    local failed=$(echo "$test_output" | grep -oE '[0-9]+ tests? failed' | grep -oE '^[0-9]+' || echo "0")
    
    # If no "tests passed" found, try counting "Passed" lines
    if [ "$passed" == "0" ]; then
        passed=$(echo "$test_output" | grep -c "Passed" || echo "0")
    fi
    
    # Record result
    echo "$label,$CURRENT_BATCH_SIZE,$CURRENT_BATCH_TIMEOUT,$CURRENT_NUM_WORKERS,$CURRENT_CHECK_INTERVAL,$passed,$failed,$duration_ms" >> "$OUTPUT_FILE"
    
    echo -e "  ${GREEN}✓${NC} $label: ${passed} passed, ${duration_ms}ms"
}

# Function to sweep a single parameter
sweep_param() {
    local param=$1
    local values
    
    case $param in
        batch_size)
            values=("${BATCH_SIZE_VALUES[@]}")
            ;;
        batch_timeout)
            values=("${BATCH_TIMEOUT_VALUES[@]}")
            ;;
        num_workers)
            values=("${NUM_WORKERS_VALUES[@]}")
            ;;
        check_interval)
            values=("${CHECK_INTERVAL_VALUES[@]}")
            ;;
        *)
            echo -e "${RED}Unknown parameter: $param${NC}"
            return 1
            ;;
    esac
    
    echo -e "\n${CYAN}Sweeping $param: ${values[*]}${NC}"
    echo "----------------------------------------"
    
    for value in "${values[@]}"; do
        modify_param "$param" "$value"
        run_benchmark "$param=$value"
    done
    
    # Restore default for this parameter
    case $param in
        batch_size) modify_param "batch_size" $DEFAULT_BATCH_SIZE ;;
        batch_timeout) modify_param "batch_timeout" $DEFAULT_BATCH_TIMEOUT ;;
        num_workers) modify_param "num_workers" $DEFAULT_NUM_WORKERS ;;
        check_interval) modify_param "check_interval" $DEFAULT_CHECK_INTERVAL ;;
    esac
}

# Write CSV header
write_header() {
    echo "label,batch_size,batch_timeout_us,num_workers,check_interval,tests_passed,tests_failed,total_time_ms" > "$OUTPUT_FILE"
}

# Main execution
main() {
    local param_to_sweep=${1:-"all"}
    
    # Check prerequisites
    if [ ! -d "$BUILD_DIR" ]; then
        echo -e "${RED}Error: Build directory not found.${NC}"
        exit 1
    fi
    
    echo -e "Output file: ${YELLOW}$OUTPUT_FILE${NC}"
    echo ""
    
    # Write CSV header
    write_header
    
    # Run baseline first
    echo -e "${CYAN}Running baseline (default values)...${NC}"
    run_benchmark "baseline"
    
    if [ "$param_to_sweep" == "all" ]; then
        # Sweep all parameters
        sweep_param "batch_size"
        sweep_param "batch_timeout"
        sweep_param "num_workers"
        sweep_param "check_interval"
    else
        # Sweep single parameter
        sweep_param "$param_to_sweep"
    fi
    
    echo ""
    echo -e "${GREEN}======================================${NC}"
    echo -e "${GREEN}  Sweep complete!${NC}"
    echo -e "${GREEN}======================================${NC}"
    echo ""
    echo -e "Results saved to: ${YELLOW}$OUTPUT_FILE${NC}"
    echo ""
    echo -e "${BLUE}Summary:${NC}"
    echo "----------------------------------------"
    column -t -s',' "$OUTPUT_FILE"
    echo "----------------------------------------"
}

# Print usage if --help
if [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
    echo "Usage: $0 [parameter_name]"
    echo ""
    echo "Runs automated parameter sweep for Enhanced OCC."
    echo ""
    echo "Parameters:"
    echo "  batch_size     - Test values: ${BATCH_SIZE_VALUES[*]}"
    echo "  batch_timeout  - Test values: ${BATCH_TIMEOUT_VALUES[*]}"
    echo "  num_workers    - Test values: ${NUM_WORKERS_VALUES[*]}"
    echo "  check_interval - Test values: ${CHECK_INTERVAL_VALUES[*]}"
    echo "  all            - Test all parameters (default)"
    echo ""
    echo "Examples:"
    echo "  $0                     # Full sweep"
    echo "  $0 batch_size          # Sweep only batch_size"
    echo "  $0 num_workers         # Sweep only num_workers"
    exit 0
fi

main "$@"

