import re
from datetime import datetime

import matplotlib.pyplot as plt
import matplotlib.dates as mdates

# Path to your log file
LOG_FILE = "battery_log.txt"

# Regex to extract timestamp + battery level
pattern = re.compile(
    r"\[(.*?)\] Battery Level: (\d+)"
)

times = []
levels = []

# Read and parse file
with open(LOG_FILE, "r") as f:
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

# Plot
plt.figure(figsize=(12, 6))
plt.plot(times, levels, marker='o', markersize=3)

plt.title("Battery Level Over Time")
plt.xlabel("Time")
plt.ylabel("Battery Level")

# Better time formatting
plt.gca().xaxis.set_major_formatter(
    mdates.DateFormatter('%H:%M:%S')
)

plt.xticks(rotation=45)
plt.grid(True)

plt.tight_layout()
plt.show()