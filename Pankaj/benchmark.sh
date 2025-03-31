#!/bin/bash

# Configuration
ITERATIONS=20            # Number of iterations for each test
INPUT_FILE="data_64_64_96_7.txt"
PX=2
PY=2
PZ=2
NX=64
NY=64
NZ=96
TIME_STEPS=7
PROCS=8                 # Total processes (should equal PX*PY*PZ)

# Create output directory for results
mkdir -p performance_results

# Function to run a test with multiple iterations
run_test() {
    implementation=$1    # "original" or "optimized"
    executable=$2        # Path to the executable

    echo "Running $implementation implementation ($ITERATIONS iterations)..."

    # Arrays to store timing results
    read_times=()
    main_times=()
    total_times=()

    # Run the specified number of iterations
    for i in $(seq 1 $ITERATIONS); do
        echo "  Iteration $i/$ITERATIONS..."

        # Create unique output filename for this iteration
        output_file="performance_results/${implementation}_iter${i}.txt"

        # Run the code
        mpirun -np $PROCS $executable $INPUT_FILE $PX $PY $PZ $NX $NY $NZ $TIME_STEPS $output_file

        # Extract timing information
        timing=$(tail -n 1 $output_file)
        read_time=$(echo $timing | cut -d',' -f1)
        main_time=$(echo $timing | cut -d',' -f2)
        total_time=$(echo $timing | cut -d',' -f3)

        # Store times in arrays
        read_times+=($read_time)
        main_times+=($main_time)
        total_times+=($total_time)

        # Sleep a bit between iterations to reduce cache effects
        sleep 1
    done

    # Calculate averages
    read_sum=0
    main_sum=0
    total_sum=0

    for i in $(seq 0 $((ITERATIONS-1))); do
        read_sum=$(echo "$read_sum + ${read_times[$i]}" | bc -l)
        main_sum=$(echo "$main_sum + ${main_times[$i]}" | bc -l)
        total_sum=$(echo "$total_sum + ${total_times[$i]}" | bc -l)
    done

    read_avg=$(echo "scale=6; $read_sum / $ITERATIONS" | bc -l)
    main_avg=$(echo "scale=6; $main_sum / $ITERATIONS" | bc -l)
    total_avg=$(echo "scale=6; $total_sum / $ITERATIONS" | bc -l)

    # Display individual results
    echo "  Individual results:"
    for i in $(seq 0 $((ITERATIONS-1))); do
        echo "    Iteration $((i+1)): Read=${read_times[$i]}, Main=${main_times[$i]}, Total=${total_times[$i]}"
    done

    # Display average results
    echo "  Average results ($ITERATIONS iterations):"
    echo "    Read time: $read_avg seconds"
    echo "    Main code time: $main_avg seconds"
    echo "    Total time: $total_avg seconds"

    # Save averages to a file for later comparison
    echo "$read_avg,$main_avg,$total_avg" > "performance_results/${implementation}_average.txt"
}

# Clear screen and print header
clear
echo "========================================"
echo "Performance Comparison: Original vs. Optimized"
echo "Grid: ${PX}x${PY}x${PZ} processes (total: $PROCS)"
echo "Data: ${NX}x${NY}x${NZ} grid, $TIME_STEPS time steps"
echo "Running $ITERATIONS iterations per implementation"
echo "========================================"
echo ""

# Run tests for both implementations
run_test "original" "./pankaj_code7"
echo ""
run_test "optimized" "./pankaj_code9"
echo ""

# Compare average results
if [ -f "performance_results/original_average.txt" ] && [ -f "performance_results/optimized_average.txt" ]; then
    orig_avg=$(cat "performance_results/original_average.txt")
    opt_avg=$(cat "performance_results/optimized_average.txt")

    orig_read=$(echo $orig_avg | cut -d',' -f1)
    orig_main=$(echo $orig_avg | cut -d',' -f2)
    orig_total=$(echo $orig_avg | cut -d',' -f3)

    opt_read=$(echo $opt_avg | cut -d',' -f1)
    opt_main=$(echo $opt_avg | cut -d',' -f2)
    opt_total=$(echo $opt_avg | cut -d',' -f3)

    # Calculate improvement percentages
    read_improve=$(echo "scale=2; ($orig_read-$opt_read)/$orig_read*100" | bc -l)
    main_improve=$(echo "scale=2; ($orig_main-$opt_main)/$orig_main*100" | bc -l)
    total_improve=$(echo "scale=2; ($orig_total-$opt_total)/$orig_total*100" | bc -l)

    echo "========================================"
    echo "PERFORMANCE SUMMARY"
    echo "========================================"
    echo "Original implementation (average of $ITERATIONS runs):"
    echo "  Read time:      $orig_read seconds"
    echo "  Main code time: $orig_main seconds"
    echo "  Total time:     $orig_total seconds"
    echo ""
    echo "Optimized implementation (average of $ITERATIONS runs):"
    echo "  Read time:      $opt_read seconds"
    echo "  Main code time: $opt_main seconds"
    echo "  Total time:     $opt_total seconds"
    echo ""
    echo "Performance improvement:"
    echo "  Read time:      $read_improve%"
    echo "  Main code time: $main_improve%"
    echo "  Total time:     $total_improve%"
    echo "========================================"

    # Generate a simple CSV for potential graphing
    echo "Metric,Original,Optimized,Improvement(%)" > performance_results/comparison.csv
    echo "Read Time,$orig_read,$opt_read,$read_improve" >> performance_results/comparison.csv
    echo "Main Code Time,$orig_main,$opt_main,$main_improve" >> performance_results/comparison.csv
    echo "Total Time,$orig_total,$opt_total,$total_improve" >> performance_results/comparison.csv


    echo "Results saved to performance_results/comparison.csv"
fi

echo ""
echo "All individual results saved in the performance_results directory"
