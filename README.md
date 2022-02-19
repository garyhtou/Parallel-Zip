# CPSC 3500 Project 3

> **Date:** February 18th, 2022

> **Assignment:** https://seattleu.instructure.com/courses/1601766/assignments/6998814

## Team members and contribution

- Gary Tou ([@garyhtou](https://github.com/garyhtou))
- Castel Villalobos ([@impropernoun](https://github.com/impropernoun))
- Hank Rudolph ([@hankrud](https://github.com/HankRud))

## Design Considerations
### How to parallelize the compression? 
We used multiple threads to compress the file. This allows us to run the compression
algorithm in parallel. In addition, we saved this compressed data in memory to
decrease the amount in the ordering semaphores' critical section.

### How to determine how many threads to create?
Using `get_nprocs()`, we can determine the number of processors available on the
system. Then we use this number as a max limit (unless the system does not support
multiple cores — which it would then default to 5). The program will not create
more threads than needed (except for the 5 default threads).

### How to efficiently perform each piece of work? 
By memory mapping and compressing the file using a thread pool, we
are able to efficiently perform each piece of work in parallel.

### How to access the input file efficiently?
Memory mapping was the way we efficiently accessed the input files.

### How to coordinate multiple threads?
We used a lock to protect shared data (queue). A semaphore to prevent job runners
from running when the queue is empty. And multiple semaphores to order the printing
output.

### How to terminate consumer threads in the thread pool?
We created a kill bool in the job struct. If kill was true in job_runner, we killed
the thread and exited appropriately.

## Strengths and Weaknesses

Strengths:
- Parallelizes the compression algorithm
- Saves compressed data to memory before printing
  - Prevents computation bottleneck
- Faster than wzip
- Handles potential system call errors

Weaknesses:
- Only one thread per file
