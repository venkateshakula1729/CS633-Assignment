#!/bin/bash

# run_scaling.sh - Run strong scaling experiments with different configurations

# Default values
IMPLEMENTATIONS="pankaj_code7 pankaj_code9 pankaj_code11"
ITERATIONS=1
OUTPUT_DIR="../results/scaling_$(date +%Y%m%d_%H%M%S)"

# Data configurations from the assignment
declare -a TEST_CASES=(
    "data_64_64_64_3.txt 2 2 2 64 64 64 3"
    # "data_64_64_64_3.txt 4 2 2 64 64 64 3"
    # "data_64_64_64_3.txt 4 4 2 64 64 64 3"
    # "data_64_64_64_3.txt 4 4 4 64 64 64 3"
    # "data_64_64_96_7.txt 2 2 2 64 64 96 7"
    # "data_64_64_96_7.txt 4 2 2 64 64 96 7"
    # "data_64_64_96_7.txt 4 4 2 64 64 96 7"
    # "data_64_64_96_7.txt 4 4 4 64 64 96 7"
)

# Create output directory
mkdir -p $OUTPUT_DIR

# Print header
echo "=========================================================="
echo "  Time Series Data Parallel Processing - Scaling Tests"
echo "  Output Directory: $OUTPUT_DIR"
echo "  Implementations: $IMPLEMENTATIONS"
echo "  Iterations per configuration: $ITERATIONS"
echo "=========================================================="

# First, build all implementations
echo "Building implementations..."
make clean && make all

# Function to extract process count from test case
get_process_count() {
    local test_case=$1
    local px=$(echo $test_case | awk '{print $2}')
    local py=$(echo $test_case | awk '{print $3}')
    local pz=$(echo $test_case | awk '{print $4}')
    echo $((px * py * pz))
}

# Run all test cases
for test_case in "${TEST_CASES[@]}"; do
    # Extract parameters
    INPUT_FILE="../data/$(echo $test_case | awk '{print $1}')"
    PX=$(echo $test_case | awk '{print $2}')
    PY=$(echo $test_case | awk '{print $3}')
    PZ=$(echo $test_case | awk '{print $4}')
    NX=$(echo $test_case | awk '{print $5}')
    NY=$(echo $test_case | awk '{print $6}')
    NZ=$(echo $test_case | awk '{print $7}')
    NC=$(echo $test_case | awk '{print $8}')

    # Calculate total processes
    PROCS=$((PX * PY * PZ))

    echo ""
    echo "======================================================="
    echo "Running test case: $test_case"
    echo "Total processes: $PROCS"
    echo "======================================================="

    # Run each implementation
    for IMPL in $IMPLEMENTATIONS; do
        EXECUTABLE="../src/bin/$IMPL"

        echo "Running $IMPL implementation..."

        # Create results directory for this configuration
        CONFIG_DIR="$OUTPUT_DIR/config_${PROCS}p_${NX}x${NY}x${NZ}_${NC}t"
        mkdir -p $CONFIG_DIR

        for i in $(seq 1 $ITERATIONS); do
            echo "  Iteration $i/$ITERATIONS..."
            OUTPUT_FILE="$CONFIG_DIR/${IMPL}_${PROCS}p_iter${i}.txt"
            # echo "    Output file: $OUTPUT_FILE"

            # Set data path
            # DATA_PATH="../data/$INPUT_FILE"
            echo "    Data path: $INPUT_FILE"

            echo "mpirun -np $PROCS $EXECUTABLE $INPUT_FILE $PX $PY $PZ $NX $NY $NZ $NC $OUTPUT_FILE"
            # Run the command
            mpirun -np $PROCS $EXECUTABLE $INPUT_FILE $PX $PY $PZ $NX $NY $NZ $NC $OUTPUT_FILE

            # Check for successful execution
            if [ $? -ne 0 ]; then
                echo "  ERROR: Execution failed!"
            fi
        done
    done
done

# Generate visualizations using the Python script
echo ""
echo "Generating visualization..."
python3 visualize.py $OUTPUT_DIR --output $OUTPUT_DIR/figures

echo ""
echo "All tests completed. Results saved to $OUTPUT_DIR"
