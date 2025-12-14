#!/bin/bash
# Comprehensive contention level testing for Enhanced OCC Phase 2
# Tests batch validation and early abort across different contention levels

set -e

BUILD_DIR="/home/ylwcs/Project/DB_RO/build"
CONFIG_DIR="/home/ylwcs/Project/DB_RO/config/generated"
RESULTS_FILE="/home/ylwcs/Project/DB_RO/contention_results.csv"

# Create config directory
mkdir -p "$CONFIG_DIR"

# Test duration in seconds
DURATION=10

# Contention levels (population sizes - more keys = less contention)
# With 16 concurrent txs and 20 ops/tx, contention = (16 * 20) / population
POPULATIONS=(500 1000 2000 5000 10000 20000)

# Configurations to test
declare -A CONFIGS
CONFIGS["baseline"]="occ:false:false:false"
CONFIGS["enhanced_threading_batch"]="occ_enhanced:true:true:false"
CONFIGS["enhanced_threading_early"]="occ_enhanced:true:false:true"
CONFIGS["enhanced_threading_both"]="occ_enhanced:true:true:true"

# Generate config file
generate_config() {
    local name=$1
    local cc_mode=$2
    local threading=$3
    local batch=$4
    local early=$5
    local population=$6
    local config_file="$CONFIG_DIR/${name}_pop${population}.yml"

    cat > "$config_file" << EOF
site:
  server:
    - ["s101:8100"]
  client:
    - ["c101"]

process:
  s101: localhost
  c101: localhost

host:
  localhost: 127.0.0.1

n_concurrent: 16

mode:
  cc: $cc_mode
  ab: none
  read_only: $cc_mode
  batch: false
  retry: 20
  ongoing: 16

execution_threading:
  enabled: $threading
  num_workers: 8

batch_validation:
  enabled: $batch
  batch_size: 32
  batch_timeout_us: 100
  num_workers: 4
  parallel_threshold: 4

early_abort:
  enabled: $early
  check_interval: 10
  bloom_filter_size: 10000

epoch_commit:
  enabled: false

bench:
  workload: rw
  scale: 1
  weight:
    read: 0.5
    write: 0.5
  population:
    history: $population

ops_per_txn: 20

schema:
  - name: history
    column:
      - {name: h_key, type: integer, primary: true}
      - {name: h_c_id, type: integer}

sharding:
  history: MOD
EOF
    echo "$config_file"
}

# Run test and extract results
run_test() {
    local config_file=$1
    local timeout_sec=$((DURATION + 30))

    # Run the test
    cd "$BUILD_DIR"
    output=$(timeout ${timeout_sec}s ./labtest -f "$config_file" -d $DURATION 2>&1) || true

    # Extract from final summary line: "Total: X, Commit: Y, Attempts: Z, Running for T"
    total=$(echo "$output" | grep -oP 'Total:\s*\K[0-9]+' | tail -1)
    commit=$(echo "$output" | grep -oP 'Commit:\s*\K[0-9]+' | tail -1)
    attempts=$(echo "$output" | grep -oP 'Attempts:\s*\K[0-9]+' | tail -1)
    duration=$(echo "$output" | grep -oP 'Running for \K[0-9]+' | tail -1)

    # Calculate TPS using bash arithmetic
    if [ -n "$total" ] && [ -n "$duration" ] && [ "$duration" -gt 0 ]; then
        tps=$((total / duration))
    else
        tps=0
    fi

    # Calculate abort rate using bash arithmetic (multiply by 100 first for precision)
    if [ -n "$attempts" ] && [ -n "$commit" ] && [ "$attempts" -gt 0 ]; then
        aborts=$((attempts - commit))
        # Calculate percentage with 2 decimal precision using integer math
        abort_pct_x100=$((aborts * 10000 / attempts))
        abort_int=$((abort_pct_x100 / 100))
        abort_frac=$((abort_pct_x100 % 100))
        abort_rate="${abort_int}.$(printf '%02d' $abort_frac)"
    else
        abort_rate="0.00"
    fi

    echo "$tps,$abort_rate"
}

# Main test loop
echo "Configuration,Population,Contention,TPS,AbortRate" > "$RESULTS_FILE"

for pop in "${POPULATIONS[@]}"; do
    # Determine contention level label based on collision probability
    # collision_prob = (n_concurrent * ops_per_txn) / population
    # 16 * 20 = 320 ops competing for keys
    if [ "$pop" -le 500 ]; then
        contention="very_high"    # 64%+ collision prob
    elif [ "$pop" -le 1000 ]; then
        contention="high"         # 32%+ collision prob
    elif [ "$pop" -le 2000 ]; then
        contention="moderate"     # 16%+ collision prob
    elif [ "$pop" -le 5000 ]; then
        contention="low"          # 6.4% collision prob
    else
        contention="very_low"     # <3.2% collision prob
    fi

    echo "============================================"
    echo "Testing Population: $pop keys ($contention contention)"
    echo "============================================"

    for config_name in "${!CONFIGS[@]}"; do
        IFS=':' read -r cc_mode threading batch early <<< "${CONFIGS[$config_name]}"

        echo -n "  Running: $config_name... "
        config_file=$(generate_config "$config_name" "$cc_mode" "$threading" "$batch" "$early" "$pop")

        result=$(run_test "$config_file")
        tps=$(echo "$result" | cut -d',' -f1)
        abort=$(echo "$result" | cut -d',' -f2)

        echo "TPS: $tps, Abort: $abort%"
        echo "$config_name,$pop,$contention,$tps,$abort" >> "$RESULTS_FILE"

        # Small delay between tests
        sleep 2
    done
done

echo ""
echo "============================================"
echo "RESULTS SUMMARY"
echo "============================================"
echo ""
column -t -s',' "$RESULTS_FILE"
echo ""
echo "Results saved to: $RESULTS_FILE"
