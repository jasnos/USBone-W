Import("env")
import os

# This script checks if the partition table exists
# and creates a symlink if needed for PlatformIO

partition_file = "default_16MB.csv"
if os.path.exists(partition_file):
    print(f"Using custom partition table: {partition_file}")
else:
    print(f"Warning: Partition table {partition_file} not found!")
