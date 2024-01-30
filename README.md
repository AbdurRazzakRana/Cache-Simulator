# Project Target
Building a cache simulator and validating its correctness.<be><br>

Cache Simulator: A trace-driven simulator needs to be developed. It will take command-line arguments of<br>
Total cache size<br>
Block size<br>
Unified vs. split I- and D-caches<br>
Associativity<br>
Write back vs. write through<br>
Write allocate vs. write no allocate<br>

Validation: The simulator must output the following counts.<br>
Number of instruction references<br>
Number of data references<br>
Number of instruction misses<br>
Number of data misses<br>
Number of words fetched from memory<br>
Number of words copied back to memory<be>

# Running the Programs
./sim -bs 128 -is 8196 -ds 8196 -a 1 -wb -wa ../traces/spice.trace 

*** Please find the full report under Final Reports ***.
