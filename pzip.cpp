// PSUEDOCODE
// Parse stdin for file names

// num_threads = MIN(num_cpu, num_cpu)
// Start pthread


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
int testing = 0;

// Information to be passed to the job runners (child thread)
struct job_t
{
	bool kill; // If true, the child process will gracefully exit (ignoring all other fields and jobs)
	char *filepath;
	sem_t *prev_sem;
	sem_t *next_sem;
};

struct mem_map_t
{
	bool success;
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
			addJob(false, argv[i], NULL, &sems[i - 1]);
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
			addJob(false, argv[i], &sems[i - 2], &sems[i - 1]);
		}
	}

	// Gracefully end threads after they finish the job queue
	stopThreadPool(tids);
}

//All
mem_map_t mmapFile(const char *filepath)
{
	if (testing == 1)
		cout << "queuing file " << filepath << endl;

	try
	{
		mem_map_t map;

		int fd = open(filepath, O_RDONLY, S_IRUSR | S_IWUSR);

		// was not getting caught
		if (fd < 0)
		{
			map.success = false;
			map.mmap = NULL;
			map.f_size = 0;
			return map;
		}

		struct stat sb;

		// Grabbing size of sb, stored in sb.st_size
		if (fstat(fd, &sb) == -1)
		{
			perror("could not get file size\n");
		}

		// Mapping file into virtual memory
		char *mmapFile = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

		// Create job

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
	// Add kill request jobs. This single kill job will be shared by all threads
	// in the queue
	if (testing == 1)
		cout << "ADDING KILL JOB" << endl;
	addJob(true, NULL, NULL, NULL);

	// Wait for all threads to finish
	for (size_t i = 0; i < tids.size(); i++)
	{
		pthread_join(tids[i], (void **)NULL);
	}
}

vector<pthread_t> startThreadPool(int num_threads)
{
	vector<pthread_t> tids;

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
				if (testing == 1)
				{
					cout << "Error creating thread" << endl;
				}

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
		if (testing == 1)
			cout << "GOT A JOB" << endl;

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
			if (testing == 1)
			{
				cout << "Killing job" << endl;
			}
			pthread_exit(0);
		}
		else
		{
			jobs.pop();
			mtx.unlock();
		}

		// MEMORY MAP FILE
		mem_map_t map = mmapFile(job.filepath);
		if (!map.success)
		{
			if (testing == 1)
				cout << "MMap failed, ignoring job" << endl;
			// Memory mapping file, ignore this job

			// Wait on semaphores then go back to thread pool
			if (job.prev_sem)
			{
				if (testing == 1)
					cout << "MMap failed, waiting on prev sem" << endl;
				sem_wait(job.prev_sem);
			}

			if (job.next_sem)
			{
				if (testing == 1)
					cout << "MMap failed, posting next sem" << endl;
				sem_post(job.next_sem);
			}

			// continue, don't return. Otherwise, this thread will leave the pool.
			continue;
		}

		// TODO: Create buffer
		char *buff = (char *)malloc(5 * map.f_size);
		int buffIndex = 0;

		// Process the job
		// This wzip code is largely based on Professor Zhu's solution for Project 1
		int count = 0;
		char last;

		for (off_t i = 0; i < map.f_size; i++)
		{
			
			if (count && map.mmap[i] != last)
			{
				// cout.write((char *)&count, sizeof(int)

				buff[buffIndex++] = count;
				buff[buffIndex++] = count >> 8;
				buff[buffIndex++] = count >> 16;
				buff[buffIndex++] = count >> 24;
				buff[buffIndex++] = last;
				count = 0;
			}
			last = map.mmap[i];
			count++;
		}

		if (count)
		{
			buff[buffIndex++] = count;
			buff[buffIndex++] = count >> 8;
			buff[buffIndex++] = count >> 16;
			buff[buffIndex++] = count >> 24;
			buff[buffIndex++] = last;
			count = 0;
		}

		if (job.prev_sem != NULL)
		{
			if (testing == 1)
			{
				cout << "waiting on previous semn" << endl;
			}
			sem_wait(job.prev_sem);
		}

		fwrite(buff, sizeof(char), (size_t)buffIndex, stdout);

		if (job.next_sem != NULL)
		{
			if (testing == 1)
			{
				cout << "posting next sem" << endl;
			}
			sem_post(job.next_sem);
		}
		// TODO: deallocate memory for mmap? (memory leak)
		
		//delete [] map.mmap;
		// DO NOT RETURN, otherwise, this thread will leave the thread pool
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
	if (testing == 1)
	{
		cout << "Getting mtx lock" << endl;
	}
	mtx.lock();
	// Add the new job to the queue
	jobs.push(job);

	// Release lock
	if (testing == 1)
		cout << "unlocking mtx" << endl;

	mtx.unlock();

	// Make job runnable by posting to semaphore
	sem_post(&full);
}