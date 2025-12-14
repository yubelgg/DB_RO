#!/bin/bash
#
# analyze_occ_results.sh - Parse and compare OCC benchmark results
#
# Reads the latest N CSV files and displays a comparison table
# with TPS, abort rates, and improvement percentages vs baseline.
#
# Usage:
#   ./scripts/analyze_occ_results.sh       # Analyze latest 3 files
#   ./scripts/analyze_occ_results.sh 5     # Analyze latest 5 files
#

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Number of CSVs to analyze (default: 3)
NUM_FILES=${1:-3}

# Find project root and build directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

# Change to build directory where CSVs are stored
cd "$BUILD_DIR" 2>/dev/null || {
  echo -e "${RED}Error: Build directory not found at $BUILD_DIR${NC}"
  exit 1
}

# Find latest CSV files (most recent first)
mapfile -t CSVS < <(ls -t results_*.csv 2>/dev/null | head -"${NUM_FILES}")

if [ ${#CSVS[@]} -eq 0 ]; then
  echo -e "${RED}Error: No results_*.csv files found${NC}"
  echo "Run some tests first: ./labtest -f ../config/tpcc_occ_baseline.yml -d 10"
  exit 1
fi

if [ ${#CSVS[@]} -lt 3 ]; then
  echo -e "${YELLOW}Warning: Only found ${#CSVS[@]} CSV files (need 3 for full comparison)${NC}"
fi

echo ""
echo -e "${BLUE}=== OCC Benchmark Comparison ===${NC}"
echo ""
printf "%-16s %8s %10s %12s %16s\n" "Mode" "TPS" "Abort%" "vs Baseline" "Abort Reduction"
echo "------------------------------------------------------------------------"

# First pass: find baseline TPS and abort rate
baseline_tps=0
baseline_abort=0

for csv in "${CSVS[@]}"; do
  # Parse last line (skip header)
  line=$(tail -1 "$csv")

  mode=$(echo "$line" | cut -d',' -f2)
  tps=$(echo "$line" | cut -d',' -f8)
  abort_rate=$(echo "$line" | cut -d',' -f7)

  if [ "$mode" == "occ" ]; then
    baseline_tps=$tps
    baseline_abort=$abort_rate
    break
  fi
done

# Second pass: print all results with improvements
for csv in "${CSVS[@]}"; do
  # Parse last line
  line=$(tail -1 "$csv")

  mode=$(echo "$line" | cut -d',' -f2)
  tps=$(echo "$line" | cut -d',' -f8)
  abort_rate=$(echo "$line" | cut -d',' -f7)

  # Convert abort rate to percentage using awk (more portable than bc)
  abort_pct=$(awk "BEGIN {printf \"%.1f\", $abort_rate * 100}")

  if [ "$mode" == "occ" ]; then
    # Baseline row
    printf "%-16s %8.1f %9.1f%% %12s %16s\n" "$mode" "$tps" "$abort_pct" "(baseline)" "-"
  else
    # Calculate improvements vs baseline using awk
    if awk "BEGIN {exit !($baseline_tps > 0)}"; then
      tps_improve=$(awk "BEGIN {printf \"%.1f\", (($tps - $baseline_tps) / $baseline_tps) * 100}")
      abort_improve=$(awk "BEGIN {printf \"%.1f\", (($baseline_abort - $abort_rate) / $baseline_abort) * 100}")

      # Format with + sign for positive values
      if awk "BEGIN {exit !($tps_improve >= 0)}"; then
        tps_str="+${tps_improve}%"
      else
        tps_str="${tps_improve}%"
      fi

      if awk "BEGIN {exit !($abort_improve >= 0)}"; then
        abort_str="+${abort_improve}%"
      else
        abort_str="${abort_improve}%"
      fi

      printf "%-16s %8.1f %9.1f%% %12s %16s\n" "$mode" "$tps" "$abort_pct" "$tps_str" "$abort_str"
    else
      printf "%-16s %8.1f %9.1f%% %12s %16s\n" "$mode" "$tps" "$abort_pct" "N/A" "N/A"
    fi
  fi
done

echo "------------------------------------------------------------------------"
echo ""
echo -e "${BLUE}Legend:${NC}"
echo "  vs Baseline     = TPS improvement percentage"
echo "  Abort Reduction = reduction in abort rate (higher is better)"
echo ""
echo -e "${BLUE}Files analyzed:${NC}"
for csv in "${CSVS[@]}"; do
  echo "  - $csv"
done
