#!/usr/bin/env python3
"""
Data Generator for Time Series Parallel Processing
Creates synthetic 3D time series data files for testing parallel implementations.

Usage: python generate_data.py nx ny nz timesteps [output_file] [--pattern {wave|random|blend}]

The output is a binary file containing float32 values, with each grid point's time series
stored sequentially.
"""

import numpy as np
import argparse
import os
import time
from math import sin, cos, exp, sqrt

def generate_wave_pattern(nx, ny, nz, timesteps, noise_level=0.2):
    """Generate data with wave patterns that evolve over time."""
    print(f"Generating wave pattern data ({nx}x{ny}x{nz}, {timesteps} timesteps)...")

    # Create a 4D array (x, y, z, time)
    data = np.zeros((nx, ny, nz, timesteps))

    # Parameters for the waves
    freq_x = 2.0 * np.pi / nx
    freq_y = 2.0 * np.pi / ny
    freq_z = 2.0 * np.pi / nz
    freq_t = 2.0 * np.pi / timesteps

    # Generate the data
    start_time = time.time()
    for t in range(timesteps):
        if t % max(1, timesteps // 10) == 0:
            print(f"  Processing timestep {t+1}/{timesteps}...")

        for x in range(nx):
            for y in range(ny):
                for z in range(nz):
                    # Base wave pattern
                    value = 10 * sin(x * freq_x + t * 0.1) + \
                            8 * cos(y * freq_y + t * 0.15) + \
                            6 * sin(z * freq_z + t * 0.2) + \
                            4 * cos((x + y + z) * 0.1 + t * freq_t)

                    # Add random noise
                    noise = np.random.normal(0, noise_level)
                    data[x, y, z, t] = value + noise * 5

    print(f"Generation completed in {time.time() - start_time:.2f} seconds")
    return data

def generate_random_data(nx, ny, nz, timesteps, smoothness=2.0):
    """Generate random data with spatial and temporal coherence."""
    print(f"Generating random data ({nx}x{ny}x{nz}, {timesteps} timesteps)...")

    # Generate random base values for the grid
    data = np.zeros((nx, ny, nz, timesteps))

    # Initial random field
    base_field = np.random.normal(0, 10, (nx, ny, nz))

    # Smooth the base field
    if smoothness > 0:
        from scipy.ndimage import gaussian_filter
        base_field = gaussian_filter(base_field, sigma=smoothness)

    # Generate time evolution
    start_time = time.time()
    for t in range(timesteps):
        if t % max(1, timesteps // 10) == 0:
            print(f"  Processing timestep {t+1}/{timesteps}...")

        # Create a slightly different field for each timestep
        time_offset = np.random.normal(0, 2, (nx, ny, nz))
        time_field = base_field + time_offset * (t / timesteps) + np.sin(t * 0.5) * 3

        if smoothness > 0:
            from scipy.ndimage import gaussian_filter
            time_field = gaussian_filter(time_field, sigma=smoothness * 0.5)

        data[:, :, :, t] = time_field

    print(f"Generation completed in {time.time() - start_time:.2f} seconds")
    return data

def generate_blend_data(nx, ny, nz, timesteps):
    """Generate data that blends wave patterns with randomness."""
    print(f"Generating blended data ({nx}x{ny}x{nz}, {timesteps} timesteps)...")

    # Generate both types of data
    wave_data = generate_wave_pattern(nx, ny, nz, timesteps, noise_level=0.1)
    random_data = generate_random_data(nx, ny, nz, timesteps, smoothness=1.5)

    # Blend data together
    blend_ratio = np.random.uniform(0.3, 0.7)
    data = wave_data * blend_ratio + random_data * (1 - blend_ratio)

    return data

def write_data_file(data, output_file):
    """Write the generated data to a binary file containing float32 values."""
    nx, ny, nz, timesteps = data.shape
    total_points = nx * ny * nz

    # Make sure the output file has .bin extension
    if not output_file.endswith('.bin'):
        output_file = output_file.replace('.txt', '.bin') if output_file.endswith('.txt') else output_file + '.bin'

    print(f"Writing binary data to {output_file}...")
    start_time = time.time()

    # Convert data to float32 to save space
    data_float32 = data.astype(np.float32)

    # Reshape to have each grid point's time series as a row
    # This reshapes from (nx, ny, nz, timesteps) to (nx*ny*nz, timesteps)
    reshaped_data = data_float32.reshape(nx * ny * nz, timesteps)

    # Write to binary file
    with open(output_file, 'wb') as f:
        reshaped_data.tofile(f)

    file_size_mb = os.path.getsize(output_file) / (1024 * 1024)
    print(f"Binary file written successfully in {time.time() - start_time:.2f} seconds")
    print(f"Output file size: {file_size_mb:.2f} MB")

def main():
    parser = argparse.ArgumentParser(description='Generate synthetic 3D time series data for parallel processing.')
    parser.add_argument('nx', type=int, help='Number of grid points in X dimension')
    parser.add_argument('ny', type=int, help='Number of grid points in Y dimension')
    parser.add_argument('nz', type=int, help='Number of grid points in Z dimension')
    parser.add_argument('timesteps', type=int, help='Number of time steps')
    parser.add_argument('output_file', nargs='?',
                        default=None,
                        help='Output file name (default: data_NX_NY_NZ_TIMESTEPS.txt)')
    parser.add_argument('--pattern', choices=['wave', 'random', 'blend'],
                        default='random',
                        help='Data pattern to generate (default: blend)')

    args = parser.parse_args()

    # Validate input
    if args.nx <= 0 or args.ny <= 0 or args.nz <= 0 or args.timesteps <= 0:
        print("Error: All dimensions and timesteps must be positive integers")
        return 1

    # Create output file name if not specified
    if args.output_file is None:
        args.output_file = f"../data/art_data_{args.nx}_{args.ny}_{args.nz}_{args.timesteps}.bin"

    print(f"Generating {args.pattern} data with dimensions: {args.nx}x{args.ny}x{args.nz} and {args.timesteps} timesteps")

    # Generate data based on the requested pattern
    if args.pattern == 'wave':
        data = generate_wave_pattern(args.nx, args.ny, args.nz, args.timesteps)
    elif args.pattern == 'random':
        data = generate_random_data(args.nx, args.ny, args.nz, args.timesteps)
    else:  # blend
        data = generate_blend_data(args.nx, args.ny, args.nz, args.timesteps)

    # Write data to file
    write_data_file(data, args.output_file)

    print(f"Done! Data file created: {args.output_file}")
    return 0

if __name__ == "__main__":
    main()
