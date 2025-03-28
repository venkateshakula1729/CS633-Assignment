#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include "mpi.h"

// Define a structure for timing information
typedef struct {
    double readTime;
    double mainCodeTime;
    double totalTime;
} TimingInfo;



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



    // if(rank == 2)
    // {
    // printf("Input File: %s\n", inputFile);
    // printf("Particle Position: (%d, %d, %d)\n", pX, pY, pZ);
    // printf("Grid Dimensions: (%d, %d, %d)\n", nX, nY, nZ);
    // printf("Time Steps: %d\n", timeSteps);
    // printf("Output File: %s\n", outputFile);
    // }

    // Start timing
    double time1 = MPI_Wtime();

    int totalDomainSize = nX * nY * nZ;

    double *globalData = NULL;
    if (cur_rank == 0) {
        // Allocating memory for entire data
        globalData = (double *)malloc(totalDomainSize * timeSteps * sizeof(double));
        // if (!globalData) {
        //     printf("Error: Failed to allocate memory for global data\n");
        //     MPI_Abort(MPI_COMM_WORLD, 1);
        // }

        FILE *fp = fopen(inputFile, "r");
        // if (fp == NULL) {
        //     printf("Error opening input file\n");
        //     MPI_Abort(MPI_COMM_WORLD, 1);
        // }

        for (int point = 0; point < totalDomainSize; point++) {
            for (int t = 0; t < timeSteps; t++) {
                if (fscanf(fp, "%lf", &globalData[point * timeSteps + t]) != 1) {
                    printf("Error reading data\n");
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
            }
        }
        fclose(fp);

        // For debugging - print first few values
        printf("First point, all timesteps: ");
        for(int t = 0; t < timeSteps; t++) {
            printf("%lf ", globalData[t]);
        }
        printf("\n");
    }

    // Record read time
    float time2 = MPI_Wtime();

    // case 1 assuming nX is divisible by pX and nY is divisible by pY and nZ is divisible by pZ.
    int subDomainSizeX = nX / pX;
    int subDomainSizeY = nY / pY;
    int subDomainSizeZ = nZ / pZ;
    int subDomainSize = subDomainSizeX * subDomainSizeY * subDomainSizeZ;

    double *localData = (double *)malloc(subDomainSize * timeSteps * sizeof(double));

    // rank position based on the subdomain.
    int subDomainX = cur_rank / (pX * pY);
    int subDomainY = (cur_rank % (pX * pY)) / pX;
    int subDomainZ = cur_rank % pX;

    // starting indices for this ranks sub-domain
    int startX = subDomainX * subDomainSizeX;
    int startY = subDomainY * subDomainSizeY;
    int startZ = subDomainZ * subDomainSizeZ;

    // Distribute data from root to all processes
    if (cur_rank == 0) {
        // Root process keeps its own portion
        int idx = 0;
        for (int z = startZ; z < startZ + subDomainSizeZ; z++) {
            for (int y = startY; y < startY + subDomainSizeY; y++) {
                for (int x = startX; x < startX + subDomainSizeX; x++) {
                    int globalIdx = (z * nX * nY + y * nX + x) * timeSteps;
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

            double *tempBuffer = (double *)malloc(subDomainSize * timeSteps * sizeof(double));
            if (!tempBuffer) {
                printf("Rank 0: Failed to allocate temp buffer for process %d\n", p);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            int bufIdx = 0;
            for (int z = pStartZ; z < pStartZ + subDomainSizeZ; z++) {
                for (int y = pStartY; y < pStartY + subDomainSizeY; y++) {
                    for (int x = pStartX; x < pStartX + subDomainSizeX; x++) {
                        int globalIdx = (z * nX * nY + y * nX + x) * timeSteps;
                        for (int t = 0; t < timeSteps; t++) {
                            tempBuffer[bufIdx++] = globalData[globalIdx + t];
                        }
                    }
                }
            }

            MPI_Send(tempBuffer, subDomainSize, MPI_DOUBLE, p, 0, MPI_COMM_WORLD);
            free(tempBuffer);
        }
    } else {
        // Other processes receive their portion of data
        MPI_Recv(localData, subDomainSize, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }

    // Free global data as it's no longer needed
    if (cur_rank == 0 && globalData) {
        free(globalData);
    }

    // Start main code timing
    double time3 = MPI_Wtime();

    // Arrays to track results
    int *localMinimaCount = (int *)calloc(timeSteps, sizeof(int));
    int *localMaximaCount = (int *)calloc(timeSteps, sizeof(int));
    double *subDomainMinValues = (double *)malloc(timeSteps * sizeof(double));
    double *subDomainMaxValues = (double *)malloc(timeSteps * sizeof(double));

    for (int t = 0; t < timeSteps; t++) {
        subDomainMinValues[t] = DBL_MAX;
        subDomainMaxValues[t] = -DBL_MAX;
    }

    // Process local data to find minima, maxima, and extreme values
    for (int z = 0; z < subDomainSizeZ; z++) {
        for (int y = 0; y < subDomainSizeY; y++) {
            for (int x = 0; x < subDomainSizeX; x++) {
                int localIdx = ((z * subDomainSizeY + y) * subDomainSizeX + x);

                // Map local coordinates to global coordinates
                // int globalX = startX + x;
                // int globalY = startY + y;
                // int globalZ = startZ + z;

                for (int t = 0; t < timeSteps; t++) {
                    double value = localData[localIdx + t];

                    // Update min/max values
                    if (value < subDomainMinValues[t]) {
                        subDomainMinValues[t] = value;
                    }
                    if (value > subDomainMaxValues[t]) {
                        subDomainMaxValues[t] = value;
                    }

                    // Check for local minima and maxima
                    // Note: This requires checking neighbors, which might be in other processes
                    // For simplicity, we'll only check interior points
                    if (x > 0 && x < subDomainSizeX - 1 &&
                        y > 0 && y < subDomainSizeY - 1 &&
                        z > 0 && z < subDomainSizeZ - 1) {

                        // Check if this point is a local minimum
                        int isMin = 1;
                        int isMax = 1;

                        // Check all six neighbors
                        int neighborOffsets[6][3] = {
                            {-1, 0, 0}, {1, 0, 0},
                            {0, -1, 0}, {0, 1, 0},
                            {0, 0, -1}, {0, 0, 1}
                        };

                        for (int n = 0; n < 6; n++) {
                            int nx = x + neighborOffsets[n][0];
                            int ny = y + neighborOffsets[n][1];
                            int nz = z + neighborOffsets[n][2];

                            int neighborIdx = (nz * subDomainSizeX * subDomainSizeY + ny * subDomainSizeX + nx) * timeSteps + t;
                            double neighborValue = localData[neighborIdx];

                            // If neighbor is less than or equal, not a minimum
                            if (value >= neighborValue) {
                                isMin = 0;
                            }

                            // If neighbor is greater than or equal, not a maximum
                            if (value <= neighborValue) {
                                isMax = 0;
                            }
                        }

                        if (isMin) {
                            localMinimaCount[t]++;
                        }
                        if (isMax) {
                            localMaximaCount[t]++;
                        }
                    }
                }
            }
        }
    }

    // Gather results from all processes
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

    // Sum up local minima/maxima counts
    MPI_Reduce(localMinimaCount, globalMinimaCount, timeSteps, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(localMaximaCount, globalMaximaCount, timeSteps, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    // Find global min/max values
    MPI_Reduce(subDomainMinValues, globalMinValues, timeSteps, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
    MPI_Reduce(subDomainMaxValues, globalMaxValues, timeSteps, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // End main code timing
    double time4 = MPI_Wtime();

    // Calculate timing information
    TimingInfo timing;
    timing.readTime = time2 - time1;
    timing.mainCodeTime = time4 - time3;
    timing.totalTime = time4 - time1;

    // Find maximum timing across all processes
    TimingInfo maxTiming;
    MPI_Reduce(&timing, &maxTiming, 3, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // Write output file (only rank 0)
    if (cur_rank == 0) {
        FILE *fp = fopen(outputFile, "w");
        if (fp == NULL) {
            printf("Error: Cannot open output file %s\n", outputFile);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        // Write local minima and maxima counts for each time step
        for (int t = 0; t < timeSteps; t++) {
            fprintf(fp, "(%d, %d)", globalMinimaCount[t], globalMaximaCount[t]);
            if (t < timeSteps - 1) {
                fprintf(fp, ", ");
            }
        }
        fprintf(fp, "\n");

        // Write global minimum and maximum values for each time step
        for (int t = 0; t < timeSteps; t++) {
            fprintf(fp, "(%g, %g)", globalMinValues[t], globalMaxValues[t]);
            if (t < timeSteps - 1) {
                fprintf(fp, ", ");
            }
        }
        fprintf(fp, "\n");

        // Write timing information
        fprintf(fp, "%g, %g, %g\n", maxTiming.readTime, maxTiming.mainCodeTime, maxTiming.totalTime);

        fclose(fp);
        printf("Output written to %s\n", outputFile);

        // Free allocated memory
        free(globalMinimaCount);
        free(globalMaximaCount);
        free(globalMinValues);
        free(globalMaxValues);
    }

    // Free local memory
    free(localData);
    free(localMinimaCount);
    free(localMaximaCount);
    free(subDomainMinValues);
    free(subDomainMaxValues);


    MPI_Finalize();
    return 0;
}
