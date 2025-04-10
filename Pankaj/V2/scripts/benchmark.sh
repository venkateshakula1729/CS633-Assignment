#!/bin/bash

# ===================================================================
# Comprehensive benchmarking script for time series parallel processing
# This script replicates the full functionality of benchmark.py
# ===================================================================

set -e  # Exit on error

# ===================== CONFIGURATION =====================
# Base directory for relative paths
BASE_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Implementations to benchmark
declare -a IMPLEMENTATIONS=(
  "pankaj_code7:$BASE_DIR/src/bin/pankaj_code7"
  "pankaj_code9:$BASE_DIR/src/bin/pankaj_code9"
  "pankaj_code11:$BASE_DIR/src/bin/pankaj_code11"
)

# Datasets
declare -a DATASETS=(
  "$BASE_DIR/data/data_64_64_64_3.txt"
  "$BASE_DIR/data/data_64_64_96_7.txt"
)

# Process counts to test
declare -a PROCESS_COUNTS=(8 16 32 64)

# Number of iterations per configuration
ITERATIONS=3

# Output directory
OUTPUT_DIR="$BASE_DIR/results/benchmark_$(date +%Y%m%d_%H%M%S)"

# Timeout in seconds
TIMEOUT=600  # 10 minutes

# Specific process decompositions for each process count
# Format is:  process_count:"px,py,pz|px,py,pz|..."
declare -A PROCESS_DECOMPOSITIONS=(
  [8]="2,2,2"
  [16]="4,2,2"
  [32]="4,4,2"
  [64]="4,4,4"
)

# Generate visualizations after benchmarking
GENERATE_VISUALIZATIONS=true

# ===================== UTILITY FUNCTIONS =====================

# Function to show timestamp at the beginning of messages
timestamp() {
  date +"%Y-%m-%d %H:%M:%S"
}

# Function to print colored output
print_color() {
  local color=$1
  local message=$2

  case $color in
    "red")    echo -e "\033[0;31m$message\033[0m" ;;
    "green")  echo -e "\033[0;32m$message\033[0m" ;;
    "yellow") echo -e "\033[0;33m$message\033[0m" ;;
    "blue")   echo -e "\033[0;34m$message\033[0m" ;;
    "magenta") echo -e "\033[0;35m$message\033[0m" ;;
    "cyan")   echo -e "\033[0;36m$message\033[0m" ;;
    *)        echo "$message" ;;
  esac
}

# Function to log messages with timestamp
log() {
  local level=$1
  local message=$2
  local color="normal"

  case $level in
    "INFO")    color="green" ;;
    "WARNING") color="yellow" ;;
    "ERROR")   color="red" ;;
    "DEBUG")   color="blue" ;;
  esac

  print_color "$color" "[$(timestamp)] [$level] $message"
}

# Function to handle errors
handle_error() {
  log "ERROR" "$1"
  exit 1
}

# Function to calculate mean of an array of values
calc_mean() {
  local values=("$@")
  local n=${#values[@]}

  if [ $n -eq 0 ]; then
    echo "0"
    return
  fi

  local sum=0
  for v in "${values[@]}"; do
    sum=$(echo "$sum + $v" | bc -l)
  done

  echo $(echo "scale=6; $sum / $n" | bc -l)
}

# Function to calculate standard deviation of an array of values
calc_std() {
  local values=("$@")
  local n=${#values[@]}

  if [ $n -lt 2 ]; then
    echo "0"
    return
  fi

  local mean=$(calc_mean "${values[@]}")
  local sum_squares=0

  for v in "${values[@]}"; do
    local diff=$(echo "$v - $mean" | bc -l)
    sum_squares=$(echo "$sum_squares + ($diff * $diff)" | bc -l)
  done

  echo $(echo "scale=6; sqrt($sum_squares / ($n - 1))" | bc -l)
}

# Function to calculate min of an array of values
calc_min() {
  local values=("$@")
  local n=${#values[@]}

  if [ $n -eq 0 ]; then
    echo "0"
    return
  fi

  local min=${values[0]}
  for v in "${values[@]:1}"; do
    if (( $(echo "$v < $min" | bc -l) )); then
      min=$v
    fi
  done

  echo $min
}

# Function to calculate max of an array of values
calc_max() {
  local values=("$@")
  local n=${#values[@]}

  if [ $n -eq 0 ]; then
    echo "0"
    return
  fi

  local max=${values[0]}
  for v in "${values[@]:1}"; do
    if (( $(echo "$v > $max" | bc -l) )); then
      max=$v
    fi
  done

  echo $max
}

# Function to parse dataset dimensions from filename
parse_dimensions() {
  local dataset="$1"
  local basename=$(basename "$dataset")

  if [[ $basename =~ data_([0-9]+)_([0-9]+)_([0-9]+)_([0-9]+) ]]; then
    echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]} ${BASH_REMATCH[3]} ${BASH_REMATCH[4]}"
  else
    log "ERROR" "Could not parse dimensions from $basename"
    return 1
  fi
}

# Function to extract timing information from output file
extract_timing() {
  local output_file="$1"
  if [ -f "$output_file" ]; then
    # Extract the last line of the file which contains timing information
    local timing_line=$(tail -n 1 "$output_file")
    echo "$timing_line"
  else
    log "ERROR" "Output file $output_file not found"
    return 1
  fi
}

# Function to get the most balanced decomposition
get_balanced_decomposition() {
  local processes=$1
  local nx=$2
  local ny=$3
  local nz=$4

  local best_px=1
  local best_py=1
  local best_pz=$processes
  local best_balance=999999

  # Try all possible decompositions
  for px in $(seq 1 $processes); do
    if [ $((processes % px)) -ne 0 ]; then
      continue
    fi

    local remaining=$((processes / px))

    for py in $(seq 1 $remaining); do
      if [ $((remaining % py)) -ne 0 ]; then
        continue
      fi

      local pz=$((remaining / py))

      # Check if dimensions are divisible
      if [ $((nx % px)) -ne 0 ] || [ $((ny % py)) -ne 0 ] || [ $((nz % pz)) -ne 0 ]; then
        continue
      fi

      # Calculate balance factor (ratio of max to min)
      local max_p=$(echo -e "$px\n$py\n$pz" | sort -nr | head -n1)
      local min_p=$(echo -e "$px\n$py\n$pz" | sort -n | head -n1)
      local balance=$(echo "scale=6; $max_p / $min_p" | bc -l)

      # If this decomposition is more balanced, update the best
      if (( $(echo "$balance < $best_balance" | bc -l) )); then
        best_px=$px
        best_py=$py
        best_pz=$pz
        best_balance=$balance
      fi
    done
  done

  # If we didn't find a valid decomposition
  if (( $(echo "$best_balance > 999" | bc -l) )); then
    return 1
  fi

  echo "$best_px $best_py $best_pz"
}

# Function to get decomposition for a process count
get_decomposition() {
  local processes=$1
  local nx=$2
  local ny=$3
  local nz=$4

  # Check if we have a specific decomposition for this process count
  if [ -n "${PROCESS_DECOMPOSITIONS[$processes]}" ]; then
    IFS=',' read px py pz <<< "${PROCESS_DECOMPOSITIONS[$processes]}"

    # Check if dimensions are divisible by the decomposition
    if [ $((nx % px)) -eq 0 ] && [ $((ny % py)) -eq 0 ] && [ $((nz % pz)) -eq 0 ] && [ $((px * py * pz)) -eq $processes ]; then
      echo "$px $py $pz"
      return 0
    else
      log "WARNING" "Specified decomposition $px,$py,$pz is invalid for dimensions $nx,$ny,$nz and processes $processes"
    fi
  fi

  # Try to find a balanced decomposition
  local balanced=$(get_balanced_decomposition "$processes" "$nx" "$ny" "$nz")
  if [ $? -eq 0 ]; then
    echo "$balanced"
    return 0
  fi

  log "WARNING" "No valid decomposition found for $processes processes with dimensions $nx,$ny,$nz"
  return 1
}

# Function to create CSV header
create_csv_header() {
  echo "implementation,dataset,processes,px,py,pz,iteration,nx,ny,nz,timesteps,problem_size,read_time,main_time,total_time,wall_time"
}

# ===================== MAIN SCRIPT =====================

# Create output directories
mkdir -p "$OUTPUT_DIR/raw"

# Create results CSV file with header
RESULTS_CSV="$OUTPUT_DIR/benchmark_results.csv"
create_csv_header > "$RESULTS_CSV"

# Print banner
echo "========================================================================"
print_color "cyan" "                  TIME SERIES BENCHMARK SCRIPT               "
echo "========================================================================"
log "INFO" "Starting benchmark with the following configuration:"
log "INFO" "  Implementations: $(for i in "${IMPLEMENTATIONS[@]}"; do echo -n "$(echo $i | cut -d':' -f1) "; done)"
log "INFO" "  Datasets: $(for d in "${DATASETS[@]}"; do echo -n "$(basename "$d") "; done)"
log "INFO" "  Process counts: ${PROCESS_COUNTS[*]}"
log "INFO" "  Iterations: $ITERATIONS"
log "INFO" "  Output directory: $OUTPUT_DIR"
echo "========================================================================"

# Save configuration to JSON file
log "INFO" "Saving configuration to $OUTPUT_DIR/config.json"
cat > "$OUTPUT_DIR/config.json" << EOL
{
  "timestamp": "$(date +%Y%m%d_%H%M%S)",
  "implementations": [$(for i in "${IMPLEMENTATIONS[@]}"; do echo -n "\"$(echo $i | cut -d':' -f1)\","; done | sed 's/,$//')]
}
EOL

# Check if all executables exist
for impl in "${IMPLEMENTATIONS[@]}"; do
  impl_name=$(echo $impl | cut -d':' -f1)
  executable=$(echo $impl | cut -d':' -f2)

  if [ ! -f "$executable" ]; then
    log "WARNING" "Executable for $impl_name not found at $executable"
  fi
done

# Check if all datasets exist
for dataset in "${DATASETS[@]}"; do
  if [ ! -f "$dataset" ]; then
    log "WARNING" "Dataset not found: $dataset"
  fi
done

# Run benchmarks for each combination
for dataset in "${DATASETS[@]}"; do
  if [ ! -f "$dataset" ]; then
    log "WARNING" "Dataset $dataset not found, skipping"
    continue
  fi

  dataset_name=$(basename "$dataset")
  log "INFO" "Processing dataset: $dataset_name"

  # Parse dimensions
  dims=$(parse_dimensions "$dataset")
  if [ $? -ne 0 ]; then
    log "WARNING" "Could not parse dimensions for $dataset_name, skipping"
    continue
  fi

  read nx ny nz timesteps <<< "$dims"
  problem_size=$((nx * ny * nz * timesteps))
  log "DEBUG" "Dataset dimensions: ${nx}x${ny}x${nz}, $timesteps timesteps, problem size: $problem_size"

  for processes in "${PROCESS_COUNTS[@]}"; do
    log "INFO" "Testing with $processes processes"

    # Get decomposition for this process count
    decomposition=$(get_decomposition "$processes" "$nx" "$ny" "$nz")
    if [ $? -ne 0 ]; then
      log "WARNING" "Skipping $processes processes for $dataset_name"
      continue
    fi

    read px py pz <<< "$decomposition"
    log "DEBUG" "Using decomposition: ${px}x${py}x${pz}"

    for impl in "${IMPLEMENTATIONS[@]}"; do
      impl_name=$(echo $impl | cut -d':' -f1)
      executable=$(echo $impl | cut -d':' -f2)

      if [ ! -f "$executable" ]; then
        log "WARNING" "Executable for $impl_name not found, skipping"
        continue
      fi

      log "INFO" "Benchmarking implementation: $impl_name"

      print_color "magenta" "================================================================="
      echo "Benchmarking: $impl_name"
      echo "  Processes: $processes (${px}x${py}x${pz})"
      echo "  Dataset: $dataset_name (${nx}x${ny}x${nz}, $timesteps timesteps)"
      print_color "magenta" "================================================================="

      # Arrays to store results
      declare -a read_times=()
      declare -a main_times=()
      declare -a total_times=()
      declare -a wall_times=()

      # Run multiple iterations
      for i in $(seq 0 $((ITERATIONS-1))); do
        log "INFO" "Running iteration $((i+1))/$ITERATIONS"

        # Prepare output file
        output_file="$OUTPUT_DIR/raw/${impl_name}_${processes}p_${dataset_name}_${i}.txt"

        # Run the command
        cmd="mpirun -np $processes --oversubscribe $executable $dataset $px $py $pz $nx $ny $nz $timesteps $output_file"
        log "DEBUG" "Command: $cmd"

        # Measure wall time
        start_time=$(date +%s.%N)

        # Run with timeout
        timeout $TIMEOUT $cmd
        exit_code=$?

        end_time=$(date +%s.%N)
        wall_time=$(echo "$end_time - $start_time" | bc -l)

        if [ $exit_code -eq 0 ] && [ -f "$output_file" ]; then
          # Extract timing
          timing=$(extract_timing "$output_file")
          if [ $? -ne 0 ]; then
            log "WARNING" "Could not extract timing from output file"
            continue
          fi

          read_time=$(echo "$timing" | cut -d',' -f1)
          main_time=$(echo "$timing" | cut -d',' -f2)
          total_time=$(echo "$timing" | cut -d',' -f3)

          # Store results
          read_times+=("$read_time")
          main_times+=("$main_time")
          total_times+=("$total_time")
          wall_times+=("$wall_time")

          # Add to CSV
          echo "$impl_name,$dataset,$processes,$px,$py,$pz,$i,$nx,$ny,$nz,$timesteps,$problem_size,$read_time,$main_time,$total_time,$wall_time" >> "$RESULTS_CSV"

          log "INFO" "Iteration $((i+1)) completed: read=$read_time, main=$main_time, total=$total_time, wall=$wall_time"
        else
          if [ $exit_code -eq 124 ]; then
            log "ERROR" "Command timed out after $TIMEOUT seconds"
          else
            log "ERROR" "Command failed with exit code $exit_code"
          fi

          # If the output file exists despite error, try to extract timing
          if [ -f "$output_file" ]; then
            timing=$(extract_timing "$output_file" 2>/dev/null || echo "0,0,0")
            read_time=$(echo "$timing" | cut -d',' -f1)
            main_time=$(echo "$timing" | cut -d',' -f2)
            total_time=$(echo "$timing" | cut -d',' -f3)

            # Add to CSV with note that it failed
            echo "$impl_name,$dataset,$processes,$px,$py,$pz,$i,$nx,$ny,$nz,$timesteps,$problem_size,$read_time,$main_time,$total_time,$wall_time,failed" >> "$RESULTS_CSV"
          fi
        fi

        # Short delay between iterations
        sleep 1
      done

      # Calculate statistics if we have any results
      n_results=${#total_times[@]}
      if [ $n_results -gt 0 ]; then
        # Calculate statistics
        mean_read=$(calc_mean "${read_times[@]}")
        mean_main=$(calc_mean "${main_times[@]}")
        mean_total=$(calc_mean "${total_times[@]}")
        mean_wall=$(calc_mean "${wall_times[@]}")

        std_read=$(calc_std "${read_times[@]}")
        std_main=$(calc_std "${main_times[@]}")
        std_total=$(calc_std "${total_times[@]}")
        std_wall=$(calc_std "${wall_times[@]}")

        min_total=$(calc_min "${total_times[@]}")
        max_total=$(calc_max "${total_times[@]}")

        # Display statistics
        echo -e "\nStatistics for $impl_name with $processes processes on $dataset_name:"
        echo "  Valid iterations: $n_results/$ITERATIONS"
        echo "  Read time:  $mean_read s (±$std_read)"
        echo "  Main time:  $mean_main s (±$std_main)"
        echo "  Total time: $mean_total s (±$std_total) [min=$min_total, max=$max_total]"
        echo "  Wall time:  $mean_wall s (±$std_wall)"

        # Also save to a separate statistics file
        stats_file="$OUTPUT_DIR/raw/${impl_name}_${processes}p_${dataset_name}_stats.txt"
        echo "Implementation: $impl_name" > "$stats_file"
        echo "Dataset: $dataset_name" >> "$stats_file"
        echo "Processes: $processes (${px}x${py}x${pz})" >> "$stats_file"
        echo "Iterations: $n_results/$ITERATIONS" >> "$stats_file"
        echo "Read time: $mean_read s (±$std_read)" >> "$stats_file"
        echo "Main time: $mean_main s (±$std_main)" >> "$stats_file"
        echo "Total time: $mean_total s (±$std_total) [min=$min_total, max=$max_total]" >> "$stats_file"
        echo "Wall time: $mean_wall s (±$std_wall)" >> "$stats_file"
      else
        log "WARNING" "No valid results collected for $impl_name with $processes processes on $dataset_name"
      fi
    done
  done
done

# Generate summary file with statistics
log "INFO" "Generating summary statistics"

# Group the data and compute statistics using awk
awk -F, 'BEGIN {OFS=",";
    print "implementation,dataset,processes,px,py,pz,nx,ny,nz,timesteps,read_time_mean,read_time_std,read_time_min,read_time_max,main_time_mean,main_time_std,main_time_min,main_time_max,total_time_mean,total_time_std,total_time_min,total_time_max";
}
NR>1 {
    # Extract fields (exclude iteration)
    key = $1","$2","$3","$4","$5","$6","$8","$9","$10","$11;

    # Save dimensional values
    dims[key]["nx"] = $8;
    dims[key]["ny"] = $9;
    dims[key]["nz"] = $10;
    dims[key]["timesteps"] = $11;

    # Aggregate data for statistics
    stats[key]["read_sum"] += $13;
    stats[key]["read_sum_sq"] += ($13 * $13);
    stats[key]["read_min"] = (key in stats && "read_min" in stats[key]) ? (stats[key]["read_min"] < $13 ? stats[key]["read_min"] : $13) : $13;
    stats[key]["read_max"] = (key in stats && "read_max" in stats[key]) ? (stats[key]["read_max"] > $13 ? stats[key]["read_max"] : $13) : $13;

    stats[key]["main_sum"] += $14;
    stats[key]["main_sum_sq"] += ($14 * $14);
    stats[key]["main_min"] = (key in stats && "main_min" in stats[key]) ? (stats[key]["main_min"] < $14 ? stats[key]["main_min"] : $14) : $14;
    stats[key]["main_max"] = (key in stats && "main_max" in stats[key]) ? (stats[key]["main_max"] > $14 ? stats[key]["main_max"] : $14) : $14;

    stats[key]["total_sum"] += $15;
    stats[key]["total_sum_sq"] += ($15 * $15);
    stats[key]["total_min"] = (key in stats && "total_min" in stats[key]) ? (stats[key]["total_min"] < $15 ? stats[key]["total_min"] : $15) : $15;
    stats[key]["total_max"] = (key in stats && "total_max" in stats[key]) ? (stats[key]["total_max"] > $15 ? stats[key]["total_max"] : $15) : $15;

    stats[key]["count"]++;
}
END {
    for (key in stats) {
        split(key, k, ",");

        # Calculate means
        read_mean = stats[key]["read_sum"] / stats[key]["count"];
        main_mean = stats[key]["main_sum"] / stats[key]["count"];
        total_mean = stats[key]["total_sum"] / stats[key]["count"];

        # Calculate standard deviations
        read_std = sqrt((stats[key]["read_sum_sq"] / stats[key]["count"]) - (read_mean * read_mean));
        main_std = sqrt((stats[key]["main_sum_sq"] / stats[key]["count"]) - (main_mean * main_mean));
        total_std = sqrt((stats[key]["total_sum_sq"] / stats[key]["count"]) - (total_mean * total_mean));

        # Output statistics
        print k[1], k[2], k[3], k[4], k[5], k[6], dims[key]["nx"], dims[key]["ny"], dims[key]["nz"], dims[key]["timesteps"],
              read_mean, read_std, stats[key]["read_min"], stats[key]["read_max"],
              main_mean, main_std, stats[key]["main_min"], stats[key]["main_max"],
              total_mean, total_std, stats[key]["total_min"], stats[key]["total_max"];
    }
}' "$RESULTS_CSV" > "$OUTPUT_DIR/benchmark_summary.csv"

# Generate visualizations if requested
if [ "$GENERATE_VISUALIZATIONS" = true ]; then
  log "INFO" "Generating visualizations..."
  if command -v python &> /dev/null; then
    python "$BASE_DIR/scripts/visualize.py" "$OUTPUT_DIR" --output "$OUTPUT_DIR/figures"
    if [ $? -eq 0 ]; then
      log "INFO" "Visualizations saved to $OUTPUT_DIR/figures"
    else
      log "ERROR" "Failed to generate visualizations"
    fi
  else
    log "WARNING" "Python not found, skipping visualization generation"
  fi
fi

log "INFO" "All benchmarks completed successfully"
log "INFO" "Results saved to $OUTPUT_DIR"
