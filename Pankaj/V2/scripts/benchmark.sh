#!/bin/bash

# ===================== CONFIGURATION =====================
# Implementations to benchmark
declare -A IMPLEMENTATIONS
IMPLEMENTATIONS["send"]="../src/bin/pankaj_code7"
IMPLEMENTATIONS["isend"]="../src/bin/pankaj_code11"
IMPLEMENTATIONS["ind_IO"]="../src/bin/independentIO"
# IMPLEMENTATIONS["coll_IO"]="../src/bin/collectiveIO"
# IMPLEMENTATIONS["ind_IO_der"]="../src/bin/independentIO_derData"
# IMPLEMENTATIONS["coll_IO_der"]="../src/bin/collectiveIO_derData"

# Datasets
DATASETS=(
    # "../data/art_data_256_256_256_7.bin"
    "../data/data_64_64_96_7.bin"
)

# Process counts to test
PROCESS_COUNTS=(8)

# Number of iterations per configuration
ITERATIONS=2

# Timeout in seconds
TIMEOUT=600  # 10 minutes

# Output directory
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="../results/benchmark_${TIMESTAMP}"
RAW_DIR="${OUTPUT_DIR}/raw"

# Specific process decompositions for each process count
# Format: "process_count:px,py,pz"
declare -A PROCESS_DECOMPOSITIONS
PROCESS_DECOMPOSITIONS["8"]="2,2,2"
PROCESS_DECOMPOSITIONS["16"]="4,2,2"
PROCESS_DECOMPOSITIONS["32"]="4,4,2"
PROCESS_DECOMPOSITIONS["64"]="4,4,4"

# ===================== UTILITY FUNCTIONS =====================

# Save benchmark configuration to JSON file
save_configuration() {
    local config_file="${OUTPUT_DIR}/config.json"

    echo "{" > "$config_file"
    echo "  \"timestamp\": \"${TIMESTAMP}\"," >> "$config_file"
    echo "  \"implementations\": [" >> "$config_file"

    local impl_keys=("${!IMPLEMENTATIONS[@]}")
    for ((i=0; i<${#impl_keys[@]}; i++)); do
        local key="${impl_keys[$i]}"
        echo -n "    \"${key}\"" >> "$config_file"
        if [[ $i -lt $((${#impl_keys[@]}-1)) ]]; then
            echo "," >> "$config_file"
        else
            echo "" >> "$config_file"
        fi
    done

    echo "  ]," >> "$config_file"
    echo "  \"datasets\": [" >> "$config_file"

    for ((i=0; i<${#DATASETS[@]}; i++)); do
        echo -n "    \"${DATASETS[$i]}\"" >> "$config_file"
        if [[ $i -lt $((${#DATASETS[@]}-1)) ]]; then
            echo "," >> "$config_file"
        else
            echo "" >> "$config_file"
        fi
    done

    echo "  ]," >> "$config_file"
    echo "  \"process_counts\": [${PROCESS_COUNTS[@]}]," >> "$config_file"
    echo "  \"iterations\": ${ITERATIONS}," >> "$config_file"
    echo "  \"timeout\": ${TIMEOUT}" >> "$config_file"
    echo "}" >> "$config_file"
}

# Extract dimensions from dataset filename
parse_dimensions() {
    local dataset="$1"
    local basename=$(basename "$dataset")

    if [[ $basename =~ data_([0-9]+)_([0-9]+)_([0-9]+)_([0-9]+) ]]; then
        nx=${BASH_REMATCH[1]}
        ny=${BASH_REMATCH[2]}
        nz=${BASH_REMATCH[3]}
        timesteps=${BASH_REMATCH[4]}
        echo "$nx $ny $nz $timesteps"
    else
        echo "Warning: Could not parse dimensions from $dataset" >&2
        return 1
    fi
}

# Determine process grid decomposition
get_decomposition() {
    local processes="$1"
    local nx="$2"
    local ny="$3"
    local nz="$4"

    # Check if we have a specific decomposition for this process count
    if [[ -n "${PROCESS_DECOMPOSITIONS[$processes]}" ]]; then
        IFS=',' read -r px py pz <<< "${PROCESS_DECOMPOSITIONS[$processes]}"

        # Check if dimensions are divisible by the decomposition
        if (( nx % px == 0 && ny % py == 0 && nz % pz == 0 && px * py * pz == processes )); then
            echo "$px $py $pz"
            return 0
        else
            echo "Warning: Configured decomposition $px,$py,$pz is not valid for $processes processes" >&2
        fi
    fi

    # Try to find a balanced decomposition
    local best_balance=9999
    local best_px=0
    local best_py=0
    local best_pz=0

    for ((px=1; px<=processes; px++)); do
        if (( processes % px == 0 && nx % px == 0 )); then
            local remaining=$((processes / px))

            for ((py=1; py<=remaining; py++)); do
                if (( remaining % py == 0 && ny % py == 0 )); then
                    local pz=$((remaining / py))

                    if (( nz % pz == 0 )); then
                        # Calculate balance factor (ratio of max to min)
                        local max=$px
                        local min=$px

                        [[ $py -gt $max ]] && max=$py
                        [[ $py -lt $min ]] && min=$py
                        [[ $pz -gt $max ]] && max=$pz
                        [[ $pz -lt $min ]] && min=$pz

                        local balance=$(echo "scale=3; $max/$min" | bc)

                        if (( $(echo "$balance < $best_balance" | bc -l) )); then
                            best_balance=$balance
                            best_px=$px
                            best_py=$py
                            best_pz=$pz
                        fi
                    fi
                fi
            done
        fi
    done

    if [[ $best_px -gt 0 ]]; then
        echo "$best_px $best_py $best_pz"
        return 0
    else
        echo "Warning: No valid decomposition found for $processes processes" >&2
        return 1
    fi
}

# Extract timing information from output file
extract_timing() {
    local output_file="$1"

    if [[ -f "$output_file" ]]; then
        # Get the last line and extract timing values
        local last_line=$(tail -n 1 "$output_file")
        local read_time=$(echo "$last_line" | cut -d',' -f1)
        local main_time=$(echo "$last_line" | cut -d',' -f2)
        local total_time=$(echo "$last_line" | cut -d',' -f3)

        if [[ -n "$read_time" && -n "$main_time" && -n "$total_time" ]]; then
            echo "$read_time $main_time $total_time"
            return 0
        fi
    fi

    echo "Error: Could not extract timing from $output_file" >&2
    return 1
}

# Calculate mean and standard deviation
calculate_statistics() {
    local values=("$@")
    local n=${#values[@]}

    if [[ $n -eq 0 ]]; then
        echo "0 0"  # Return 0 for both mean and stddev
        return
    fi

    local sum=0
    for val in "${values[@]}"; do
        sum=$(echo "$sum + $val" | bc -l)
    done

    local mean=$(echo "scale=6; $sum / $n" | bc -l)

    if [[ $n -eq 1 ]]; then
        echo "$mean 0"  # Return mean and 0 for stddev
        return
    fi

    local sum_squared_diff=0
    for val in "${values[@]}"; do
        local diff=$(echo "$val - $mean" | bc -l)
        local squared_diff=$(echo "$diff * $diff" | bc -l)
        sum_squared_diff=$(echo "$sum_squared_diff + $squared_diff" | bc -l)
    done

    local stddev=$(echo "scale=6; sqrt($sum_squared_diff / ($n - 1))" | bc -l)
    echo "$mean $stddev"
}

# Run a single benchmark instance
run_benchmark() {
    local executable="$1"
    local dataset="$2"
    local processes="$3"
    local px="$4"
    local py="$5"
    local pz="$6"
    local nx="$7"
    local ny="$8"
    local nz="$9"
    local timesteps="${10}"
    local iteration="${11}"

    local impl_name=$(basename "$executable")
    local dataset_name=$(basename "$dataset")
    local output_file="${RAW_DIR}/${impl_name}_${processes}p_${dataset_name}_${iteration}.txt"

    local cmd="mpirun -np $processes --oversubscribe $executable $dataset $px $py $pz $nx $ny $nz $timesteps $output_file"

    echo "Running: $cmd"

    local start_time=$(date +%s.%N)
    timeout $TIMEOUT bash -c "$cmd"
    local exit_code=$?
    local end_time=$(date +%s.%N)
    local elapsed=$(echo "$end_time - $start_time" | bc -l)

    if [[ $exit_code -eq 0 && -f "$output_file" ]]; then
        local timing=$(extract_timing "$output_file")
        if [[ $? -eq 0 ]]; then
            echo "$timing $elapsed"
            return 0
        fi
    elif [[ $exit_code -eq 124 ]]; then
        echo "Error: Command timed out after $TIMEOUT seconds" >&2
    else
        echo "Error: Command failed with exit code $exit_code" >&2
    fi

    return 1
}

# Append a result to the CSV file
append_result() {
    local results_file="$1"
    local impl="$2"
    local dataset="$3"
    local processes="$4"
    local px="$5"
    local py="$6"
    local pz="$7"
    local nx="$8"
    local ny="$9"
    local nz="${10}"
    local timesteps="${11}"
    local iteration="${12}"
    local read_time="${13}"
    local main_time="${14}"
    local total_time="${15}"
    local wall_time="${16}"

    local problem_size=$((nx * ny * nz * timesteps))

    echo "$impl,$dataset,$processes,$px,$py,$pz,$iteration,$nx,$ny,$nz,$timesteps,$problem_size,$read_time,$main_time,$total_time,$wall_time" >> "$results_file"
}

# Create summary from results
create_summary() {
    local results_file="$1"
    local summary_file="$2"

    echo "implementation,dataset,processes,px,py,pz,read_time_mean,read_time_std,main_time_mean,main_time_std,total_time_mean,total_time_std" > "$summary_file"

    # Get unique combinations of implementation, dataset, processes, px, py, pz
    local combinations=$(awk -F, 'NR>1 {print $1","$2","$3","$4","$5","$6}' "$results_file" | sort -u)

    while IFS= read -r combo; do
        IFS=',' read -r impl dataset processes px py pz <<< "$combo"

        # Extract read_time, main_time, total_time for this combination
        local read_times=($(awk -F, -v combo="$combo" 'NR>1 && $1","$2","$3","$4","$5","$6 == combo {print $13}' "$results_file"))
        local main_times=($(awk -F, -v combo="$combo" 'NR>1 && $1","$2","$3","$4","$5","$6 == combo {print $14}' "$results_file"))
        local total_times=($(awk -F, -v combo="$combo" 'NR>1 && $1","$2","$3","$4","$5","$6 == combo {print $15}' "$results_file"))

        # Calculate statistics
        read read_mean read_std <<< $(calculate_statistics "${read_times[@]}")
        read main_mean main_std <<< $(calculate_statistics "${main_times[@]}")
        read total_mean total_std <<< $(calculate_statistics "${total_times[@]}")

        # Append to summary
        echo "$impl,$dataset,$processes,$px,$py,$pz,$read_mean,$read_std,$main_mean,$main_std,$total_mean,$total_std" >> "$summary_file"
    done <<< "$combinations"
}

# ===================== MAIN SCRIPT =====================

# Create output directories
mkdir -p "$OUTPUT_DIR" "$RAW_DIR"

# Save configuration
save_configuration

# Print benchmark configuration
echo "=================================="
echo "Benchmark Configuration:"
echo "  Implementations: ${!IMPLEMENTATIONS[@]}"
echo "  Datasets: ${DATASETS[@]}"
echo "  Process counts: ${PROCESS_COUNTS[@]}"
echo "  Iterations: $ITERATIONS"
echo "  Output directory: $OUTPUT_DIR"
echo "=================================="

# Create results CSV file
RESULTS_FILE="${OUTPUT_DIR}/benchmark_results.csv"
echo "implementation,dataset,processes,px,py,pz,iteration,nx,ny,nz,timesteps,problem_size,read_time,main_time,total_time,wall_time" > "$RESULTS_FILE"

# Run all benchmarks
for dataset in "${DATASETS[@]}"; do
    if [[ ! -f "$dataset" ]]; then
        echo "Warning: Dataset $dataset not found, skipping"
        continue
    fi

    # Parse dimensions
    dims=$(parse_dimensions "$dataset")
    if [[ $? -ne 0 ]]; then
        continue
    fi

    read nx ny nz timesteps <<< "$dims"

    for processes in "${PROCESS_COUNTS[@]}"; do
        # Get decomposition
        decomp=$(get_decomposition "$processes" "$nx" "$ny" "$nz")
        if [[ $? -ne 0 ]]; then
            continue
        fi

        read px py pz <<< "$decomp"

        for impl_name in "${!IMPLEMENTATIONS[@]}"; do
            executable="${IMPLEMENTATIONS[$impl_name]}"

            echo -e "\n=================================================="
            echo "Benchmarking $impl_name with $processes processes on $(basename "$dataset")"
            echo "Decomposition: ${px}x${py}x${pz}"
            echo "=================================================="

            read_times=()
            main_times=()
            total_times=()

            for ((i=0; i<ITERATIONS; i++)); do
                echo "Iteration $((i+1))/$ITERATIONS"

                # Run the benchmark
                timing=$(run_benchmark "$executable" "$dataset" "$processes" "$px" "$py" "$pz" "$nx" "$ny" "$nz" "$timesteps" "$i")

                if [[ $? -eq 0 ]]; then
                    read read_time main_time total_time wall_time <<< "$timing"

                    # Save result
                    append_result "$RESULTS_FILE" "$impl_name" "$dataset" "$processes" "$px" "$py" "$pz" "$nx" "$ny" "$nz" "$timesteps" "$i" "$read_time" "$main_time" "$total_time" "$wall_time"

                    # Store for statistics
                    read_times+=("$read_time")
                    main_times+=("$main_time")
                    total_times+=("$total_time")
                fi

                # Short delay between iterations
                sleep 1
            done

            # Compute statistics for this configuration
            if [[ ${#read_times[@]} -gt 0 ]]; then
                read read_mean read_std <<< $(calculate_statistics "${read_times[@]}")
                read main_mean main_std <<< $(calculate_statistics "${main_times[@]}")
                read total_mean total_std <<< $(calculate_statistics "${total_times[@]}")

                echo -e "\nResults:"
                echo "Read Time:  ${read_mean}s (±${read_std})"
                echo "Main Time:  ${main_mean}s (±${main_std})"
                echo "Total Time: ${total_mean}s (±${total_std})"
            else
                echo "No valid results collected"
            fi
        done
    done
done

# Create summary
SUMMARY_FILE="${OUTPUT_DIR}/benchmark_summary.csv"
create_summary "$RESULTS_FILE" "$SUMMARY_FILE"

echo -e "\nResults saved to $RESULTS_FILE"
echo "Summary saved to $SUMMARY_FILE"
