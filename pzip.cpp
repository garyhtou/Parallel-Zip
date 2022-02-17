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
void queueFile(string filepath, sem_t *prev_sem, sem_t *next_sem);
void addJob(bool kill, char *file, off_t size, sem_t *prev_sem, sem_t *next_sem);

// Information to be passed to the job runners (child thread)
struct job_t
{
	bool kill; // If true, the child process will gracefully exit (ignoring all other fields and jobs)
	off_t fileSize;
	char *file;
	sem_t *prev_sem;
	sem_t *next_sem;
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

	//get the number of threads 
    int num_thrd = get_n_procs();

	vector<pthread_t> tids = startThreadPool(num_thrd);

	// Queue filepaths
	// Semaphore index 0 is not used to make the indecies in the code below nicer.
	vector<sem_t> sems;
	// TODO: create semaphores for ordering
	// sems.push_back();

	// First file
	sem_init(&sems[1], 0, 0);
	queueFile(argv[1], NULL, &sems[1]);
	cout << "first file" << endl;

	// Second to second to last file
	for (int i = 2; i < argc - 1; i++)
	{
		sem_init(&sems[i], 0, 0);
		queueFile(argv[i], &sems[i - 1], &sems[i]);
		cout << "middle file " << i << endl;
	}

	// Last file
	if (argc > 1) // Check that the last file exists
	{
		// Last file doesn't need to create a new semaphore (and post to it). It
		// just needs to wait for the previous.
		queueFile(argv[argc], &sems[argc - 1], NULL);
		cout << "last file" << endl;
	}

	// Gracefully end threads after they finish the job queue
	stopThreadPool(tids);
}

// This function will add kill requests to the job queue and return once all
// threads have quit.
void stopThreadPool(vector<pthread_t> tids)
{
	// Add kill request jobs
	for (size_t i = 0; i < tids.size(); i++)
	{
		addJob(true, NULL, 0, NULL, NULL);
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



	int retry = 0;
	for (int i = 0; i < num_threads; i++)
	{
		pthread_t tid;
		if (pthread_create(&tid, NULL, job_runner, NULL))
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
		cout << "GOT A JOB";

		// Aquire lock for queue
		job_t job;
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

		// Process the job
		// This wzip code is largely based on Professor Zhu's solution for Project 1
		int count = 0;
		char last;
		sem_wait(job.prev_sem);
		for (int i = 0; i < job.fileSize; i++)
		{
			// printf("\tCURRENT CHAR: %c\n", job.file[i]);
			if (count && job.file[i] != last)
			{
				cout.write((char *)&count, sizeof(int));
				cout.write((char *)&last, 1);
				count = 0;
			}
			last = job.file[i];
			count++;
		}

		if (count)
		{
			cout.write((char *)&count, sizeof(int));
			cout.write((char *)&last, 1);
		}
		sem_post(job.next_sem);

		// TODO: deallocate memory for mmap? (memory leak)
	}
}

void queueFile(string filepath, sem_t *prev_sem, sem_t *next_sem)
{
	// cout << "queuing file " << filepath << endl;

	int fd = open(filepath.c_str(), O_RDONLY, S_IRUSR | S_IWUSR);
	struct stat sb;

	// Grabbing size of sb, stored in sb.st_size
	if (fstat(fd, &sb) == -1)
	{
		perror("could not get file size\n");
	}

	// Mapping file into virtual memory
	char *mmapFile = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

	// Create job
	addJob(false, mmapFile, sb.st_size, prev_sem, next_sem);
}

void addJob(bool kill, char *file, off_t size, sem_t *prev_sem, sem_t *next_sem)
{
	// Create struct
	job_t job;
	job.kill = kill;
	job.file = file;
	job.fileSize = size;
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