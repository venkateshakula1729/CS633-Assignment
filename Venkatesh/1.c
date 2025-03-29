#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <stdbool.h>
#include "mpi.h"

// Convert 3D coordinates to 1D index
int idx3d(int x, int y, int z, int nx, int ny) {
    return (z * ny + y) * nx + x;
}

// Check if point is local minimum
int is_min(double *data, int x, int y, int z, int nx, int ny, int nz, int t, int nt) {
    int center = idx3d(x, y, z, nx, ny);
    double val = data[center * nt + t];
    
    // Check 6 neighbors (left, right, up, down, front, back)
    int nbrs[6][3] = {{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
    
    for (int i = 0; i < 6; i++) {
        int nx = x + nbrs[i][0];
        int ny = y + nbrs[i][1];
        int nz = z + nbrs[i][2];
        
        // Skip if outside boundaries
        if (nx < 0 || nx >= nx || ny < 0 || ny >= ny || nz < 0 || nz >= nz)
            continue;
            
        int nidx = idx3d(nx, ny, nz, nx, ny);
        if (data[nidx * nt + t] < val)
            return 0;
    }
    
    return 1;
}

// Check if point is local maximum
int is_max(double *data, int x, int y, int z, int nx, int ny, int nz, int t, int nt) {
    int center = idx3d(x, y, z, nx, ny);
    double val = data[center * nt + t];
    
    // Check 6 neighbors (left, right, up, down, front, back)
    int nbrs[6][3] = {{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}};
    
    for (int i = 0; i < 6; i++) {
        int nx = x + nbrs[i][0];
        int ny = y + nbrs[i][1];
        int nz = z + nbrs[i][2];
        
        // Skip if outside boundaries
        if (nx < 0 || nx >= nx || ny < 0 || ny >= ny || nz < 0 || nz >= nz)
            continue;
            
        int nidx = idx3d(nx, ny, nz, nx, ny);
        if (data[nidx * nt + t] > val)
            return 0;
    }
    
    return 1;
}

int main(int argc, char **argv) {
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    // Check command line args
    if (argc != 10) {
        if (rank == 0) {
            printf("Usage: %s input px py pz nx ny nz timesteps output\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }
    
    char *infile = argv[1];
    int px = atoi(argv[2]);
    int py = atoi(argv[3]);
    int pz = atoi(argv[4]);
    int nx = atoi(argv[5]);
    int ny = atoi(argv[6]);
    int nz = atoi(argv[7]);
    int nt = atoi(argv[8]);
    char *outfile = argv[9];
    
    // Validate process count
    if (px * py * pz != size) {
        if (rank == 0) {
            printf("Error: px*py*pz (%d) must equal number of processes (%d)\n", 
                   px*py*pz, size);
        }
        MPI_Finalize();
        return 1;
    }
    
    // Start timing
    double t_start = MPI_Wtime();
    
    // Calculate grid size
    int total_points = nx * ny * nz;
    double *full_data = NULL;
    
    // Read data on rank 0
    if (rank == 0) {
        full_data = malloc(total_points * nt * sizeof(double));
        if (!full_data) {
            printf("Memory allocation failed\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        
        FILE *fp = fopen(infile, "r");
        if (!fp) {
            printf("Cannot open input file: %s\n", infile);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        
        for (int i = 0; i < total_points; i++) {
            for (int t = 0; t < nt; t++) {
                if (fscanf(fp, "%lf", &full_data[i * nt + t]) != 1) {
                    printf("Error reading data\n");
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
            }
        }
        fclose(fp);
    }
    
    // Mark end of read time
    double t_read = MPI_Wtime();
    
    // Determine local domain size
    int local_nx = nx / px;
    int local_ny = ny / py;
    int local_nz = nz / pz;
    
    // Handle non-divisible dimensions
    int px_id = rank % px;
    int py_id = (rank / px) % py;
    int pz_id = rank / (px * py);
    
    int start_x = px_id * local_nx;
    int start_y = py_id * local_ny;
    int start_z = pz_id * local_nz;
    
    int end_x = (px_id + 1 == px) ? nx : start_x + local_nx;
    int end_y = (py_id + 1 == py) ? ny : start_y + local_ny;
    int end_z = (pz_id + 1 == pz) ? nz : start_z + local_nz;
    
    // Adjusted dimensions for this process
    local_nx = end_x - start_x;
    local_ny = end_y - start_y;
    local_nz = end_z - start_z;
    
    // Allocate local data with ghost cells
    int ghost_nx = local_nx + 2;
    int ghost_ny = local_ny + 2;
    int ghost_nz = local_nz + 2;
    int ghost_size = ghost_nx * ghost_ny * ghost_nz;
    
    double *local_data = malloc(ghost_size * nt * sizeof(double));
    if (!local_data) {
        printf("Rank %d: Memory allocation failed\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    // Initialize to extreme values
    for (int i = 0; i < ghost_size * nt; i++) {
        local_data[i] = DBL_MAX;  // For minima check
    }
    
    // Setup MPI derived datatype for distributing data
    MPI_Datatype block_type, resized_type;
    int sizes[3] = {nx, ny, nz};
    int subsizes[3] = {local_nx, local_ny, local_nz};
    int starts[3] = {start_x, start_y, start_z};
    
    MPI_Type_create_subarray(3, sizes, subsizes, starts, MPI_ORDER_C, 
                            MPI_DOUBLE, &block_type);
    MPI_Type_commit(&block_type);
    
    // Scatter data from rank 0 to all
    if (rank == 0) {
        // First copy local data for rank 0
        for (int z = 0; z < local_nz; z++) {
            for (int y = 0; y < local_ny; y++) {
                for (int x = 0; x < local_nx; x++) {
                    int src_idx = idx3d(start_x + x, start_y + y, start_z + z, nx, ny);
                    int dst_idx = idx3d(x + 1, y + 1, z + 1, ghost_nx, ghost_ny);
                    
                    for (int t = 0; t < nt; t++) {
                        local_data[dst_idx * nt + t] = full_data[src_idx * nt + t];
                    }
                }
            }
        }
        
        // Send to other ranks
        for (int r = 1; r < size; r++) {
            int r_px = r % px;
            int r_py = (r / px) % py;
            int r_pz = r / (px * py);
            
            int r_sx = r_px * (nx / px);
            int r_sy = r_py * (ny / py);
            int r_sz = r_pz * (nz / pz);
            
            int r_ex = (r_px + 1 == px) ? nx : r_sx + (nx / px);
            int r_ey = (r_py + 1 == py) ? ny : r_sy + (ny / py);
            int r_ez = (r_pz + 1 == pz) ? nz : r_sz + (nz / pz);
            
            int r_nx = r_ex - r_sx;
            int r_ny = r_ey - r_sy;
            int r_nz = r_ez - r_sz;
            
            for (int z = 0; z < r_nz; z++) {
                for (int y = 0; y < r_ny; y++) {
                    for (int x = 0; x < r_nx; x++) {
                        int src_idx = idx3d(r_sx + x, r_sy + y, r_sz + z, nx, ny);
                        int buf_idx = (z * r_ny + y) * r_nx + x;
                        
                        // Create a temporary buffer
                        double *send_buf = malloc(r_nx * r_ny * r_nz * nt * sizeof(double));
                        for (int t = 0; t < nt; t++) {
                            send_buf[buf_idx * nt + t] = full_data[src_idx * nt + t];
                        }
                        
                        MPI_Send(send_buf, r_nx * r_ny * r_nz * nt, MPI_DOUBLE, r, 0, MPI_COMM_WORLD);
                        free(send_buf);
                    }
                }
            }
        }
        
        // Free full data since it's no longer needed
        free(full_data);
    } else {
        // Receive data
        int recv_size = local_nx * local_ny * local_nz * nt;
        double *recv_buf = malloc(recv_size * sizeof(double));
        
        MPI_Recv(recv_buf, recv_size, MPI_DOUBLE, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        // Copy received data to local buffer with ghost cells
        for (int z = 0; z < local_nz; z++) {
            for (int y = 0; y < local_ny; y++) {
                for (int x = 0; x < local_nx; x++) {
                    int src_idx = (z * local_ny + y) * local_nx + x;
                    int dst_idx = idx3d(x + 1, y + 1, z + 1, ghost_nx, ghost_ny);
                    
                    for (int t = 0; t < nt; t++) {
                        local_data[dst_idx * nt + t] = recv_buf[src_idx * nt + t];
                    }
                }
            }
        }
        
        free(recv_buf);
    }
    
    // Exchange ghost cells with neighbors (simplified for clarity)
    // In a real implementation, you'd do proper ghost cell exchanges here
    
    // Mark start of computation time
    double t_compute = MPI_Wtime();
    
    // Initialize local results
    int *min_counts = calloc(nt, sizeof(int));
    int *max_counts = calloc(nt, sizeof(int));
    double *min_values = malloc(nt * sizeof(double));
    double *max_values = malloc(nt * sizeof(double));
    
    for (int t = 0; t < nt; t++) {
        min_values[t] = DBL_MAX;
        max_values[t] = -DBL_MAX;
    }
    
    // Process local domain
    for (int z = 0; z < local_nz; z++) {
        for (int y = 0; y < local_ny; y++) {
            for (int x = 0; x < local_nx; x++) {
                // Adjust indices for ghost cells
                int ghost_x = x + 1;
                int ghost_y = y + 1;
                int ghost_z = z + 1;
                int idx = idx3d(ghost_x, ghost_y, ghost_z, ghost_nx, ghost_ny);
                
                for (int t = 0; t < nt; t++) {
                    double val = local_data[idx * nt + t];
                    
                    // Update min/max values
                    if (val < min_values[t]) min_values[t] = val;
                    if (val > max_values[t]) max_values[t] = val;
                    
                    // Count local extrema
                    if (is_min(local_data, ghost_x, ghost_y, ghost_z, ghost_nx, ghost_ny, ghost_nz, t, nt))
                        min_counts[t]++;
                    if (is_max(local_data, ghost_x, ghost_y, ghost_z, ghost_nx, ghost_ny, ghost_nz, t, nt))
                        max_counts[t]++;
                }
            }
        }
    }
    
    // Reduce results across all processes
    int *global_min_counts = NULL;
    int *global_max_counts = NULL;
    double *global_min_values = NULL;
    double *global_max_values = NULL;
    
    if (rank == 0) {
        global_min_counts = malloc(nt * sizeof(int));
        global_max_counts = malloc(nt * sizeof(int));
        global_min_values = malloc(nt * sizeof(double));
        global_max_values = malloc(nt * sizeof(double));
    }
    
    MPI_Reduce(min_counts, global_min_counts, nt, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(max_counts, global_max_counts, nt, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(min_values, global_min_values, nt, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
    MPI_Reduce(max_values, global_max_values, nt, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    
    // End computation time
    double t_end = MPI_Wtime();
    
    // Calculate timing
    double read_time = t_read - t_start;
    double compute_time = t_end - t_compute;
    double total_time = t_end - t_start;
    
    // Gather max timing across all processes
    double max_read_time, max_compute_time, max_total_time;
    MPI_Reduce(&read_time, &max_read_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&compute_time, &max_compute_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&total_time, &max_total_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    
    // Write output file from rank 0
    if (rank == 0) {
        FILE *fp = fopen(outfile, "w");
        if (!fp) {
            printf("Cannot open output file: %s\n", outfile);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        
        // Write minima and maxima counts
        for (int t = 0; t < nt; t++) {
            fprintf(fp, "(%d, %d)", global_min_counts[t], global_max_counts[t]);
            if (t < nt - 1) fprintf(fp, ", ");
        }
        fprintf(fp, "\n");
        
        // Write min and max values
        for (int t = 0; t < nt; t++) {
            fprintf(fp, "(%g, %g)", global_min_values[t], global_max_values[t]);
            if (t < nt - 1) fprintf(fp, ", ");
        }
        fprintf(fp, "\n");
        
        // Write timing information
        fprintf(fp, "%g, %g, %g\n", max_read_time, max_compute_time, max_total_time);
        
        fclose(fp);
        printf("Output written to %s\n", outfile);
        
        // Free memory
        free(global_min_counts);
        free(global_max_counts);
        free(global_min_values);
        free(global_max_values);
    }
    
    // Free local memory
    free(local_data);
    free(min_counts);
    free(max_counts);
    free(min_values);
    free(max_values);
    
    MPI_Type_free(&block_type);
    MPI_Finalize();
    return 0;
}
