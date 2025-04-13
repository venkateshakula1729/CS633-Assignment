#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <stdbool.h>
#include "mpi.h"

// Structure to hold timing information
typedef struct {
    float readTime;
    float mainCodeTime;
    float totalTime;
} TimingInfo;

// Structure to hold global analysis results
typedef struct {
    int* minimaCount;
    int* maximaCount;
    float* minValues;
    float* maxValues;
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
    results->minValues = (float*)malloc(timeSteps * sizeof(float));
    results->maxValues = (float*)malloc(timeSteps * sizeof(float));

    // Initialize min/max values
    for (int t = 0; t < timeSteps; t++) {
        results->minValues[t] = FLT_MAX;
        results->maxValues[t] = -FLT_MAX;
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
    if (x > 0 && data[getLinearIndex(x-1, y, z, width, height, depth) * timeSteps + time] <= value)
        return false;
    if (x < width-1 && data[getLinearIndex(x+1, y, z, width, height, depth) * timeSteps + time] <= value)
        return false;

    // Y-axis neighbors
    if (y > 0 && data[getLinearIndex(x, y-1, z, width, height, depth) * timeSteps + time] <= value)
        return false;
    if (y < height-1 && data[getLinearIndex(x, y+1, z, width, height, depth) * timeSteps + time] <= value)
        return false;

    // Z-axis neighbors
    if (z > 0 && data[getLinearIndex(x, y, z-1, width, height, depth) * timeSteps + time] <= value)
        return false;
    if (z < depth-1 && data[getLinearIndex(x, y, z+1, width, height, depth) * timeSteps + time] <= value)
        return false;

    return true;
}

// Check if a point is a local maximum
bool isLocalMaximum(float* data, int x, int y, int z, int width, int height, int depth, int time, int timeSteps) {
    int idx = getLinearIndex(x, y, z, width, height, depth);
    float value = data[idx * timeSteps + time];

    // Check all six neighbors (if they exist)
    // X-axis neighbors
    if (x > 0 && data[getLinearIndex(x-1, y, z, width, height, depth) * timeSteps + time] >= value)
        return false;
    if (x < width-1 && data[getLinearIndex(x+1, y, z, width, height, depth) * timeSteps + time] >= value)
        return false;

    // Y-axis neighbors
    if (y > 0 && data[getLinearIndex(x, y-1, z, width, height, depth) * timeSteps + time] >= value)
        return false;
    if (y < height-1 && data[getLinearIndex(x, y+1, z, width, height, depth) * timeSteps + time] >= value)
        return false;

    // Z-axis neighbors
    if (z > 0 && data[getLinearIndex(x, y, z-1, width, height, depth) * timeSteps + time] >= value)
        return false;
    if (z < depth-1 && data[getLinearIndex(x, y, z+1, width, height, depth) * timeSteps + time] >= value)
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

// Optimized binary file reading function
float* readInputData(const char* inputFile, int totalDomainSize, int timeSteps) {
    float* data = (float*)malloc(totalDomainSize * timeSteps * sizeof(float));
    if (!data) {
        printf("Failed to allocate memory for input data\n");
        return NULL;
    }

    FILE* fp = fopen(inputFile, "rb");  // Open in binary mode
    if (!fp) {
        printf("Failed to open input file: %s\n", inputFile);
        free(data);
        return NULL;
    }

    // Use buffered I/O for better performance
    char* buffer = (char*)malloc(8192 * 1024); // 8MB buffer
    if (buffer) {
        setvbuf(fp, buffer, _IOFBF, 8192 * 1024);
    }

    // Read data directly from file (already in float format)
    const int BLOCK_SIZE = 4096;  // Increased block size for better performance
    for (int point = 0; point < totalDomainSize; point += BLOCK_SIZE) {
        int blockEnd = (point + BLOCK_SIZE < totalDomainSize) ?
                      point + BLOCK_SIZE : totalDomainSize;
        int pointsToRead = blockEnd - point;

        // Read a block of float values directly into the data array
        size_t itemsRead = fread(&data[(point * timeSteps)], sizeof(float),
                                pointsToRead * timeSteps, fp);

        if (itemsRead != pointsToRead * timeSteps) {
            printf("Error reading data: expected %d items, got %zu\n",
                   pointsToRead * timeSteps, itemsRead);
            free(data);
            if (buffer) free(buffer);
            fclose(fp);
            return NULL;
        }
    }

    if (buffer) free(buffer);
    fclose(fp);
    return data;
}

// Distribute data from root to all processes
float* distributeData(int rank, const SubDomain* subdomain, float* globalData,
                     int nX, int nY, int nZ, int timeSteps, int pX, int pY, int pZ) {
    int localDataSize = subdomain->tempWidth * subdomain->tempHeight *
                        subdomain->tempDepth * timeSteps;

    float* localData = (float*)malloc(localDataSize * sizeof(float));
    if (!localData) {
        printf("Rank %d: Failed to allocate memory for local data\n", rank);
        return NULL;
    }

    if (rank == 0) {
        // Root process copies its own data
        int idx = 0;
        for (int z = subdomain->tempStartZ; z <= subdomain->tempEndZ; z++) {
            for (int y = subdomain->tempStartY; y <= subdomain->tempEndY; y++) {
                for (int x = subdomain->tempStartX; x <= subdomain->tempEndX; x++) {
                    int globalIdx = getLinearIndex(x, y, z, nX, nY, nZ) * timeSteps;
                    for (int t = 0; t < timeSteps; t++) {
                        localData[idx++] = globalData[globalIdx + t];
                    }
                }
            }
        }

        // Send data to other processes
        for (int p = 1; p < pX * pY * pZ; p++) {
            SubDomain recvSubdomain;
            calculateSubDomainBoundaries(p, pX, pY, pZ, nX, nY, nZ, &recvSubdomain);

            int sendDataSize = recvSubdomain.tempWidth * recvSubdomain.tempHeight *
                              recvSubdomain.tempDepth * timeSteps;

            float* tempBuffer = (float*)malloc(sendDataSize * sizeof(float));
            if (!tempBuffer) {
                printf("Rank 0: Failed to allocate temp buffer for process %d\n", p);
                free(localData);
                return NULL;
            }

            int bufIdx = 0;
            for (int z = recvSubdomain.tempStartZ; z <= recvSubdomain.tempEndZ; z++) {
                for (int y = recvSubdomain.tempStartY; y <= recvSubdomain.tempEndY; y++) {
                    for (int x = recvSubdomain.tempStartX; x <= recvSubdomain.tempEndX; x++) {
                        int globalIdx = getLinearIndex(x, y, z, nX, nY, nZ) * timeSteps;
                        for (int t = 0; t < timeSteps; t++) {
                            tempBuffer[bufIdx++] = globalData[globalIdx + t];
                        }
                    }
                }
            }

            MPI_Send(tempBuffer, sendDataSize, MPI_FLOAT, p, 0, MPI_COMM_WORLD);
            free(tempBuffer);
        }
    } else {
        // Receive data from root
        MPI_Recv(localData, localDataSize, MPI_FLOAT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

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
        fprintf(fp, "(%g, %g)", (double)globalResults->minValues[t], (double)globalResults->maxValues[t]);
        if (t < timeSteps - 1) {
            fprintf(fp, ", ");
        }
    }
    fprintf(fp, "\n");

    // Line 3: Timing information
    fprintf(fp, "%g, %g, %g\n", (double)timing->readTime, (double)timing->mainCodeTime, (double)timing->totalTime);

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

    // Read and distribute data
    float* localData = NULL;
    float* globalData = NULL;
    int totalDomainSize = nX * nY * nZ;

    if (rank == 0) {
        globalData = readInputData(inputFile, totalDomainSize, timeSteps);
        if (!globalData) {
            MPI_Abort(MPI_COMM_WORLD, 1);
            return 1;
        }
    }

    // Distribute data
    localData = distributeData(rank, &subdomain, globalData, nX, nY, nZ, timeSteps, pX, pY, pZ);
    if (!localData) {
        if (rank == 0 && globalData) free(globalData);
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }

    // Free global data on rank 0 as it's no longer needed
    if (rank == 0 && globalData) {
        free(globalData);
    }

    // Start main code timing
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
              timeSteps, MPI_FLOAT, MPI_MIN, 0, MPI_COMM_WORLD);

    MPI_Reduce(localResults->maxValues, rank == 0 ? globalResults->maxValues : NULL,
              timeSteps, MPI_FLOAT, MPI_MAX, 0, MPI_COMM_WORLD);

    // End main code timing
    double time3 = MPI_Wtime();

    // Compute timing information
    TimingInfo timing;
    timing.readTime = (float)(time2 - time1);
    timing.mainCodeTime = (float)(time3 - time2);
    timing.totalTime = (float)(time3 - time1);

    TimingInfo maxTiming;
    MPI_Reduce(&timing, &maxTiming, 3, MPI_FLOAT, MPI_MAX, 0, MPI_COMM_WORLD);

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
