# Code Description

## 1. Overview of the Code

The program implements a parallel computing solution for processing time series data of 3D volumes using MPI. The workflow can be summarized as follows:

1. **Initialization**: Parse command line arguments and initialize MPI environment
2. **Domain Decomposition**: Each process calculates its own subdomain boundaries including ghost regions
3. **Data Loading**: Either parallel file reading or root process reads and distributes data
4. **Data Processing**: Each process analyzes its subdomain to identify:
   - Local minima and maxima at each time step
   - Global minimum and maximum at each time step
5. **Result Collection**: Reduction operations aggregate results from all processes
6. **Output Generation**: Root process writes results to the specified output file

The code prioritizes performance through optimized I/O strategies, efficient domain decomposition with ghost regions, and non-blocking communication patterns.

## 2. Detailed Description of Important Components

### Domain Decomposition Function

```c
void calculateSubDomainBoundaries(int rank, int pX, int pY, int pZ, int nX, int nY, int nZ, SubDomain* subdomain)
```

**Logic**:
This function calculates the portion of the 3D domain assigned to each process, including ghost regions for proper local minima/maxima detection. It first determines the process's position in the 3D process grid, then calculates its subdomain boundaries and adds ghost cells where needed (except at global domain boundaries).

**Arguments**:
- `rank`: Process ID in the MPI communicator
- `pX, pY, pZ`: Number of processes in each dimension
- `nX, nY, nZ`: Total grid size in each dimension
- `subdomain`: Output structure to store calculated boundaries

**Outputs**:
Populates the `subdomain` structure with:
- Regular domain boundaries (`startX/Y/Z`, `endX/Y/Z`, `width/height/depth`)
- Extended domain boundaries including ghost regions (`tempStartX/Y/Z`, `tempEndX/Y/Z`, `tempWidth/height/depth`)

### Optimized Parallel I/O

```c
float* readInputDataParallel_Level2(const char* inputFile, const SubDomain* subdomain,
                                   int nX, int nY, int nZ, int timeSteps)
```

**Logic**:
Implements efficient parallel I/O using MPI-IO with derived datatypes. Each process reads only its required portion of the file (including ghost regions) directly into its local memory. The function creates a custom MPI datatype that represents the 4D subarray (3D space + time) required by the process.

**Arguments**:
- `inputFile`: Path to the input data file
- `subdomain`: Process's subdomain information
- `nX, nY, nZ`: Global domain dimensions
- `timeSteps`: Number of time steps in the data

**Outputs**:
Returns a float array containing the process's portion of the data (including ghost cells) for all time steps.

### Data Distribution

```c
float* distributeData(int rank, const SubDomain* subdomain, float* globalData,
                     int nX, int nY, int nZ, int timeSteps, int pX, int pY, int pZ)
```

**Logic**:
Used for scenarios with many processes where parallel I/O might be inefficient. The root process reads the entire dataset and distributes appropriate portions to each process using non-blocking communication. This function employs `MPI_Isend` to overlap communication and uses separate buffers for each receiving process to avoid data corruption.

**Arguments**:
- `rank`: Process ID in the MPI communicator
- `subdomain`: Process's subdomain information
- `globalData`: Complete dataset (only used by rank 0)
- `nX, nY, nZ`: Global domain dimensions
- `timeSteps`: Number of time steps
- `pX, pY, pZ`: Process grid dimensions

**Outputs**:
Returns a float array containing the process's portion of the data for all time steps.

### Local Data Processing

```c
void processLocalData(float* localData, const SubDomain* subdomain, TimeSeriesResults* results, int timeSteps)
```

**Logic**:
Analyzes the local subdomain data to count local minima/maxima and find extreme values at each time step. For each point in the actual subdomain (excluding ghost cells), it checks if the point is a local minimum or maximum by comparing with its six neighbors. The function also tracks global minimum and maximum values.

**Arguments**:
- `localData`: Process's portion of the data including ghost regions
- `subdomain`: Process's subdomain information
- `results`: Structure to store computation results
- `timeSteps`: Number of time steps

**Outputs**:
Populates the `results` structure with counts of local minima/maxima and extreme values for each time step.

### Local Extrema Detection

```c
bool isLocalMinimum(float* data, int x, int y, int z, int width, int height, int depth, int time, int timeSteps)
bool isLocalMaximum(float* data, int x, int y, int z, int width, int height, int depth, int time, int timeSteps)
```

**Logic**:
These functions determine if a point is a local minimum or maximum by comparing its value with all six neighboring points (along x, y, and z axes). For a minimum, the point must be strictly smaller than all neighbors; for a maximum, strictly larger.

**Arguments**:
- `data`: Local data array
- `x, y, z`: Coordinates of the point to check
- `width, height, depth`: Dimensions of the local data volume
- `time`: Current time step being processed
- `timeSteps`: Total number of time steps

**Outputs**:
Returns `true` if the point is a local minimum/maximum, `false` otherwise.

### Results Aggregation and Output

```c
// Result reduction and output sections in main()
```

**Logic**:
After local processing, the code uses `MPI_Reduce` operations to aggregate results across all processes. For minima/maxima counts, it uses `MPI_SUM` to add up all local counts. For global minimum and maximum values, it uses `MPI_MIN` and `MPI_MAX` operations respectively. Finally, timing information is gathered using `MPI_MAX` to report the worst-case performance.

The root process (rank 0) then writes the consolidated results to the specified output file in the required format.

### Memory Management

The code employs a structured approach to memory management with dedicated allocation and deallocation functions for result data structures. It includes error checking to prevent memory leaks and uses temporary buffers for efficient communication.
