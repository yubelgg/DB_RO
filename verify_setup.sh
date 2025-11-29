#!/bin/bash
# Quick verification and fix for test directory setup

echo "========================================="
echo "OCC Test Directory Setup Verification"
echo "========================================="
echo ""

# Check 1: Current directory
echo "✓ Check 1: Current Directory"
CURRENT_DIR=$(pwd)
echo "  Current: $CURRENT_DIR"
if [[ "$CURRENT_DIR" == */DB_RO ]]; then
    echo "  ✅ GOOD: You're in the project root"
else
    echo "  ❌ PROBLEM: You should be in DB_RO directory"
    echo "  Run: cd ~/Documents/SBUFolder/FinalProject/DB_RO"
    exit 1
fi
echo ""

# Check 2: Test file location
echo "✓ Check 2: Test File Location"
if [ -f "src/deptran/occ/test/test_early_abort.cc" ]; then
    echo "  ✅ GOOD: Test file is in test/ directory"
else
    echo "  ❌ PROBLEM: Test file not found at src/deptran/occ/test/test_early_abort.cc"
    
    if [ -f "src/deptran/occ/test_early_abort.cc" ]; then
        echo "  Found at old location. Moving it..."
        mkdir -p src/deptran/occ/test
        mv src/deptran/occ/test_early_abort.cc src/deptran/occ/test/
        echo "  ✅ Moved to test/ directory"
    else
        echo "  ❌ Test file not found anywhere!"
        exit 1
    fi
fi
echo ""

# Check 3: Header files
echo "✓ Check 3: Header Files"
HEADERS=("early_abort_detector.h" "conflict_graph.h" "bloom_filter.h" "concurrent_map.h")
for header in "${HEADERS[@]}"; do
    if [ -f "src/deptran/occ/$header" ]; then
        echo "  ✅ Found: $header"
    else
        echo "  ❌ Missing: $header"
    fi
done
echo ""

# Check 4: Makefile include paths
echo "✓ Check 4: Makefile Include Paths"
if grep -q "CXXFLAGS_OCC.*-Isrc/deptran/occ.*-Isrc/deptran/occ/test" Makefile; then
    echo "  ✅ GOOD: Makefile has correct include paths"
    grep "CXXFLAGS_OCC" Makefile
else
    echo "  ❌ PROBLEM: Makefile missing correct include paths"
    echo "  Current setting:"
    grep "CXXFLAGS_OCC" Makefile || echo "  (not found)"
    echo ""
    echo "  Should be:"
    echo "  CXXFLAGS_OCC = -std=c++17 -Wall -Wextra -pthread -Isrc/deptran/occ -Isrc/deptran/occ/test"
    echo ""
    echo "  You need to update your Makefile!"
    exit 1
fi
echo ""

# Check 5: OCC_TEST_DIR variable
echo "✓ Check 5: Makefile Test Directory Variable"
if grep -q "OCC_TEST_DIR.*=.*test" Makefile; then
    echo "  ✅ GOOD: OCC_TEST_DIR variable exists"
    grep "OCC_TEST_DIR" Makefile
else
    echo "  ❌ PROBLEM: OCC_TEST_DIR variable not found"
    echo "  You need to update your Makefile!"
    exit 1
fi
echo ""

# Check 6: Test compilation rule
echo "✓ Check 6: Test Compilation Rule"
if grep -q '\$(OCC_TEST_DIR)/%.o:.*\$(OCC_TEST_DIR)/%.cc' Makefile; then
    echo "  ✅ GOOD: Test compilation rule exists"
else
    echo "  ⚠️  WARNING: Test compilation rule might be missing"
fi
echo ""

echo "========================================="
echo "All Checks Passed! ✅"
echo "========================================="
echo ""
echo "You can now run:"
echo "  make occ-clean"
echo "  make occ-test"
echo ""
