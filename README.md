# 🛤️ Parallel Zip (`pzip`)

## About

Parallel Zip (`pzip`) is a multi-threaded program that compresses a list of
input files specified in the command line arguments using Run Length Encoding
(RLE). It implements **locks** and **semaphores** to ensure multiple threads
can safely access a shared unbounded buffer. Additional semaphores are also used
to order the output (print in the same order as the input list).

<sub>More information can be found [here](/assignment/Project3_para_zip.pdf).</sub>

## Team members and contribution

- Gary Tou ([@garyhtou](https://github.com/garyhtou))
- Castel Villalobos ([@impropernoun](https://github.com/impropernoun))
- Hank Rudolph ([@hankrud](https://github.com/HankRud))

## Design Considerations

### How to parallelize the compression?

We used multiple threads to compress the file. This allows us to run the
compression algorithm in parallel. In addition, we saved this compressed data in
memory to decrease the amount of time spent in the ordering semaphores' critical
section.

### How to determine how many threads to create?

Using `get_nprocs()`, we can determine the number of processors available on the
system. This number is then used as the max thread limit (unless the system does
not support multiple cores — which it would then default to 5). The program will
not create more threads than needed (except for the 5 default threads).

### How to efficiently perform each piece of work?

By memory mapping input files, compressing files using a thread pool, storing
compressed data in memory until their turn to print, we are able to efficiently
perform each piece of work in parallel.

### How to access the input file efficiently?

Memory mapping was the way we efficiently accessed the input files.

### How to coordinate multiple threads?

We used a lock to protect shared data (queue). A semaphore to prevent job
runners from running when the queue is empty. And multiple semaphores to order
the printing output.

### How to terminate consumer threads in the thread pool?

We created a `kill` boolean in the job struct. Whenever a job runner receives a
new job, it will check the `kill` boolean. If `kill` is `true`, we killed the
thread and exited appropriately.

## Strengths and Weaknesses

Strengths:

- Parallelizes the compression algorithm
- Saves compressed data to memory before printing
  - Prevents computation bottleneck
- Faster than wzip
- Handles potential system call errors

Weaknesses:

- Only one thread per file
