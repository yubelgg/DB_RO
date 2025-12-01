#!/bin/bash
#
# tune_occ.sh - Enhanced OCC Parameter Tuning Script
#
# This script modifies OCC parameters, rebuilds, runs tests, and restores defaults.
#
# Usage:
#   ./scripts/tune_occ.sh <batch_size> <batch_timeout> <num_workers> <check_interval>
#
# Example:
#   ./scripts/tune_occ.sh 64 200 4 20
#
# Parameters:
#   batch_size     - Max transactions per batch (default: 32)
#   batch_timeout  - Max wait time in microseconds (default: 100)
#   num_workers    - Parallel validation threads (default: 8)
#   check_interval - Operations between abort checks (default: 10)
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Project root (script is in scripts/)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Source files to modify
SCHEDULER_H="$PROJECT_ROOT/src/deptran/occ/scheduler_enhanced.h"
SCHEDULER_CC="$PROJECT_ROOT/src/deptran/occ/scheduler_enhanced.cc"
TX_ENHANCED_H="$PROJECT_ROOT/src/deptran/occ/tx_enhanced.h"

# Default values
DEFAULT_BATCH_SIZE=32
DEFAULT_BATCH_TIMEOUT=100
DEFAULT_NUM_WORKERS=8
DEFAULT_CHECK_INTERVAL=10

# Parse arguments
BATCH_SIZE=${1:-$DEFAULT_BATCH_SIZE}
BATCH_TIMEOUT=${2:-$DEFAULT_BATCH_TIMEOUT}
NUM_WORKERS=${3:-$DEFAULT_NUM_WORKERS}
CHECK_INTERVAL=${4:-$DEFAULT_CHECK_INTERVAL}

echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}  Enhanced OCC Parameter Tuning${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""
echo -e "Parameters:"
echo -e "  batch_size:     ${YELLOW}$BATCH_SIZE${NC} (default: $DEFAULT_BATCH_SIZE)"
echo -e "  batch_timeout:  ${YELLOW}$BATCH_TIMEOUT${NC} μs (default: $DEFAULT_BATCH_TIMEOUT)"
echo -e "  num_workers:    ${YELLOW}$NUM_WORKERS${NC} (default: $DEFAULT_NUM_WORKERS)"
echo -e "  check_interval: ${YELLOW}$CHECK_INTERVAL${NC} (default: $DEFAULT_CHECK_INTERVAL)"
echo ""

# Function to backup files
backup_files() {
    echo -e "${BLUE}[1/5] Backing up source files...${NC}"
    cp "$SCHEDULER_H" "$SCHEDULER_H.bak"
    cp "$SCHEDULER_CC" "$SCHEDULER_CC.bak"
    cp "$TX_ENHANCED_H" "$TX_ENHANCED_H.bak"
    echo -e "${GREEN}  Backup created${NC}"
}

# Function to modify parameters
modify_parameters() {
    echo -e "${BLUE}[2/5] Modifying parameters...${NC}"
    
    # Modify batch_size in scheduler_enhanced.h
    # Line: size_t batch_size_ = 32;
    sed -i "s/size_t batch_size_ = [0-9]*;/size_t batch_size_ = $BATCH_SIZE;/" "$SCHEDULER_H"
    echo -e "  batch_size_ = $BATCH_SIZE"
    
    # Modify batch_timeout in scheduler_enhanced.h
    # Line: std::chrono::microseconds batch_timeout_{100};
    sed -i "s/batch_timeout_{[0-9]*}/batch_timeout_{$BATCH_TIMEOUT}/" "$SCHEDULER_H"
    echo -e "  batch_timeout_ = $BATCH_TIMEOUT μs"
    
    # Modify num_workers in scheduler_enhanced.cc
    # Line: 8 // num_workers for parallel validation
    sed -i "s/[0-9]* \/\/ num_workers for parallel validation/$NUM_WORKERS \/\/ num_workers for parallel validation/" "$SCHEDULER_CC"
    echo -e "  num_workers = $NUM_WORKERS"
    
    # Modify check_interval in tx_enhanced.h
    # Line: size_t check_interval_ = 10;
    sed -i "s/size_t check_interval_ = [0-9]*;/size_t check_interval_ = $CHECK_INTERVAL;/" "$TX_ENHANCED_H"
    echo -e "  check_interval_ = $CHECK_INTERVAL"
    
    echo -e "${GREEN}  Parameters modified${NC}"
}

# Function to rebuild
rebuild() {
    echo -e "${BLUE}[3/5] Rebuilding project...${NC}"
    cd "$PROJECT_ROOT/build"
    make -j$(nproc) 2>&1 | tail -5
    echo -e "${GREEN}  Build complete${NC}"
}

# Function to run tests
run_tests() {
    echo -e "${BLUE}[4/5] Running OCC tests...${NC}"
    cd "$PROJECT_ROOT/build"
    
    echo ""
    echo -e "${YELLOW}Test Results:${NC}"
    echo "----------------------------------------"
    
    # Run the OCC-related tests
    ctest -R "BloomFilter|ConcurrentMap|ConflictGraph|BatchValidator|SchedulerEnhanced|ValidationQueue|EarlyAbortDetector" --output-on-failure 2>&1 | grep -E "(Passed|Failed|Test #|tests passed)"
    
    echo "----------------------------------------"
    echo ""
}

# Function to restore original files
restore_files() {
    echo -e "${BLUE}[5/5] Restoring original files...${NC}"
    mv "$SCHEDULER_H.bak" "$SCHEDULER_H"
    mv "$SCHEDULER_CC.bak" "$SCHEDULER_CC"
    mv "$TX_ENHANCED_H.bak" "$TX_ENHANCED_H"
    echo -e "${GREEN}  Original files restored${NC}"
}

# Cleanup function for trap
cleanup() {
    if [ -f "$SCHEDULER_H.bak" ]; then
        echo -e "\n${YELLOW}Cleaning up...${NC}"
        restore_files
    fi
}

# Set trap for cleanup on exit
trap cleanup EXIT

# Main execution
main() {
    # Check if files exist
    if [ ! -f "$SCHEDULER_H" ] || [ ! -f "$SCHEDULER_CC" ] || [ ! -f "$TX_ENHANCED_H" ]; then
        echo -e "${RED}Error: Source files not found. Run from project root.${NC}"
        exit 1
    fi
    
    # Check if build directory exists
    if [ ! -d "$PROJECT_ROOT/build" ]; then
        echo -e "${RED}Error: Build directory not found. Run 'cmake' first.${NC}"
        exit 1
    fi
    
    backup_files
    modify_parameters
    rebuild
    run_tests
    restore_files
    
    echo -e "${GREEN}======================================${NC}"
    echo -e "${GREEN}  Tuning complete!${NC}"
    echo -e "${GREEN}======================================${NC}"
    echo ""
    echo "Results recorded for:"
    echo "  batch_size=$BATCH_SIZE, batch_timeout=$BATCH_TIMEOUT, num_workers=$NUM_WORKERS, check_interval=$CHECK_INTERVAL"
    echo ""
    echo "To test more values, run again with different parameters."
}

# Print usage if --help
if [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
    echo "Usage: $0 <batch_size> <batch_timeout> <num_workers> <check_interval>"
    echo ""
    echo "Parameters:"
    echo "  batch_size     - Max transactions per batch (default: 32)"
    echo "  batch_timeout  - Max wait time in microseconds (default: 100)"
    echo "  num_workers    - Parallel validation threads (default: 8)"
    echo "  check_interval - Operations between abort checks (default: 10)"
    echo ""
    echo "Examples:"
    echo "  $0                     # Use defaults"
    echo "  $0 64 200 4 20         # Custom values"
    echo "  $0 128 500 16 5        # High throughput config"
    exit 0
fi

main

