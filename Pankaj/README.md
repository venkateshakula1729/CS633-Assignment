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
![Alt text](assets/csv_results.png)
