

# Variables
BUILD_DIR = build

# OCC Early Abort Detection Component Variables
CXX = g++
CXXFLAGS_OCC = -std=c++17 -Wall -Wextra -pthread -Isrc/deptran/occ
OPTFLAGS = -O3
DEBUGFLAGS = -g -DDEBUG

# OCC Source files (in src/deptran/occ/)
OCC_DIR = src/deptran/occ
OCC_TEST_DIR = test
OCC_SRCS = $(OCC_DIR)/conflict_graph.cc $(OCC_DIR)/early_abort_detector.cc
OCC_TEST_SRC = $(OCC_TEST_DIR)/test_early_abort.cc
OCC_INTEGRATION_TEST_SRC = $(OCC_TEST_DIR)/test_occ_focused_integration.cc
OCC_HEADERS = $(OCC_DIR)/conflict_graph.h $(OCC_DIR)/early_abort_detector.h $(OCC_DIR)/bloom_filter.h $(OCC_DIR)/concurrent_map.h

# OCC Object files
OCC_OBJS = $(OCC_DIR)/conflict_graph.o $(OCC_DIR)/early_abort_detector.o
OCC_TEST_OBJ = $(OCC_TEST_DIR)/test_early_abort.o
OCC_INTEGRATION_TEST_OBJ = $(OCC_TEST_DIR)/test_occ_focused_integration.o


# OCC Targets
OCC_TARGET = test_early_abort
OCC_TARGET_DEBUG = test_early_abort_debug

OCC_INTEGRATION_TARGET=test_occ_focused_integration
OCC_INTEGRATION_TARGET_DEBUG=test_occ_focused_integration_debug



.PHONY: all configure configure-labtest build dbtest labtest clean rebuild run test test-verbose test-parallel run-raft-tests run-kv-tests run-shard-tests run-all-lab-tests occ-release occ-debug occ-test occ-clean occ-install occ-help

# Default target: build labtest with slim build (excludes erpc, rust, mako)
all: labtest

# Standard configure (full build with mako, erpc, rust)
configure:
	cmake -S . -B $(BUILD_DIR)

# Configure for lab tests (slim build - excludes erpc, rust, mako)
configure-labtest:
	cmake -S . -B $(BUILD_DIR) -DBUILD_RAFT_LAB_TESTS=ON

# Full build (all targets)
build: configure
	@echo "Building with $(if $(filter -j%,$(MAKEFLAGS)),$(subst -j,,$(filter -j%,$(MAKEFLAGS))),4) parallel jobs..."
	cmake --build $(BUILD_DIR) --parallel $(if $(filter -j%,$(MAKEFLAGS)),$(subst -j,,$(filter -j%,$(MAKEFLAGS))),4)

# Build dbtest (requires full build with mako)
dbtest: configure
	@echo "Building dbtest target..."
	cmake --build $(BUILD_DIR) --target dbtest --parallel $(if $(filter -j%,$(MAKEFLAGS)),$(subst -j,,$(filter -j%,$(MAKEFLAGS))),4)

# Build labtest (slim build - default target) and OCC tests
labtest: configure-labtest $(OCC_TARGET) $(OCC_INTEGRATION_TARGET)
	@echo "Building labtest target (slim build)..."
	cmake --build $(BUILD_DIR) --target labtest --parallel $(if $(filter -j%,$(MAKEFLAGS)),$(subst -j,,$(filter -j%,$(MAKEFLAGS))),4)  

clean:
	rm -rf $(BUILD_DIR)
	# Clean out-perf.masstree
	rm -rf ./out-perf.masstree/*
	# Clean mako out-perf.masstree
	rm -rf ./src/mako/out-perf.masstree/*
	# Clean Masstree configuration
	@echo "Cleaning Masstree configuration..."
	@cd src/mako/masstree && make distclean 2>/dev/null || true
	@rm -f src/mako/masstree/config.h src/mako/masstree/config.h.in
	@rm -f src/mako/masstree/configure src/mako/masstree/config.status
	@rm -f src/mako/masstree/config.log src/mako/masstree/GNUmakefile
	@rm -f src/mako/masstree/autom4te.cache -rf
	# Clean LZ4 library
	@echo "Cleaning LZ4 library..."
	@cd third-party/lz4 && make clean 2>/dev/null || true
	@rm -f third-party/lz4/liblz4.so third-party/lz4/*.o
	# Clean Rust library
	@echo "Cleaning Rust library..."
	@cd rust-lib && cargo clean 2>/dev/null || true
	# Clean rusty-cpp
	@rm -rf third-party/rusty-cpp/target || true
	# Clean OCC components
	@$(MAKE) occ-clean




rebuild: clean all

run: build
	./$(BUILD_DIR)/dbtest
	./$(BUILD_DIR)/simpleTransction
	./$(BUILD_DIR)/simpleTransctionRep
	./$(BUILD_DIR)/simplePaxos

# Run Raft lab tests
run-raft-tests: labtest
	@echo "Running Raft lab tests..."
	./$(BUILD_DIR)/labtest -f config/raft_lab_test.yml

# Run KV lab tests
run-kv-tests: labtest
	@echo "Running KV lab tests..."
	./$(BUILD_DIR)/labtest -f config/kv_lab_test.yml

# Run Shard lab tests
run-shard-tests: labtest
	@echo "Running Shard lab tests..."
	./$(BUILD_DIR)/labtest -f config/shard_lab_test.yml

# Run all lab tests
run-all-lab-tests: labtest
	@echo "Running all lab tests..."
	./test/run_all_lab_tests.sh

# Run tests using ctest
test: build
	@echo "Running tests..."
	@cd $(BUILD_DIR) && ctest --output-on-failure

# Run tests with verbose output
test-verbose: build
	@echo "Running tests with verbose output..."
	@cd $(BUILD_DIR) && ctest --verbose --output-on-failure

# Run tests in parallel
test-parallel: build
	@echo "Running tests in parallel..."
	@cd $(BUILD_DIR) && ctest -j$(if $(filter -j%,$(MAKEFLAGS)),$(subst -j,,$(filter -j%,$(MAKEFLAGS))),4) --output-on-failure

# ============================================================================
# OCC Early Abort Detection Component Targets
# ============================================================================

# OCC Release build
occ-release: CXXFLAGS_OCC += $(OPTFLAGS)
occ-release: $(OCC_TARGET)

# OCC Debug build
occ-debug: CXXFLAGS_OCC += $(DEBUGFLAGS)
occ-debug: $(OCC_TARGET_DEBUG)

# Link OCC test executable
$(OCC_TARGET): $(OCC_OBJS) $(OCC_TEST_OBJ)
	$(CXX) $(CXXFLAGS_OCC) -o $@ $^
	@echo "Build complete: $(OCC_TARGET)"
	@echo "Run with: ./$(OCC_TARGET)"

# Link OCC test executable
$(OCC_INTEGRATION_TARGET): $(OCC_OBJS) $(OCC_INTEGRATION_TEST_OBJ)
	$(CXX) $(CXXFLAGS_OCC) -o $@ $^
	@echo "Build complete: $(OCC_INTEGRATION_TARGET)"
	@echo "Run with: ./$(OCC_INTEGRATION_TARGET)"


# Link OCC debug test executable
$(OCC_TARGET_DEBUG): $(OCC_OBJS) $(OCC_TEST_OBJ)
	$(CXX) $(CXXFLAGS_OCC) -o $@ $^
	@echo "Debug build complete: $(OCC_TARGET_DEBUG)"
	@echo "Run with: ./$(OCC_TARGET_DEBUG)"

# Compile OCC source files (main directory)
$(OCC_DIR)/%.o: $(OCC_DIR)/%.cc $(OCC_HEADERS)
	$(CXX) $(CXXFLAGS_OCC) -c $< -o $@

# Compile OCC test files (test subdirectory)
$(OCC_TEST_DIR)/%.o: $(OCC_TEST_DIR)/%.cc $(OCC_HEADERS)
	$(CXX) $(CXXFLAGS_OCC) -c $< -o $@

# Run OCC tests
occ-test: $(OCC_TARGET)
	@echo "Running OCC tests..."
	@./$(OCC_TARGET)

# Clean OCC build artifacts
occ-clean:
	rm -f $(OCC_OBJS) $(OCC_TEST_OBJ) $(OCC_TARGET) $(OCC_TARGET_DEBUG)
	@echo "Cleaned OCC build artifacts"

# Install OCC headers
occ-install:
	@echo "Installing OCC headers..."
	@mkdir -p /usr/local/include/deptran
	@cp $(OCC_HEADERS) /usr/local/include/deptran/
	@echo "Headers installed to /usr/local/include/deptran/"

# OCC Help
occ-help:
	@echo "OCC Early Abort Detection Component Targets:"
	@echo "  occ-release  - Build optimized OCC release version"
	@echo "  occ-debug    - Build OCC debug version with symbols"
	@echo "  occ-test     - Build and run OCC tests"
	@echo "  occ-clean    - Remove OCC build artifacts"
	@echo "  occ-install  - Install OCC headers to system directory"
	@echo "  occ-help     - Show OCC help message"
	@echo ""
	@echo "Examples:"
	@echo "  make occ-release      # Build OCC release version"
	@echo "  make occ-debug        # Build OCC debug version"
	@echo "  make occ-test         # Build and run OCC tests"

# Main help target
help:
	@echo "====================================================================="
	@echo "Main Project Build Targets:"
	@echo "====================================================================="
	@echo "  all                - Build labtest (default)"
	@echo "  configure          - Configure full build with CMake"
	@echo "  configure-labtest  - Configure slim build for lab tests"
	@echo "  build              - Full build (all targets)"
	@echo "  dbtest             - Build dbtest target"
	@echo "  labtest            - Build labtest target (slim build)"
	@echo "  clean              - Remove all build artifacts"
	@echo "  rebuild            - Clean and rebuild"
	@echo "  run                - Run built executables"
	@echo "  test               - Run tests using ctest"
	@echo "  test-verbose       - Run tests with verbose output"
	@echo "  test-parallel      - Run tests in parallel"
	@echo ""
	@echo "Lab Test Targets:"
	@echo "  run-raft-tests     - Run Raft lab tests"
	@echo "  run-kv-tests       - Run KV lab tests"
	@echo "  run-shard-tests    - Run Shard lab tests"
	@echo "  run-all-lab-tests  - Run all lab tests"
	@echo ""
	@echo "====================================================================="
	@echo "OCC Early Abort Detection Component Targets:"
	@echo "====================================================================="
	@$(MAKE) occ-help
	@echo ""
	@echo "For more information, see README.md"


