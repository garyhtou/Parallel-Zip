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
void addJob(int num, bool kill, char *filepath, sem_t *prev_sem, sem_t *next_sem);
// Information to be passed to the job runners (child thread)
struct job_t
{
	int num;	 // The job's ID
	bool kill; // If true, the child process will gracefully exit (ignoring all other fields and jobs)
	char *filepath;
	sem_t *prev_sem;
	sem_t *next_sem;
};

// Data structure for holding the results of a memory mapped file
struct mem_map_t
{
	bool success;
	char *mmap;
	off_t f_size;
};

// A queue of jobs that the thread pool will run
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
	// prevent creating more threads than there are files. Default to at least 5
	// threads (as per the instructions).
	int num_threads = max(min(argc - 1, get_nprocs()), 5);

	// Create the thread pool
	vector<pthread_t> tids = startThreadPool(num_threads);

	// Array to hold semaphores
	vector<sem_t *> sems;
	// Variables to hold the current working prev/next semaphores
	sem_t *prev_sem = NULL;
	sem_t *next_sem = NULL;

	// Loop through the filepaths provided in argv
	for (int i = 1; i < argc; i++)
	{
		// If single file, no need for semaphore
		if (i == 1 && argc == 2)
		{
			addJob(i, false, argv[i], NULL, NULL);
			continue;
		}

		// Create new semaphore and intialize it
		next_sem = new sem_t;
		sem_init(next_sem, 0, 0);
		sems.push_back(next_sem);

		// First file
		if (i == 1)
		{
			// No previous sem
			addJob(i, false, argv[i], NULL, next_sem);
		}
		// Last file
		else if (i == argc - 1)
		{
			// No next sem
			addJob(i, false, argv[i], prev_sem, NULL);
		}
		// Middle file
		else
		{
			addJob(i, false, argv[i], prev_sem, next_sem);
		}

		prev_sem = next_sem;
	}

	// Gracefully end threads after they finish the job queue
	stopThreadPool(tids);

	// Destroy semaphore
	for (size_t i = 0; i < sems.size(); i++)
	{
		sem_destroy(sems[i]);
	}
}

mem_map_t mmapFile(const char *filepath)
{
	try
	{
		mem_map_t map;

		int fd = open(filepath, O_RDONLY, S_IRUSR | S_IWUSR);
		if (fd < 0)
		{
			map.success = false;
			map.mmap = NULL;
			map.f_size = 0;
			return map;
		}

		// Get size of file
		struct stat sb;
		fstat(fd, &sb);

		// Mapping file into virtual memory
		char *mmapFile = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (mmapFile == MAP_FAILED)
		{
			exit(1);
		}

		// Set up map results to be returned
		map.success = true;
		map.mmap = mmapFile;
		map.f_size = sb.st_size;

		return map;
	}
	catch (...)
	{
		// Failed to memory map file
		mem_map_t map;
		map.success = false;
		return map;
	}
}

// This function will add kill requests to the job queue and return once all
// threads have quit.
void stopThreadPool(vector<pthread_t> tids)
{
	// Add kill request jobs
	for (size_t i = 0; i < tids.size(); i++)
	{
		addJob(-1, true, NULL, NULL, NULL);
	}

	// Wait for all threads to finish
	for (size_t i = 0; i < tids.size(); i++)
	{
		if (pthread_join(tids[i], (void **)NULL) != 0)
		{
			cout << "Filed to join thread." << endl;
			exit(1);
		}
	}
}

// Create the thread pool
vector<pthread_t> startThreadPool(int num_threads)
{
	vector<pthread_t> tids;

	// Thread creation retry logic to make this program more robust
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
				cout << "Failed to create thread" << endl;
				exit(1);
			}
		}
		else
		{
			tids.push_back(tid);
		}
	}

	return tids;
}

void *job_runner(void *)
{
	// This function serves as a job runner is in the thread pool, so we run until
	// explicitly terminated.
	while (1)
	{
		// Wait until there is at least one job in the queue
		sem_wait(&full);

		job_t job;

		// Aquire lock for queue
		mtx.lock();

		// Get the next job
		job = jobs.front();
		jobs.pop();

		// Release the lock
		mtx.unlock();

		// Check if the job is a kill request
		if (job.kill)
		{
			pthread_exit(0);
		}

		// MEMORY MAP FILE
		mem_map_t map = mmapFile(job.filepath);
		if (!map.success)
		{
			// Wait and post semaphores then go back to thread pool
			if (job.prev_sem)
			{
				sem_wait(job.prev_sem);
			}
			if (job.next_sem)
			{
				sem_post(job.next_sem);
			}

			// continue, don't return. Otherwise, this thread will leave the pool.
			continue;
		}

		// Create buffer to hold compressed data
		char *buff = (char *)malloc(5 * map.f_size);
		int buffIndex = 0;

		// Process the job (compress the file)
		int count = 0;
		char last;
		for (off_t i = 0; i < map.f_size; i++)
		{
			// Compress the last "count" equivalent characters
			if (count && map.mmap[i] != last)
			{
				buff[buffIndex++] = count & 0xff;
				buff[buffIndex++] = (count >> 8) & 0xff;
				buff[buffIndex++] = (count >> 16) & 0xff;
				buff[buffIndex++] = (count >> 24) & 0xff;
				buff[buffIndex++] = last;
				count = 0;
			}
			last = map.mmap[i];
			count++;
		}

		if (count)
		{
			buff[buffIndex++] = count & 0xff;
			buff[buffIndex++] = (count >> 8) & 0xff;
			buff[buffIndex++] = (count >> 16) & 0xff;
			buff[buffIndex++] = (count >> 24) & 0xff;
			buff[buffIndex++] = last;
			count = 0;
		}

		// Wait for the previous job to finish printing
		if (job.prev_sem != NULL)
		{
			sem_wait(job.prev_sem);
		}

		// Write the buffer to stdout
		fwrite(buff, sizeof(char), (size_t)buffIndex, stdout);

		// Signal the next thread
		if (job.next_sem != NULL)
		{
			sem_post(job.next_sem);
		}

		// Deallocating memory for mmap
		if (munmap(map.mmap, map.f_size) < 0)
		{
			cout << "munmap fail" << endl;
			exit(1);
		}

		//  DO NOT RETURN, otherwise, this thread will leave the thread pool
	}
}

// Creates and adds a job to the job queue
void addJob(int num, bool kill, char *filepath, sem_t *prev_sem, sem_t *next_sem)
{
	// Create struct instance
	job_t job;
	job.num = num;
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