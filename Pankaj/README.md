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
(24159, 23308), (24159, 23308), (24159, 23308)
(-51.45, 33.63), (-51.45, 33.63), (-51.45, 33.63)
0.192183, 0.014977, 0.21126
```
