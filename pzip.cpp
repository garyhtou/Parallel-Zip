// PSUEDOCODE
// Parse stdin for file names

// num_threads = MIN(num_cpu, num_cpu)
// Start pthread

// mmap files (somehow?)
// for each file, add a job to the queue

// need lock around critical section (reading or writing from the queue/buffer)
// semaphores:
//   - jobs
//   - semaphore for ordering (one per N-1 threads?) — array for semaphore

// Parent (producer)
// aquire lock
// add job to queue
// release lock
// sem_post(&jobs)

// Child (consumer)
// sem_wait(&jobs)
// aquire lock
// read job from queue
// remove job from queue
// release lock
// if job is "go to hell job" then gracefully dieeeeee
// do compression
// sem_wait(&prev_job_sem)
// print
// sem_post(&next_job_sem)

#include <iostream>
#include <fstream>
#include <string>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <queue>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <mutex>
#include <vector>
#include <unistd.h>

using namespace std;

// Forward delcare functions
vector<pthread_t> startThreadPool(int num_threads);
void stopThreadPool(vector<pthread_t> tids);
void *job_runner(void *);
void addJob(bool kill, char *filepath, sem_t *prev_sem, sem_t *next_sem);

// Information to be passed to the job runners (child thread)
struct job_t
{
	bool kill; // If true, the child process will gracefully exit (ignoring all other fields and jobs)
	char *filepath;
	sem_t *prev_sem;
	sem_t *next_sem;
};

//
struct mem_map_t
{
	char *mmap;
	off_t f_size;
};

// A queue of jobs that thread pool will run
queue<job_t> jobs;

// Mutex Lock for critical sections (when using shared queue)
mutex mtx;
// Semaphore to block runners when there are no files in the queue
sem_t full;

int main(int argc, char *argv[])
{
	// Validate that at least one filepath was given
	if (argc <= 1)
	{
		cout << "pzip: file1 [file2 ...]" << endl;
		exit(1);
	}

	// Initalize semaphore
	sem_init(&full, 0, 0);

	// Get the number of processors available. Min() with number of files to
	// prevent creating more threads than there are files.
	int num_threads = min(argc - 1, get_nprocs());

	// fill thread pool
	vector<pthread_t> tids = startThreadPool(num_threads);

	// Queue filepaths and create semaphores for order
	vector<sem_t> sems;
	// TODO: switch to only storing previous sem
	for (int i = 1; i < argc; i++)
	{
		// Create semaphore
		sem_t loopSem;
		sem_init(&loopSem, 0, 0);
		sems.push_back(loopSem);

		// First file
		if (i == 1)
		{
			// No previous sem
			addJob(false, argv[i], NULL, &loopSem);
		}
		// Last file
		else if (i == argc - 1)
		{
			// No next sem
			addJob(false, argv[i], &sems[i - 2], NULL);
		}
		// Middle file
		else
		{
			addJob(false, argv[i], &sems[i - 2], &loopSem);
		}
	}

	// Gracefully end threads after they finish the job queue
	stopThreadPool(tids);
}

mem_map_t mmapFile(const char *filepath)
{
	// cout << "queuing file " << filepath << endl;

	int fd = open(filepath, O_RDONLY, S_IRUSR | S_IWUSR);

	struct stat sb;

	// Grabbing size of sb, stored in sb.st_size
	if (fstat(fd, &sb) == -1)
	{
		perror("could not get file size\n");
	}

	// Mapping file into virtual memory
	char *mmapFile = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

	// Create job
	// addJob(false, fd, sb.st_size, prev_sem, next_sem);
	mem_map_t map;
	map.mmap = mmapFile;
	map.f_size = sb.st_size;

	return map;
}

// This function will add kill requests to the job queue and return once all
// threads have quit.
void stopThreadPool(vector<pthread_t> tids)
{
	// Add kill request jobs
	for (size_t i = 0; i < tids.size(); i++)
	{
		addJob(true, NULL, NULL, NULL);
	}

	// Wait for all threads to finish
	for (size_t i = 0; i < tids.size(); i++)
	{
		pthread_join(tids[i], (void **)NULL);
	}
}

vector<pthread_t> startThreadPool(int num_threads)
{
	vector<pthread_t> tids;

	// cout << num_threads << endl;

	int retry = 0;
	for (int i = 0; i < num_threads; i++)
	{
		pthread_t tid;
		if (pthread_create(&tid, NULL, job_runner, NULL) != 0)
		{

			if (retry < num_threads)
			{
				// Going to retry to create this thread
				retry++;
				i--;
			}
			else
			{
				cout << "Error creating thread" << endl;
				exit(1);
			}
		}
		else
		{
			tids.push_back(tid);
		}
	}

	// cout << "returning thread id's" << endl;
	return tids;
}

void *job_runner(void *)
{
	// This function serves as a job is in a thread pool, so we run until
	// explicitly terminated
	while (1)
	{
		// Wait until there is a job in the queue
		sem_wait(&full);
		// cout << "GOT A JOB" << endl;

		// Aquire lock for queue

		job_t job;
		mtx.lock();

		// cout << "Aquired lock" << endl;
		// Get the next job
		job = jobs.front();
		// Release the lock

		// Check if the job is a kill request
		if (job.kill)
		{
			// Leave "kill" request in the queue to kill other threads
			mtx.unlock();
			pthread_exit(0);
		}
		else
		{
			jobs.pop();
			mtx.unlock();
		}

		// MEMORY MAP FILE
		mem_map_t map = mmapFile(job.filepath);
		// int fd = open(job.filepath, O_RDONLY, S_IRUSR | S_IWUSR);

		// // Grabbing size of file
		// struct stat sb;
		// if (fstat(fd, &sb) == -1)
		// {
		// 	perror("could not get file size\n");
		// }

		// char *mmapFile = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		// cout << "unlocked" << endl;

		// Process the job
		// This wzip code is largely based on Professor Zhu's solution for Project 1
		int count = 0;
		char last;
		// cout << "get job sem" << endl;

		// COMPRESS AND PRINT FILE
		if (job.prev_sem)
		{
			sem_wait(job.prev_sem);
		}

		for (off_t i = 0; i < map.f_size; i++)
		{
			// printf("\tCURRENT CHAR: %c\n", job.file[i]);
			if (count && map.mmap[i] != last)
			{
				// cout.write((char *)&count, sizeof(int));
				fwrite(&count, 4, 1, stdout);
				// cout.write((char *)&last, 1);
				fwrite(&last, 1, 1, stdout);
				count = 0;
			}
			last = map.mmap[i];
			count++;
		}

		if (count)
		{
			cout.write((char *)&count, sizeof(int));
			cout.write((char *)&last, 1);
		}
		if (job.next_sem)
		{
			sem_post(job.next_sem);
		}
		// TODO: deallocate memory for mmap? (memory leak)
	}
}

void addJob(bool kill, char *filepath, sem_t *prev_sem, sem_t *next_sem)
{
	// Create struct
	job_t job;
	job.kill = kill;
	job.filepath = filepath;
	job.prev_sem = prev_sem;
	job.next_sem = next_sem;

	// Aquire lock for queue
	mtx.lock();
	// Add the new job to the queue
	jobs.push(job);

	// Release lock
	mtx.unlock();

	// Make job runnable by posting to semaphore
	sem_post(&full);
}