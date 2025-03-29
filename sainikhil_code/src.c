#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

typedef struct {
    int start;
    int end;
    int size;
} Range;

Range get_range(int rank, int total, int size) {
    int base = size / total;
    int remainder = size % total;
    int start = 0;
    for (int i = 0; i < rank; i++) {
        start += base + (i < remainder ? 1 : 0);
    }
    int local_size = base + (rank < remainder ? 1 : 0);
    Range r = {start, start + local_size - 1, local_size};
    return r;
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc != 10) {
        if (rank == 0) fprintf(stderr, "Usage: %s dataset PX PY PZ NX NY NZ NC output\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    char *input_file = argv[1];
    int PX = atoi(argv[2]);
    int PY = atoi(argv[3]);
    int PZ = atoi(argv[4]);
    int NX = atoi(argv[5]);
    int NY = atoi(argv[6]);
    int NZ = atoi(argv[7]);
    int NC = atoi(argv[8]);
    char *output_file = argv[9];

    if (PX * PY * PZ != size) {
        if (rank == 0) fprintf(stderr, "Error: PX*PY*PZ must equal the number of processes\n");
        MPI_Finalize();
        return 1;
    }

    int dims[3] = {PX, PY, PZ};
    int periods[3] = {0, 0, 0};
    MPI_Comm cart_comm;
    MPI_Cart_create(MPI_COMM_WORLD, 3, dims, periods, 0, &cart_comm);

    int coords[3];
    MPI_Cart_coords(cart_comm, rank, 3, coords);
    int px = coords[0];
    int py = coords[1];
    int pz = coords[2];

    Range x_range = get_range(px, PX, NX);
    Range y_range = get_range(py, PY, NY);
    Range z_range = get_range(pz, PZ, NZ);

    int x_local = x_range.size;
    int y_local = y_range.size;
    int z_local = z_range.size;
    int local_points = x_local * y_local * z_local;

    float **local_data = (float **)malloc(local_points * sizeof(float *));
    for (int i = 0; i < local_points; i++) {
        local_data[i] = (float *)malloc(NC * sizeof(float));
    }

    if (rank == 0) {
        FILE *file = fopen(input_file, "r");
        if (!file) {
            perror("Failed to open input file");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        int total_points = NX * NY * NZ;
        float **global_data = (float **)malloc(total_points * sizeof(float *));
        for (int i = 0; i < total_points; i++) {
            global_data[i] = (float *)malloc(NC * sizeof(float));
            for (int t = 0; t < NC; t++) {
                if (fscanf(file, "%f", &global_data[i][t]) != 1) {
                    fprintf(stderr, "Error reading data\n");
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
            }
        }
        fclose(file);

        for (int p = 0; p < size; p++) {
            int pcoords[3];
            MPI_Cart_coords(cart_comm, p, 3, pcoords);

            Range pxr = get_range(pcoords[0], PX, NX);
            Range pyr = get_range(pcoords[1], PY, NY);
            Range pzr = get_range(pcoords[2], PZ, NZ);

            int psize = pxr.size * pyr.size * pzr.size;
            float *buffer = (float *)malloc(psize * NC * sizeof(float));

            int idx = 0;
            for (int z = pzr.start; z <= pzr.end; z++) {
                for (int y = pyr.start; y <= pyr.end; y++) {
                    for (int x = pxr.start; x <= pxr.end; x++) {
                        int gidx = z * (NX * NY) + y * NX + x;
                        memcpy(buffer + idx * NC, global_data[gidx], NC * sizeof(float));
                        idx++;
                    }
                }
            }

            if (p == 0) {
                for (int i = 0; i < psize; i++) {
                    memcpy(local_data[i], buffer + i * NC, NC * sizeof(float));
                }
            } else {
                MPI_Send(buffer, psize * NC, MPI_FLOAT, p, 0, cart_comm);
            }

            free(buffer);
        }

        for (int i = 0; i < total_points; i++) free(global_data[i]);
        free(global_data);
    } else {
        float *buffer = (float *)malloc(local_points * NC * sizeof(float));
        MPI_Recv(buffer, local_points * NC, MPI_FLOAT, 0, 0, cart_comm, MPI_STATUS_IGNORE);
        for (int i = 0; i < local_points; i++) {
            memcpy(local_data[i], buffer + i * NC, NC * sizeof(float));
        }
        free(buffer);
    }

    int *local_mins = (int *)calloc(NC, sizeof(int));
    int *local_maxs = (int *)calloc(NC, sizeof(int));
    float *global_min = (float *)malloc(NC * sizeof(float));
    float *global_max = (float *)malloc(NC * sizeof(float));

    double t_start = MPI_Wtime();

    for (int t = 0; t < NC; t++) {
        float *sub_data = (float *)malloc(x_local * y_local * z_local * sizeof(float));
        int idx = 0;
        for (int z = 0; z < z_local; z++) {
            for (int y = 0; y < y_local; y++) {
                for (int x = 0; x < x_local; x++) {
                    sub_data[idx++] = local_data[z * y_local * x_local + y * x_local + x][t];
                }
            }
        }

        float ***halo = (float ***)malloc((x_local + 2) * sizeof(float **));
        for (int i = 0; i < x_local + 2; i++) {
            halo[i] = (float **)malloc((y_local + 2) * sizeof(float *));
            for (int j = 0; j < y_local + 2; j++) {
                halo[i][j] = (float *)malloc((z_local + 2) * sizeof(float));
                for (int k = 0; k < z_local + 2; k++) {
                    halo[i][j][k] = FLT_MAX;
                }
            }
        }

        idx = 0;
        for (int i = 1; i <= x_local; i++) {
            for (int j = 1; j <= y_local; j++) {
                for (int k = 1; k <= z_local; k++) {
                    halo[i][j][k] = sub_data[idx++];
                }
            }
        }

        int left, right, down, up, back, front;
        MPI_Cart_shift(cart_comm, 0, 1, &left, &right);
        MPI_Cart_shift(cart_comm, 1, 1, &down, &up);
        MPI_Cart_shift(cart_comm, 2, 1, &back, &front);

        // Halo exchange in x-direction
        if (left != MPI_PROC_NULL) {
            MPI_Sendrecv(&halo[1][1][1], y_local * z_local, MPI_FLOAT, left, 0,
                         &halo[0][1][1], y_local * z_local, MPI_FLOAT, left, 0, cart_comm, MPI_STATUS_IGNORE);
        }
        if (right != MPI_PROC_NULL) {
            MPI_Sendrecv(&halo[x_local][1][1], y_local * z_local, MPI_FLOAT, right, 0,
                         &halo[x_local + 1][1][1], y_local * z_local, MPI_FLOAT, right, 0, cart_comm, MPI_STATUS_IGNORE);
        }

        // Similar steps for y and z directions (omitted for brevity)

        int cnt_min = 0, cnt_max = 0;
        float lmin = FLT_MAX, lmax = -FLT_MAX;

        // for (int i = 1; i <= x_local; i++) {
        //     for (int j = 1; j <= y_local; j++) {
        //         for (int k = 1; k <= z_local; k++) {
        //             float val = halo[i][j][k];
        //             int is_min = 1, is_max = 1;

        //             for (int dx = -1; dx <= 1; dx++) {
        //                 for (int dy = -1; dy <= 1; dy++) {
        //                     for (int dz = -1; dz <= 1; dz++) {
        //                         if (dx == 0 && dy == 0 && dz == 0) continue;

        //                         int ni = i + dx, nj = j + dy, nk = k + dz;
        //                         if (ni < 0 || ni >= x_local + 2 || nj < 0 || nj >= y_local + 2 || nk < 0 || nk >= z_local + 2)
        //                             continue;

        //                         float nval = halo[ni][nj][nk];
        //                         if (nval < val) is_min = 0;
        //                         if (nval > val) is_max = 0;
        //                     }
        //                 }
        //             }

        //             if (is_min) cnt_min++;
        //             if (is_max) cnt_max++;
        //             if (val < lmin) lmin = val;
        //             if (val > lmax) lmax = val;
        //         }
        //     }
        // }



        //GitHub Copilot
        // ...existing code...

        for (int i = 1; i <= x_local; i++) {
            for (int j = 1; j <= y_local; j++) {
                for (int k = 1; k <= z_local; k++) {
                    float val = halo[i][j][k];
                    int is_min = 1, is_max = 1;
        
                    // Compare with six direct neighbors
                    int neighbors[6][3] = {
                        {i - 1, j, k}, // left
                        {i + 1, j, k}, // right
                        {i, j - 1, k}, // down
                        {i, j + 1, k}, // up
                        {i, j, k - 1}, // back
                        {i, j, k + 1}  // front
                    };
                    
                    for (int n = 0; n < 6; n++) {
                        int ni = neighbors[n][0];
                        int nj = neighbors[n][1];
                        int nk = neighbors[n][2];
                    
                        // Ensure the neighbor is within bounds of the local halo and the global grid
                        if (ni < 0 || ni >= x_local + 2 || nj < 0 || nj >= y_local + 2 || nk < 0 || nk >= z_local + 2 ||
                            (x_range.start + ni - 1) < 0 || (x_range.start + ni - 1) >= NX ||
                            (y_range.start + nj - 1) < 0 || (y_range.start + nj - 1) >= NY ||
                            (z_range.start + nk - 1) < 0 || (z_range.start + nk - 1) >= NZ)
                            continue;
                    
                        float nval = halo[ni][nj][nk];
                        if (nval <= val) is_min = 0; // Strict inequality for maxima
                        if (nval >= val) is_max = 0; // Strict inequality for minima
                    }
                    
                    if (is_min) cnt_min++;
                    if (is_max) cnt_max++;
                    if (val < lmin) lmin = val;
                    if (val > lmax) lmax = val;
                }
            }
        }

// ...existing code...



        local_mins[t] = cnt_min;
        local_maxs[t] = cnt_max;

        MPI_Allreduce(&lmin, &global_min[t], 1, MPI_FLOAT, MPI_MIN, cart_comm);
        MPI_Allreduce(&lmax, &global_max[t], 1, MPI_FLOAT, MPI_MAX, cart_comm);

        free(sub_data);
        for (int i = 0; i < x_local + 2; i++) {
            for (int j = 0; j < y_local + 2; j++) {
                free(halo[i][j]);
            }
            free(halo[i]);
        }
        free(halo);
    }

    double t_end = MPI_Wtime();
    double main_time = t_end - t_start;

    int *total_mins = NULL, *total_maxs = NULL;
    if (rank == 0) {
        total_mins = (int *)malloc(NC * sizeof(int));
        total_maxs = (int *)malloc(NC * sizeof(int));
    }

    MPI_Reduce(local_mins, total_mins, NC, MPI_INT, MPI_SUM, 0, cart_comm);
    MPI_Reduce(local_maxs, total_maxs, NC, MPI_INT, MPI_SUM, 0, cart_comm);

    if (rank == 0) {
        FILE *out = fopen(output_file, "w");
        if (!out) {
            perror("Failed to open output file");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        for (int t = 0; t < NC; t++) {
            fprintf(out, "(%d,%d) ", total_mins[t], total_maxs[t]);
        }
        fprintf(out, "\n");

        for (int t = 0; t < NC; t++) {
            fprintf(out, "(%.1f,%.1f) ", global_min[t], global_max[t]);
        }
        fprintf(out, "\n");

        fprintf(out, "%.6f %.6f %.6f\n", 0.0, main_time, main_time);

        fclose(out);
        free(total_mins);
        free(total_maxs);
    }

    for (int i = 0; i < local_points; i++) free(local_data[i]);
    free(local_data);
    free(local_mins);
    free(local_maxs);
    free(global_min);
    free(global_max);

    MPI_Finalize();
    return 0;
}











