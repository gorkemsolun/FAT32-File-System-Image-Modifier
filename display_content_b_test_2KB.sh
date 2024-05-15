#!/bin/bash

measure_time() {
    echo "Running command: $@"
    start=$(date +%s.%N)
    "$@"
    end=$(date +%s.%N)
    runtime=$(echo "$end - $start" | bc)
    echo "Time taken: $runtime seconds"
}

# Define the values array
values=(5 10 15 20 25 30)

# Loop through each value in the array
for ((i = 0; i < ${#values[@]}; i++)); do

    # Repeated create and write operations (adjust count as needed)
    for ((j = 0; j < ${values[i]}; j++)); do
        ./fatmod disk1 -c "test${j}.txt"
        ./fatmod disk1 -w "test${j}.txt" 0 2000 "65"
    done

    count=${values[i]}
    measure_time bash -c "
    count=$count
    for ((j = 0; j < count; j++)); do
        ./fatmod disk1 -r -b \"test\${j}.txt\"
    done
    "
    
    # Clean up created files (adjust if needed)
    for ((j = 0; j < ${values[i]}; j++)); do
        ./fatmod disk1 -d "test${j}.txt"
    done

done


echo "Test script completed."
