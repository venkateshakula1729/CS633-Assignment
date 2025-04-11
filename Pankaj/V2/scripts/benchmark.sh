#!/usr/bin/env bash

# Benchmarking script for time series parallel processing implementations (Bash version)

# ===================== CONFIGURATION =====================
# Implementations to benchmark
declare -A IMPLEMENTATIONS=(
    ["send"]="../src/bin/pankaj_code7"
    ["isend"]="../src/bin/pankaj_code11"
    ["ind_IO"]="../src/bin/independentIO"
    ["coll_IO"]="../src/bin/collectiveIO"
    ["ind_IO_der"]="../src/bin/independentIO_derData"
    ["coll_IO_der"]="../src/bin/collectiveIO_derData"
)

# Datasets
DATASETS=(
    # "../data/data_64_64_96_7.bin"
    "../data/art_data_256_256_256_7.bin"
)

# Process counts to test
PROCESS_COUNTS=(8)

# Number of iterations per configuration
ITERATIONS=5

# Output directory
OUTPUT_DIR="../results/benchmark_$(date +%Y%m%d_%H%M%S)"

# Timeout in seconds
TIMEOUT=600  # 10 minutes

# Process decompositions
declare -A PROCESS_DECOMPOSITIONS=(
    ["8"]="2 2 2"
    ["16"]="4 2 2"
    ["32"]="4 4 2"
    ["64"]="4 4 4"
)

# ===================== END CONFIGURATION =====================

# Initialize directories and files
RAW_DIR="${OUTPUT_DIR}/raw"
RESULTS_CSV="${OUTPUT_DIR}/benchmark_results.csv"
mkdir -p "${OUTPUT_DIR}" "${RAW_DIR}"

# Save configuration
echo "Saving configuration..."
cat > "${OUTPUT_DIR}/config.txt" <<EOF
Timestamp: $(date)
Implementations: ${!IMPLEMENTATIONS[@]}
Datasets: ${DATASETS[@]}
Process counts: ${PROCESS_COUNTS[@]}
Iterations: ${ITERATIONS}
Timeout: ${TIMEOUT}
EOF

# Function to parse dimensions from filename
parse_dimensions() {
    local dataset="$1"
    local filename=$(basename "$dataset")
    if [[ $filename =~ data_([0-9]+)_([0-9]+)_([0-9]+)_([0-9]+) ]]; then
        echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]} ${BASH_REMATCH[3]} ${BASH_REMATCH[4]}"
    else
        echo "Error parsing dimensions from $dataset" >&2
        return 1
    fi
}

# Function to get process decomposition
get_decomposition() {
    local processes=$1
    local nx=$2 ny=$3 nz=$4
    local decomposition=${PROCESS_DECOMPOSITIONS[$processes]}

    if [ -z "$decomposition" ]; then
        echo "No decomposition found for $processes processes" >&2
        return 1
    fi

    echo "$decomposition"
}

# Function to extract timing from output file
extract_timing() {
    local output_file="$1"
    if [ -f "$output_file" ]; then
        tail -n 1 "$output_file" | awk -F', ' '{
            printf "%.4f %.4f %.4f", $1, $2, $3
        }'
    else
        echo "Error: Output file not found" >&2
        return 1
    fi
}

# Create CSV header
echo "implementation,dataset,processes,px,py,pz,iteration,nx,ny,nz,timesteps,read_time,main_time,total_time,wall_time" > "$RESULTS_CSV"

# Main benchmarking loop
for dataset in "${DATASETS[@]}"; do
    if [ ! -f "$dataset" ]; then
        echo "Warning: Dataset $dataset not found, skipping"
        continue
    fi

    dims=($(parse_dimensions "$dataset")) || continue
    nx=${dims[0]}
    ny=${dims[1]}
    nz=${dims[2]}
    timesteps=${dims[3]}

    for processes in "${PROCESS_COUNTS[@]}"; do
        decomposition=($(get_decomposition "$processes" "$nx" "$ny" "$nz")) || continue
        px=${decomposition[0]}
        py=${decomposition[1]}
        pz=${decomposition[2]}

        for impl_name in "${!IMPLEMENTATIONS[@]}"; do
            executable="${IMPLEMENTATIONS[$impl_name]}"
            if [ ! -x "$executable" ]; then
                echo "Warning: Executable $executable not found, skipping"
                continue
            fi

            echo -e "\n$(printf '=%.0s' {1..70})"
            echo "Benchmarking $impl_name with $processes processes on $(basename "$dataset")"
            echo "Decomposition: ${px}x${py}x${pz}"
            echo "$(printf '=%.0s' {1..70})"

            for ((i=0; i<ITERATIONS; i++)); do
                echo "Iteration $((i+1))/${ITERATIONS}"
                output_file="${RAW_DIR}/${impl_name}_${processes}p_$(basename "$dataset")_${i}.txt"
                error_log="${RAW_DIR}/${impl_name}_${processes}p_error_${i}.log"

                # Build command
                cmd=(
                    timeout --kill-after=10 $TIMEOUT
                    mpirun -np "$processes" --oversubscribe
                    "$executable" "$dataset"
                    "$px" "$py" "$pz"
                    "$nx" "$ny" "$nz" "$timesteps"
                    "$output_file"
                )

                # echo "Running: ${cmd[@]}"
                start_time=$(date +%s.%N)
                "${cmd[@]}" 2>"$error_log"
                exit_code=$?
                wall_time=$(echo "$(date +%s.%N) - $start_time" | bc)

                # Process results
                if [ $exit_code -eq 0 ]; then
                    timings=($(extract_timing "$output_file")) && {
                        read_time=${timings[0]}
                        main_time=${timings[1]}
                        total_time=${timings[2]}

                        # Append to CSV
                        echo "$impl_name,$dataset,$processes,$px,$py,$pz,$i,$nx,$ny,$nz,$timesteps,$read_time,$main_time,$total_time,$wall_time" >> "$RESULTS_CSV"
                    }
                else
                    echo "Error: Run failed with exit code $exit_code (see $error_log)"
                fi

                sleep 1  # Short delay between iterations
            done
        done
    done
done

echo -e "\nBenchmark completed. Results saved to: $RESULTS_CSV"
