#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <stdbool.h>
#include "mpi.h"

// Structure to hold timing information
typedef struct {
    double readTime;
    double mainCodeTime;
    double totalTime;
} TimingInfo;

// Structure to hold global analysis results
typedef struct {
    int* minimaCount;
    int* maximaCount;
    double* minValues;
    double* maxValues;
} TimeSeriesResults;

// Structure to hold domain decomposition information
typedef struct {
    int startX, startY, startZ;
    int endX, endY, endZ;
    int width, height, depth;
    int tempStartX, tempStartY, tempStartZ;
    int tempEndX, tempEndY, tempEndZ;
    int tempWidth, tempHeight, tempDepth;
} SubDomain;

// Convert 3D coordinates to 1D array index
int getLinearIndex(int x, int y, int z, int width, int height, int depth) {
    return ((z * height + y) * width + x);
}

// Allocate memory for results structure
TimeSeriesResults* allocateResults(int timeSteps) {
    TimeSeriesResults* results = (TimeSeriesResults*)malloc(sizeof(TimeSeriesResults));
    if (!results) return NULL;

    results->minimaCount = (int*)calloc(timeSteps, sizeof(int));
    results->maximaCount = (int*)calloc(timeSteps, sizeof(int));
    results->minValues = (double*)malloc(timeSteps * sizeof(double));
    results->maxValues = (double*)malloc(timeSteps * sizeof(double));

    // Initialize min/max values
    for (int t = 0; t < timeSteps; t++) {
        results->minValues[t] = DBL_MAX;
        results->maxValues[t] = -DBL_MAX;
    }

    return results;
}

// Free memory for results structure
void freeResults(TimeSeriesResults* results) {
    if (!results) return;

    free(results->minimaCount);
    free(results->maximaCount);
    free(results->minValues);
    free(results->maxValues);
    free(results);
}

// Check if a point is a local minimum
bool isLocalMinimum(float* data, int x, int y, int z, int width, int height, int depth, int time, int timeSteps) {
    int idx = getLinearIndex(x, y, z, width, height, depth);
    float value = data[idx * timeSteps + time];

    // Check all six neighbors (if they exist)
    // X-axis neighbors
    if (x > 0 && data[getLinearIndex(x-1, y, z, width, height, depth) * timeSteps + time] < value)
        return false;
    if (x < width-1 && data[getLinearIndex(x+1, y, z, width, height, depth) * timeSteps + time] < value)
        return false;

    // Y-axis neighbors
    if (y > 0 && data[getLinearIndex(x, y-1, z, width, height, depth) * timeSteps + time] < value)
        return false;
    if (y < height-1 && data[getLinearIndex(x, y+1, z, width, height, depth) * timeSteps + time] < value)
        return false;

    // Z-axis neighbors
    if (z > 0 && data[getLinearIndex(x, y, z-1, width, height, depth) * timeSteps + time] < value)
        return false;
    if (z < depth-1 && data[getLinearIndex(x, y, z+1, width, height, depth) * timeSteps + time] < value)
        return false;

    return true;
}

// Check if a point is a local maximum
bool isLocalMaximum(float* data, int x, int y, int z, int width, int height, int depth, int time, int timeSteps) {
    int idx = getLinearIndex(x, y, z, width, height, depth);
    float value = data[idx * timeSteps + time];

    // Check all six neighbors (if they exist)
    // X-axis neighbors
    if (x > 0 && data[getLinearIndex(x-1, y, z, width, height, depth) * timeSteps + time] > value)
        return false;
    if (x < width-1 && data[getLinearIndex(x+1, y, z, width, height, depth) * timeSteps + time] > value)
        return false;

    // Y-axis neighbors
    if (y > 0 && data[getLinearIndex(x, y-1, z, width, height, depth) * timeSteps + time] > value)
        return false;
    if (y < height-1 && data[getLinearIndex(x, y+1, z, width, height, depth) * timeSteps + time] > value)
        return false;

    // Z-axis neighbors
    if (z > 0 && data[getLinearIndex(x, y, z-1, width, height, depth) * timeSteps + time] > value)
        return false;
    if (z < depth-1 && data[getLinearIndex(x, y, z+1, width, height, depth) * timeSteps + time] > value)
        return false;

    return true;
}

// Calculate subdomain boundaries including ghost zones
void calculateSubDomainBoundaries(int rank, int pX, int pY, int pZ, int nX, int nY, int nZ, SubDomain* subdomain) {
    // Calculate process position in the 3D process grid
    int posZ = rank / (pX * pY);
    int posY = (rank % (pX * pY)) / pX;
    int posX = rank % pX;

    // Calculate subdomain size
    int subSizeX = nX / pX;
    int subSizeY = nY / pY;
    int subSizeZ = nZ / pZ;

    // Calculate boundaries of actual subdomain (without ghost zones)
    subdomain->startX = posX * subSizeX;
    subdomain->startY = posY * subSizeY;
    subdomain->startZ = posZ * subSizeZ;

    // Handle edge processes that might get slightly larger domains due to division remainder
    subdomain->endX = (posX == pX - 1) ? nX - 1 : subdomain->startX + subSizeX - 1;
    subdomain->endY = (posY == pY - 1) ? nY - 1 : subdomain->startY + subSizeY - 1;
    subdomain->endZ = (posZ == pZ - 1) ? nZ - 1 : subdomain->startZ + subSizeZ - 1;

    // Calculate boundaries including ghost zones
    subdomain->tempStartX = (subdomain->startX > 0) ? subdomain->startX - 1 : subdomain->startX;
    subdomain->tempStartY = (subdomain->startY > 0) ? subdomain->startY - 1 : subdomain->startY;
    subdomain->tempStartZ = (subdomain->startZ > 0) ? subdomain->startZ - 1 : subdomain->startZ;

    subdomain->tempEndX = (subdomain->endX < nX - 1) ? subdomain->endX + 1 : subdomain->endX;
    subdomain->tempEndY = (subdomain->endY < nY - 1) ? subdomain->endY + 1 : subdomain->endY;
    subdomain->tempEndZ = (subdomain->endZ < nZ - 1) ? subdomain->endZ + 1 : subdomain->endZ;

    // Calculate dimensions
    subdomain->width = subdomain->endX - subdomain->startX + 1;
    subdomain->height = subdomain->endY - subdomain->startY + 1;
    subdomain->depth = subdomain->endZ - subdomain->startZ + 1;

    subdomain->tempWidth = subdomain->tempEndX - subdomain->tempStartX + 1;
    subdomain->tempHeight = subdomain->tempEndY - subdomain->tempStartY + 1;
    subdomain->tempDepth = subdomain->tempEndZ - subdomain->tempStartZ + 1;
}

// Level-3 Parallel I/O: Collective I/O + derived datatype
float* readInputDataParallel_Level3(const char* inputFile, const SubDomain* subdomain,
                                   int nX, int nY, int nZ, int timeSteps) {
    // Allocate memory for local data (including ghost regions)
    int localDataSize = subdomain->tempWidth * subdomain->tempHeight *
                        subdomain->tempDepth * timeSteps;

    float* localData = (float*)malloc(localDataSize * sizeof(float));
    if (!localData) {
        printf("Failed to allocate memory for local data\n");
        return NULL;
    }

    // Set up MPI-IO hints for collective operations
    MPI_Info info;
    MPI_Info_create(&info);
    // Set buffer size for collective buffering
    MPI_Info_set(info, "cb_buffer_size", "16777216");
    // Specify collective buffering nodes
    MPI_Info_set(info, "cb_nodes", "8");
    // Allow MPI to use large contiguous regions
    MPI_Info_set(info, "romio_cb_read", "enable");
    // Enable data sieving for better performance with non-contiguous access
    MPI_Info_set(info, "romio_ds_read", "enable");
    // Specify alignment restrictions (system dependent)
    MPI_Info_set(info, "striping_unit", "4194304");

    // Open the binary file with collective access
    MPI_File fh;
    MPI_Status status;
    int ret = MPI_File_open(MPI_COMM_WORLD, inputFile, MPI_MODE_RDONLY, info, &fh);

    if (ret != MPI_SUCCESS) {
        char error_string[MPI_MAX_ERROR_STRING];
        int length_of_error_string;
        MPI_Error_string(ret, error_string, &length_of_error_string);
        printf("Error opening file: %s\n", error_string);
        free(localData);
        MPI_Info_free(&info);
        return NULL;
    }

    // Define the dimensions for the file view
    // Each grid point in the file has timeSteps consecutive floats

    // 1. Define the shape of the global array (including time dimension)
    int globalSizes[4] = {nZ, nY, nX, timeSteps};

    // 2. Define the shape of our local subarray
    int subSizes[4] = {subdomain->tempDepth, subdomain->tempHeight, subdomain->tempWidth, timeSteps};

    // 3. Define the starting coordinates of our subarray in the global array
    int starts[4] = {subdomain->tempStartZ, subdomain->tempStartY, subdomain->tempStartX, 0};

    // Create a derived datatype for our 4D subarray (3D + time)
    MPI_Datatype filetype;
    MPI_Type_create_subarray(4, globalSizes, subSizes, starts,
                            MPI_ORDER_C, MPI_FLOAT, &filetype);
    MPI_Type_commit(&filetype);

    // Set the file view using this datatype and our optimization hints
    MPI_File_set_view(fh, 0, MPI_FLOAT, filetype, "native", info);

    // Read the entire subdomain in a single COLLECTIVE operation
    // All processes must participate in this call
    MPI_File_read_all(fh, localData, localDataSize, MPI_FLOAT, &status);

    // Verify the read was successful
    int count;
    MPI_Get_count(&status, MPI_FLOAT, &count);
    if (count != localDataSize) {
        printf("Error: Read %d elements, expected %d\n", count, localDataSize);
    }

    // Clean up
    MPI_Type_free(&filetype);
    MPI_Info_free(&info);
    MPI_File_close(&fh);

    return localData;
}

// Process local data to find minima, maxima, and extreme values
void processLocalData(float* localData, const SubDomain* subdomain, TimeSeriesResults* results, int timeSteps) {
    for (int t = 0; t < timeSteps; t++) {
        int minimaCount = 0;
        int maximaCount = 0;

        for (int z = 0; z < subdomain->tempDepth; z++) {
            for (int y = 0; y < subdomain->tempHeight; y++) {
                for (int x = 0; x < subdomain->tempWidth; x++) {
                    // Check if this point is within the actual subdomain (not ghost zone)
                    bool isActualPoint = (x >= (subdomain->startX - subdomain->tempStartX) &&
                                         x <= (subdomain->endX - subdomain->tempStartX) &&
                                         y >= (subdomain->startY - subdomain->tempStartY) &&
                                         y <= (subdomain->endY - subdomain->tempStartY) &&
                                         z >= (subdomain->startZ - subdomain->tempStartZ) &&
                                         z <= (subdomain->endZ - subdomain->tempStartZ));

                    if (isActualPoint) {
                        int idx = getLinearIndex(x, y, z, subdomain->tempWidth, subdomain->tempHeight, subdomain->tempDepth);
                        float value = localData[idx * timeSteps + t];

                        // Update min/max values
                        if (value < results->minValues[t]) results->minValues[t] = value;
                        if (value > results->maxValues[t]) results->maxValues[t] = value;

                        // Check for local minima/maxima
                        if (isLocalMinimum(localData, x, y, z, subdomain->tempWidth, subdomain->tempHeight,
                                          subdomain->tempDepth, t, timeSteps)) {
                            minimaCount++;
                        }

                        if (isLocalMaximum(localData, x, y, z, subdomain->tempWidth, subdomain->tempHeight,
                                          subdomain->tempDepth, t, timeSteps)) {
                            maximaCount++;
                        }
                    }
                }
            }
        }

        results->minimaCount[t] = minimaCount;
        results->maximaCount[t] = maximaCount;
    }
}

// Write results to output file
void writeResults(const char* outputFile, const TimeSeriesResults* globalResults, int timeSteps, const TimingInfo* timing) {
    FILE* fp = fopen(outputFile, "w");
    if (!fp) {
        printf("Error: Cannot open output file %s\n", outputFile);
        return;
    }

    // Line 1: Local minima and maxima counts
    for (int t = 0; t < timeSteps; t++) {
        fprintf(fp, "(%d, %d)", globalResults->minimaCount[t], globalResults->maximaCount[t]);
        if (t < timeSteps - 1) {
            fprintf(fp, ", ");
        }
    }
    fprintf(fp, "\n");

    // Line 2: Global minimum and maximum values
    for (int t = 0; t < timeSteps; t++) {
        fprintf(fp, "(%g, %g)", globalResults->minValues[t], globalResults->maxValues[t]);
        if (t < timeSteps - 1) {
            fprintf(fp, ", ");
        }
    }
    fprintf(fp, "\n");

    // Line 3: Timing information
    fprintf(fp, "%g, %g, %g\n", timing->readTime, timing->mainCodeTime, timing->totalTime);

    fclose(fp);
}

int main(int argc, char** argv) {
    int rank, size;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Check for correct number of arguments
    if (argc != 10) {
        if (rank == 0) {
            printf("Usage: %s <inputFile> <pX> <pY> <pZ> <nX> <nY> <nZ> <timeSteps> <outputFile>\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    // Parse command line arguments
    char inputFile[256], outputFile[256];
    strcpy(inputFile, argv[1]);
    int pX = atoi(argv[2]);
    int pY = atoi(argv[3]);
    int pZ = atoi(argv[4]);
    int nX = atoi(argv[5]);
    int nY = atoi(argv[6]);
    int nZ = atoi(argv[7]);
    int timeSteps = atoi(argv[8]);
    strcpy(outputFile, argv[9]);

    // Verify process grid matches total number of processes
    if (pX * pY * pZ != size) {
        if (rank == 0) {
            printf("Error: pX*pY*pZ (%d) must equal the total number of processes (%d)\n", pX*pY*pZ, size);
        }
        MPI_Finalize();
        return 1;
    }

    // Start timing
    double time1 = MPI_Wtime();

    // Calculate domain decomposition
    SubDomain subdomain;
    calculateSubDomainBoundaries(rank, pX, pY, pZ, nX, nY, nZ, &subdomain);

    // Read data using Level-0 parallel I/O (independent reads)
    float* localData = readInputDataParallel_Level3(inputFile, &subdomain, nX, nY, nZ, timeSteps);

    if (!localData) {
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    // End of read timing
    double time2 = MPI_Wtime();

    // Allocate structures for results
    TimeSeriesResults* localResults = allocateResults(timeSteps);
    TimeSeriesResults* globalResults = NULL;

    // Process local data
    processLocalData(localData, &subdomain, localResults, timeSteps);

    // Allocate global results on root process
    if (rank == 0) {
        globalResults = allocateResults(timeSteps);
    }

    // Reduce results
    MPI_Reduce(localResults->minimaCount, rank == 0 ? globalResults->minimaCount : NULL,
              timeSteps, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    MPI_Reduce(localResults->maximaCount, rank == 0 ? globalResults->maximaCount : NULL,
              timeSteps, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    MPI_Reduce(localResults->minValues, rank == 0 ? globalResults->minValues : NULL,
              timeSteps, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);

    MPI_Reduce(localResults->maxValues, rank == 0 ? globalResults->maxValues : NULL,
              timeSteps, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // End main code timing
    double time3 = MPI_Wtime();

    // Compute timing information
    TimingInfo timing;
    timing.readTime = time2 - time1;
    timing.mainCodeTime = time3 - time2;
    timing.totalTime = time3 - time1;

    TimingInfo maxTiming;
    MPI_Reduce(&timing, &maxTiming, 3, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // Write results to file
    if (rank == 0) {
        writeResults(outputFile, globalResults, timeSteps, &maxTiming);
        printf("Output written to %s\n", outputFile);
        freeResults(globalResults);
    }

    // Clean up
    freeResults(localResults);
    free(localData);

    MPI_Finalize();
    return 0;
}
