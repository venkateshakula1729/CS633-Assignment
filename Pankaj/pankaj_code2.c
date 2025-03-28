#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <stdbool.h>

#include "mpi.h"

// Define a structure for timing information
typedef struct {
    double readTime;
    double mainCodeTime;
    double totalTime;
} TimingInfo;

// converting 3d index to 1d index
int threeD_To_oneD(int x, int y, int z, int width, int height, int depth) {
    return ((z * height + y) * width + x);
}

bool checkLocalMinima(double* data, int x, int y, int z, int width, int height, int depth) {

    int idx = threeD_To_oneD(x, y, z, width, height, depth);
    double val = data[idx];
    bool isMinima = true;

    // Check left neighbor
    if (x > 0 && data[idx - 1] < val) {
        isMinima = false;
    }

    // Check right neighbor
    if (x < width - 1 && data[idx + 1] < val) {
        isMinima = false;
    }

    // Check top neighbor
    if (y > 0 && data[idx - width] < val) {
        isMinima = false;
    }

    // Check bottom neighbor
    if (y < height - 1 && data[idx + width] < val) {
        isMinima = false;
    }

    // Check front neighbor
    if (z > 0 && data[idx - height * width] < val) {
        isMinima = false;
    }

    // Check back neighbor
    if (z < depth - 1 && data[idx + height * width] < val) {
        isMinima = false;
    }

    return isMinima;
}

bool checkLocalMaxima(double* data, int x, int y, int z, int width, int height, int depth) {
    int idx = threeD_To_oneD(x, y, z, width, height, depth);
    double val = data[idx];
    bool isMaxima = true;

    // Check left neighbor
    if (x > 0 && data[idx - 1] > val) {
        isMaxima = false;
    }

    // Check right neighbor
    if (x < width - 1 && data[idx + 1] > val) {
        isMaxima = false;
    }

    // Check top neighbor
    if (y > 0 && data[idx - width] > val) {
        isMaxima = false;
    }

    // Check bottom neighbor
    if (y < height - 1 && data[idx + width] > val) {
        isMaxima = false;
    }

    // Check front neighbor
    if (z > 0 && data[idx - height * width] > val) {
        isMaxima = false;
    }

    // Check back neighbor
    if (z < depth - 1 && data[idx + height * width] > val) {
        isMaxima = false;
    }

    return isMaxima;
}

int main(int argc, char **argv) {

    int cur_rank, size;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &cur_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);


    char inputFile[100];
    strcpy(inputFile, argv[1]);
    int pX = atoi(argv[2]);
    int pY = atoi(argv[3]);
    int pZ = atoi(argv[4]);
    int nX = atoi(argv[5]);
    int nY = atoi(argv[6]);
    int nZ = atoi(argv[7]);
    int timeSteps = atoi(argv[8]);
    char outputFile[100];
    strcpy(outputFile, argv[9]);

    // Validate process grid
    if (pX * pY * pZ != size) {
        if (cur_rank == 0) {
            printf("Error: pX*pY*pZ (%d) must equal the total number of processes (%d)\n", pX*pY*pZ, size);
        }
        MPI_Finalize();
        return 1;
    }

    // Start timing
    double time1 = MPI_Wtime();

    int totalDomainSize = nX * nY * nZ;

    double *globalData = NULL;
    if (cur_rank == 0) {
        globalData = (double *)malloc(totalDomainSize * timeSteps * sizeof(double));
        FILE *fp = fopen(inputFile, "r");
        for (int point = 0; point < totalDomainSize; point++) {
            for (int t = 0; t < timeSteps; t++) {
                fscanf(fp, "%lf", &globalData[point * timeSteps + t]);
            }
        }
        fclose(fp);
        // // For debugging - print first few values
        // printf("First point, all timesteps: ");
        // for(int t = 0; t < timeSteps; t++) {
        //     printf("%lf ", globalData[t]);
        // }
        // printf("\n");
    }

    float time2 = MPI_Wtime();

    // sub domain position.
    int subDomainZ = cur_rank / (pX * pY);
    int subDomainY = (cur_rank % (pX * pY)) / pX;
    int subDomainX = cur_rank % pX;

    // assuming nX is divisible by pX and nY is divisible by pY and nZ is divisible by pZ.
    int subDomainSizeX = nX / pX;
    int subDomainSizeY = nY / pY;
    int subDomainSizeZ = nZ / pZ;

    // starting indices of the data that for this ranks sub-domain
    int startX = subDomainX * subDomainSizeX;
    int startY = subDomainY * subDomainSizeY;
    int startZ = subDomainZ * subDomainSizeZ;
    int endX = startX + subDomainSizeX - 1;
    if(subDomainX == pX - 1) endX = nX - 1;
    int endY = startY + subDomainSizeY - 1;
    if(subDomainY == pY - 1) endY = nY - 1;
    int endZ = startZ + subDomainSizeZ - 1;
    if(subDomainZ == pZ - 1) endZ = nZ - 1;

    int tempStartX = startX - 1 >= 0 ? startX - 1 : startX;
    int tempStartY = startY - 1 >= 0 ? startY - 1 : startY;
    int tempStartZ = startZ - 1 >= 0 ? startZ - 1 : startZ;
    int tempEndX = endX + 1 <= nX-1 ? endX + 1 : endX;
    int tempEndY = endY + 1 <= nY-1 ? endY + 1 : endY;
    int tempEndZ = endZ + 1 <= nZ-1 ? endZ + 1 : endZ;


    int localDataSize = (tempEndX + 1 - tempStartX) * (tempEndY + 1 - tempStartY) * (tempEndZ + 1 - tempStartZ) * timeSteps;
    double *localData = (double *)malloc(localDataSize * sizeof(double));
    // Distribute data from root to all processes
    if (cur_rank == 0) {
        // Root process keeps its own portion
        int idx = 0;
        for (int z = tempStartZ; z <= tempEndZ; z++) {
            for (int y = tempStartY; y <= tempEndY; y++) {
                for (int x = tempStartX; x <= tempEndX; x++) {
                    int globalIdx = threeD_To_oneD(x, y, z, nX, nY, nZ) * timeSteps;
                    for (int t = 0; t < timeSteps; t++) {
                        localData[idx++] = globalData[globalIdx + t];
                    }
                }
            }
        }

        // Send data to other processes
        for (int p = 1; p < size; p++) {
            int posZ = p / (pX * pY);
            int posY = (p % (pX * pY)) / pX;
            int posX = p % pX;

            int pStartX = posX * subDomainSizeX;
            int pStartY = posY * subDomainSizeY;
            int pStartZ = posZ * subDomainSizeZ;
            int pEndX = pStartX + subDomainSizeX - 1;
            if(posX == pX - 1) pEndX = nX - 1;
            int pEndY = pStartY + subDomainSizeY - 1;
            if(posY == pY - 1) pEndY = nY - 1;
            int pEndZ = pStartZ + subDomainSizeZ - 1;
            if(posZ == pZ - 1) pEndZ = nZ - 1;

            int tempPStartX = pStartX - 1 >= 0 ? pStartX - 1 : pStartX;
            int tempPStartY = pStartY - 1 >= 0 ? pStartY - 1 : pStartY;
            int tempPStartZ = pStartZ - 1 >= 0 ? pStartZ - 1 : pStartZ;
            int tempPEndX = pEndX + 1 <= nX - 1 ? pEndX + 1 : pEndX;
            int tempPEndY = pEndY + 1 <= nY - 1 ? pEndY + 1 : pEndY;
            int tempPEndZ = pEndZ + 1 <= nZ - 1 ? pEndZ + 1 : pEndZ;

            int sendDataSize = (tempPEndX - tempPStartX + 1) * (tempPEndY - tempPStartY + 1) * (tempPEndZ - tempPStartZ + 1) * timeSteps;
            double *tempBuffer = (double *)malloc(sendDataSize * sizeof(double));
            if (!tempBuffer) {
                printf("Rank 0: Failed to allocate temp buffer for process %d\n", p);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            int bufIdx = 0;
            for (int z = pStartZ; z < pStartZ + subDomainSizeZ; z++) {
                for (int y = pStartY; y < pStartY + subDomainSizeY; y++) {
                    for (int x = pStartX; x < pStartX + subDomainSizeX; x++) {
                        int globalIdx = threeD_To_oneD(x, y, z, nX, nY, nZ) * timeSteps;
                        for (int t = 0; t < timeSteps; t++) {
                            tempBuffer[bufIdx++] = globalData[globalIdx + t];
                        }
                    }
                }
            }

            MPI_Send(tempBuffer, sendDataSize, MPI_DOUBLE, p, 0, MPI_COMM_WORLD);
            free(tempBuffer);
        }
    } else {
        MPI_Recv(localData, localDataSize, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    if (cur_rank == 0 && globalData) {
        free(globalData);
    }

    // Start main code timing
    double time3 = MPI_Wtime();

    int *localMinimaCount = (int *)calloc(timeSteps, sizeof(int));
    int *localMaximaCount = (int *)calloc(timeSteps, sizeof(int));
    double *subDomainMinValues = (double *)malloc(timeSteps * sizeof(double));
    double *subDomainMaxValues = (double *)malloc(timeSteps * sizeof(double));

    for (int t = 0; t < timeSteps; t++) {
        subDomainMinValues[t] = DBL_MAX;
        subDomainMaxValues[t] = -DBL_MAX;
    }

    for(int t = 0; t < timeSteps; t++) {
        int localMinimaCount_at_t = 0;
        int localMaximaCount_at_t = 0;
        for (int z = 0; z <= tempEndZ - tempStartZ; z++) {
            for (int y = 0; y <= tempEndY - tempStartY; y++) {
                for (int x = 0; x <= tempEndX - tempStartX; x++) {
                    if(x >= (startX - tempStartX) && x <= (endX - tempStartX) && y >= (startY - tempStartY) && y <= (endY - tempStartY) && z >= (startZ - tempStartZ) && z <= (endZ - tempStartZ)) {
                        int width = tempEndX + 1 - tempStartX;
                        int height = tempEndY + 1 - tempStartY;
                        int depth = tempEndZ + 1 - tempStartZ;
                        int idx = threeD_To_oneD(x, y, z, width, height, depth);
                        double val = localData[idx];
                        if(val < subDomainMinValues[t]) subDomainMinValues[t] = val;
                        if(val > subDomainMaxValues[t]) subDomainMaxValues[t] = val;

                        if(checkLocalMinima(localData,x,y,z,width, height, depth)) localMinimaCount_at_t++;
                        if(checkLocalMaxima(localData,x,y,z,width, height, depth)) localMaximaCount_at_t++;

                    }
                }
            }
        }
        localMinimaCount[t] = localMinimaCount_at_t;
        localMaximaCount[t] = localMaximaCount_at_t;
    }

    int *globalMinimaCount = NULL;
    int *globalMaximaCount = NULL;
    double *globalMinValues = NULL;
    double *globalMaxValues = NULL;

    if (cur_rank == 0) {
        globalMinimaCount = (int *)malloc(timeSteps * sizeof(int));
        globalMaximaCount = (int *)malloc(timeSteps * sizeof(int));
        globalMinValues = (double *)malloc(timeSteps * sizeof(double));
        globalMaxValues = (double *)malloc(timeSteps * sizeof(double));
    }

    MPI_Reduce(localMinimaCount, globalMinimaCount, timeSteps, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(localMaximaCount, globalMaximaCount, timeSteps, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    MPI_Reduce(subDomainMinValues, globalMinValues, timeSteps, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
    MPI_Reduce(subDomainMaxValues, globalMaxValues, timeSteps, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // End main code timing
    double time4 = MPI_Wtime();

    TimingInfo timing;
    timing.readTime = time2 - time1;
    timing.mainCodeTime = time4 - time3;
    timing.totalTime = time4 - time1;

    TimingInfo maxTiming;
    MPI_Reduce(&timing, &maxTiming, 3, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (cur_rank == 0) {
        FILE *fp = fopen(outputFile, "w");
        if (fp == NULL) {
            printf("Error: Cannot open output file %s\n", outputFile);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        for (int t = 0; t < timeSteps; t++) {
            fprintf(fp, "(%d, %d)", globalMinimaCount[t], globalMaximaCount[t]);
            if (t < timeSteps - 1) {
                fprintf(fp, ", ");
            }
        }
        fprintf(fp, "\n");

        for (int t = 0; t < timeSteps; t++) {
            fprintf(fp, "(%g, %g)", globalMinValues[t], globalMaxValues[t]);
            if (t < timeSteps - 1) {
                fprintf(fp, ", ");
            }
        }
        fprintf(fp, "\n");

        fprintf(fp, "%g, %g, %g\n", maxTiming.readTime, maxTiming.mainCodeTime, maxTiming.totalTime);

        fclose(fp);
        printf("Output written to %s\n", outputFile);

        free(globalMinimaCount);
        free(globalMaximaCount);
        free(globalMinValues);
        free(globalMaxValues);
    }

    free(localData);
    free(localMinimaCount);
    free(localMaximaCount);
    free(subDomainMinValues);
    free(subDomainMaxValues);


    MPI_Finalize();
    return 0;
}
