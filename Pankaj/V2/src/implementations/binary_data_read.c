#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <sys/stat.h>

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc != 6) {
        if (rank == 0) {
            printf("Usage: %s <binary_file> <nx> <ny> <nz> <timesteps>\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    char* filename = argv[1];
    int nx = atoi(argv[2]);
    int ny = atoi(argv[3]);
    int nz = atoi(argv[4]);
    int timesteps = atoi(argv[5]);

    int total_points = nx * ny * nz;
    int points_per_process = total_points / size;
    int start_point = rank * points_per_process;
    int end_point = (rank == size - 1) ? total_points : start_point + points_per_process;
    int local_points = end_point - start_point;

    // Check file size
    if (rank == 0) {
        // Using stat to get file size
        struct stat st;
        if (stat(filename, &st) == 0) {
            long file_size = st.st_size;
            long expected_size_double = (long)total_points * timesteps * sizeof(double);
            long expected_size_float = (long)total_points * timesteps * sizeof(float);

            printf("Reading binary file: %s\n", filename);
            printf("Domain size: %d x %d x %d with %d timesteps\n", nx, ny, nz, timesteps);
            printf("Total points: %d, Points per process: ~%d\n", total_points, points_per_process);
            printf("File size: %ld bytes\n", file_size);
            printf("Expected size (float): %ld bytes\n", expected_size_float);
            printf("Expected size (double): %ld bytes\n", expected_size_double);

            if (file_size == expected_size_float) {
                printf("File size matches expected size for float data.\n");
            } else if (file_size == expected_size_double) {
                printf("File size matches expected size for double data.\n");
            } else {
                printf("WARNING: File size doesn't match expected size for either float or double!\n");
            }
        } else {
            printf("ERROR: Could not determine file size!\n");
        }
    }

    // Open the binary file
    MPI_File fh;
    MPI_Status status;
    int ret = MPI_File_open(MPI_COMM_WORLD, filename, MPI_MODE_RDONLY, MPI_INFO_NULL, &fh);

    if (ret != MPI_SUCCESS) {
        char error_string[MPI_MAX_ERROR_STRING];
        int length_of_error_string;
        MPI_Error_string(ret, error_string, &length_of_error_string);
        printf("Rank %d: Error opening file: %s\n", rank, error_string);
        MPI_Finalize();
        return 1;
    }

    // Get file size using MPI
    MPI_Offset file_size;
    MPI_File_get_size(fh, &file_size);

    if (rank == 0) {
        printf("MPI file size: %lld bytes\n", (long long)file_size);
    }

    // Determine if we should use float or double based on file size
    int use_float = 1;  // Default to float based on your observation
    if (file_size == (MPI_Offset)total_points * timesteps * sizeof(double)) {
        use_float = 0;  // Use double if file size matches
    }

    if (rank == 0) {
        printf("Using %s data type based on file size\n", use_float ? "float" : "double");
    }

    // Allocate memory for local data (using float)
    float* local_data_float = NULL;
    double* local_data_double = NULL;

    if (use_float) {
        local_data_float = (float*)malloc(local_points * timesteps * sizeof(float));
        if (!local_data_float) {
            printf("Rank %d: Failed to allocate memory for local data\n", rank);
            MPI_File_close(&fh);
            MPI_Finalize();
            return 1;
        }
    } else {
        local_data_double = (double*)malloc(local_points * timesteps * sizeof(double));
        if (!local_data_double) {
            printf("Rank %d: Failed to allocate memory for local data\n", rank);
            MPI_File_close(&fh);
            MPI_Finalize();
            return 1;
        }
    }

    // Each process reads its portion of the data
    MPI_Offset offset;
    if (use_float) {
        offset = start_point * timesteps * sizeof(float);
    } else {
        offset = start_point * timesteps * sizeof(double);
    }

    if (rank == 0) {
        printf("\nProcess offsets and sizes:\n");
    }

    // Print info about each process's read plan
    for (int i = 0; i < size; i++) {
        if (rank == i) {
            printf("Rank %d: Reading %d points starting at offset %lld (%lld bytes)\n",
                   rank, local_points, (long long)(start_point), (long long)offset);
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    double start_time = MPI_Wtime();

    MPI_File_seek(fh, offset, MPI_SEEK_SET);

    // Now attempt the full read with the appropriate data type
    if (use_float) {
        MPI_File_read(fh, local_data_float, local_points * timesteps, MPI_FLOAT, &status);
    } else {
        MPI_File_read(fh, local_data_double, local_points * timesteps, MPI_DOUBLE, &status);
    }

    double end_time = MPI_Wtime();
    double local_time = end_time - start_time;
    double max_time;

    MPI_Reduce(&local_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    // Check if the correct amount of data was read
    int count;
    if (use_float) {
        MPI_Get_count(&status, MPI_FLOAT, &count);
        printf("Rank %d: Read %d float elements out of %d requested\n",
               rank, count, local_points * timesteps);
    } else {
        MPI_Get_count(&status, MPI_DOUBLE, &count);
        printf("Rank %d: Read %d double elements out of %d requested\n",
               rank, count, local_points * timesteps);
    }

    // Print some statistics
    if (rank == 0) {
        printf("\nTime to read binary data: %f seconds\n", max_time);
        printf("Read performance: %f MB/s\n",
               (total_points * timesteps * (use_float ? sizeof(float) : sizeof(double))) /
               (1024.0 * 1024.0) / max_time);

        printf("\nSample data from first process:\n");
        for (int i = 0; i < 5 && i < local_points; i++) {
            printf("Point %d: ", i);
            for (int t = 0; t < timesteps; t++) {
                if (use_float) {
                    printf("%g ", local_data_float[i * timesteps + t]);
                } else {
                    printf("%g ", local_data_double[i * timesteps + t]);
                }
            }
            printf("\n");
        }
    }

    // Clean up
    if (use_float) {
        free(local_data_float);
    } else {
        free(local_data_double);
    }

    MPI_File_close(&fh);
    MPI_Finalize();

    return 0;
}
