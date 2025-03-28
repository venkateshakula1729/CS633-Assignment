# Time Series Data Parallel Processing Assignment

**Maximum Marks:** 100 **Due Date:** 10-04-2025

## Problem Description

Process time series data of a 3D volume (nx * ny * nz) with the following parallel computing objectives:

1. Count local minima for each time step
2. Count local maxima for each time step
3. Find global minimum for each time step
4. Find global maximum for each time step

### Key Constraints

- Perform 3D decomposition across spatial dimensions
- Optimize for performance
- X-axis: Horizontal
- Y-axis: Vertical
- Z-axis: Going into the screen

## Input File Specifications

### Data Organization

- Input file contains time series data (floats) for a 3D volume
- Number of rows = Total grid points (nx * ny * nz)
- Number of columns = Number of time steps

### Dimensional Limits

- Maximum total grid points: 1024³
- Maximum grid points in any dimension: 1024
- Maximum number of time steps: 1000
- Data organized in XYZ dimension order

#### Example Input Layout

For nx = 2, ny = 3, nz = 2, time steps = 2 Grid point order: (0,0,0), (1,0,0), (0,1,0), (1,1,0), (0,2,0), (1,2,0), (0,0,1), (1,0,1), (0,1,1), (1,1,1), (0,2,1), (1,2,1)

## Logical Code Sequence

1. **Process 0 (Rank 0)**
    
    - Read entire data using sequential I/O functions
    - Distribute data to all processes
    - Use selected data distribution strategy
2. **Process Distribution**
    
    - Assign sub-domains to processes
    - Each process finds local minima/maxima for its sub-volume
    - May require communication with neighboring processes

## Timing Requirements

1. **Time 1:** Initialization
2. **Time 2:** File read and data distribution
3. **Time 3:** Main code execution
4. **Time 4:** Finalization

**Timing Notes:**

- Separately time file read and data distribution (Time 2 - Time 1)
- Time main code from after file reading to before output
- Report maximum time across all processes

## Input Arguments (9 Total)

1. Dataset (Input file name .txt)
2. PX – Number of processes in X-dimension
3. PY – Number of processes in Y-dimension
4. PZ – Number of processes in Z-dimension
5. NX – Number of grid points in X-dimension
6. NY – Number of grid points in Y-dimension
7. NZ – Number of grid points in Z-dimension
8. NC – Number of columns (time steps)
9. Output file name (Recommended: output_NX_NY_NZ_NC.txt)

### Input Constraints

- PX ≥ 1, PY ≥ 1, PZ ≥ 1
- NX ≤ 1024, NY ≤ 1024, NZ ≤ 1024
- NC ≤ 1000
- Total Processes P = PX * PY * PZ

## Output File Format

The output file must contain exactly 3 lines:

1. **Line 1:** Local minima and maxima count
    
    - Format: `(local_minima_count, local_maxima_count), ...`
    - One pair per time step
2. **Line 2:** Global minimum and maximum values
    
    - Format: `(global_minimum, global_maximum), ...`
    - One pair per time step
3. **Line 3:** Timing information
    
    - Format: `Read Time, Main Code Time, Total Time`
    - Three double values

## Preliminary Execution Instructions

Run the following test cases twice each:

```bash
mpirun -np 8 -f hostfile ./executable data_64_64_64_3.txt 2 2 2 64 64 64 3 output_64_64_64_3.txt
mpirun -np 8 -f hostfile ./executable data_64_64_96_7.txt 2 2 2 64 64 96 7 output_64_64_96_7.txt
```

## Technical Requirements

- Language: C + MPI
- Source Code Filename: `src.c`
- Comprehensive code documentation
- Submission: `report.pdf`
    - Describe data distribution strategy
    - Explain parallelization approach
    - Include group details and group name

## Additional Notes

- Refer to Lecture 1 for plagiarism policy
- Detailed report format and submission guidelines will be provided separately
- LaTeX template will be available for the report