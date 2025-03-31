# CS633-Assignment

## Compilation
To compile the code use:

```bash
mpicc -o pankaj_code2 pankaj_code2.c
```

## Running the Program
To run the code use:

```bash
mpirun -np 8 ./pankaj_code2 test.txt 2 2 2 64 64 64 3 out.txt
```

## Expected Output
results will be stored in `out.txt` The output i got for above command is :

```
(38042, 38054), (37893, 38073), (37861, 38051)
(-48.25, 33.63), (-51.45, 33.35), (-48.55, 33.35)
0.13338, 0.0199329, 0.146658
```
## global minima and maxima verified from csv.
The output has been verified against the original data source:
![Alt text](assets/csv_results.png)

## Performance Comparison
I've created several implementations with different optimization techniques.  results comparing without and with file reading optimization.

![Performance Comparison](assets/code4vs7.png)

results comparing without and with memcpy

![Performance Comparison](assets/code7vs9.png)

results comparing with MPI_Send and with MPI_Bsend

![Performance Comparison](assets/code7vs10.png)

results comparing with MPI_Send and with MPI_Isend

![Performance Comparison](assets/code7vs11.png)

results



The performance testing was conducted using the benchmark script (`benchmark.sh`), which runs each implementation 20 times and calculates average performance metrics.


### Performance Testing Tools
Two utility scripts were created for performance analysis:
- `benchmark.sh`: Runs multiple iterations of each implementation and calculates average performance
- `plot_results.py`: Creates visualizations of the performance data

To run the performance comparison yourself:
```bash
# Run the benchmark
./benchmark.sh

# Generate the performance visualization
python3 plot_results.py
```
