import re
from datetime import datetime

import matplotlib.pyplot as plt
import matplotlib.dates as mdates

# List of tuples: (file_path, legend_name)
LOG_FILES = [
    ("battery_log_0_10.txt", "0% Screen Brightness, 10 seconds"),
    ("battery_log_0.txt", "0% Screen Brightness"),
    # ("battery_log_10_10.txt", "10% Screen Brightness, 10 seconds"),
    # ("battery_log_10.txt", "10% Screen Brightness"),
    # ("battery_log_50_10.txt", "50% Screen Brightness, 10 seconds"),
    # ("battery_log_max_10.txt", "100% Screen Brightness, 10 seconds"),
    # Add more files here as needed
]

# Regex to extract timestamp + battery level
pattern = re.compile(
    r"\[(.*?)\] Battery Level: (\d+)"
)

# Plot
plt.figure(figsize=(12, 6))

for file_path, legend_name in LOG_FILES:
    times = []
    levels = []

    # Read and parse file
    try:
        with open(file_path, "r") as f:
            for line in f:
                match = pattern.search(line)
                if match:
                    timestamp_str = match.group(1)
                    level = int(match.group(2))

                    timestamp = datetime.fromisoformat(
                        timestamp_str.replace("Z", "+00:00")
                    )

                    times.append(timestamp)
                    levels.append(level)
    except FileNotFoundError:
        print(f"Warning: {file_path} not found. Skipping...")
        continue

    # Normalize time to start at 00:00:00
    if times:
        t0 = times[0]
        base_date = datetime(1970, 1, 1) # arbitrary base date
        times = [base_date + (t - t0) for t in times]

    plt.plot(times, levels, marker='o', markersize=3, label=legend_name)

plt.title("Battery Level Over Time")
plt.xlabel("Elapsed Time (HH:MM:SS)")
plt.ylabel("Battery Level")

# Better time formatting
plt.gca().xaxis.set_major_formatter(
    mdates.DateFormatter('%H:%M:%S')
)

plt.xticks(rotation=45)
plt.grid(True)
plt.legend()

plt.tight_layout()
plt.show()