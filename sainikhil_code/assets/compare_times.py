import matplotlib.pyplot as plt

# File paths
files = [
    "out_7_code7newbin.txt",
    "out_7_parallel.txt"
]

# Labels for the files
labels = ["Code7", "parallelIO_Subarray.c"]

# Data storage
data_distribution_times = []
code_times = []
total_times = []

# Read data from files
for file in files:
    with open(file, "r") as f:
        lines = f.readlines()
        # Extract the third line (timing information)
        times = list(map(float, lines[2].strip().split(", ")))
        data_distribution_times.append(times[0])
        code_times.append(times[1])
        total_times.append(times[2])

# Plotting
x = range(len(files))

plt.figure(figsize=(10, 6))

# Plot each time category
plt.plot(x, data_distribution_times, label="Data Distribution Time", marker="o")
plt.plot(x, code_times, label="Code Time", marker="o")
plt.plot(x, total_times, label="Total Time", marker="o")

# Add labels and legend
plt.xticks(x, labels)
plt.xlabel("Files")
plt.ylabel("Time (seconds)")
plt.title("Comparison of Times Across Files")
plt.legend()
plt.grid(True)

# Show the plot
plt.tight_layout()
plt.show()
