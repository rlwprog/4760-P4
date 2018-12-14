#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/msg.h>
#include <getopt.h>
#include <string.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include "master.h"

#define SHMCLOCKKEY	86868            	/* Parent and child agree on common key for clock.*/
#define MSGQUEUEKEY	68686            	/* Parent and child agree on common key for msgqueue.*/
#define QUANTUM 100000					/* Max time each process runs in nanoseconds*/
#define PRIORITY 20						/* Likelihood process randomly gets put in high/low priority queue*/
#define MAXOUTPUT 10000					/* Max lines in output file*/
#define CLOCKINCREMENTPERROUND 1000		/* Amount clock increments per round*/

#define PERMS (mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define FLAGS (O_CREAT | O_EXCL)

//globals
static volatile sig_atomic_t doneflag = 0;

static clockStruct *sharedClock;
static clockStruct *forkTime;
static processTable *procTable;

static mymsg_t *toParentMsg;
static int queueid;

int randForkTime;
int childCounter;
int shmclock;
int lenOfMessage;
int totalDispatches;
int totalTimeInSystem;
int totalCpuTimeUsed;

int main (int argc, char *argv[]){

	FILE *logfile;
	logfile = fopen("logfile", "w");

	srand(time(NULL) + getpid());

	int childPid;
	int timeLimit = 2;
	int childLimit = 18;
    int totalChildren = 0;
    int totalLinecount = 0;
    int waitTime = 0;

    totalDispatches = 0;
	childCounter = 0;
 
	sigHandling();
	initPCBStructures();

	alarm(timeLimit);

	setForkTimer();
	

	while(!doneflag){
		if(childCounter < childLimit && checkIfTimeToFork() == 1){

			if ((childPid = fork()) == 0){
				execlp("./worker", "./worker", (char*)NULL);

				fprintf(stderr, "%sFailed exec worker!\n", argv[0]);
				_exit(1);
			}	
	
			childCounter += 1;

			// add process to process table
			toParentMsg->pt = addToProcessTable(childPid);
			toParentMsg->mtype = childPid;
			toParentMsg->priority = decidePriority();
			toParentMsg->burst = 0;
			toParentMsg->pid = childPid;
			toParentMsg->seconds = sharedClock->seconds;
			toParentMsg->nanosecs = sharedClock->nanosecs;

			if (totalLinecount < MAXOUTPUT){
				if (toParentMsg->priority == 0){
					fprintf(logfile, "OSS: Generating process with PID %d (High priority) and putting it in queue 0 at time %d:%d\n", toParentMsg->pt, sharedClock->seconds, sharedClock->nanosecs);

				} else {
					fprintf(logfile, "OSS: Generating process with PID %d (Low priority) and putting it in queue 1 at time %d:%d\n", toParentMsg->pt, sharedClock->seconds, sharedClock->nanosecs);
				}
				totalLinecount += 1;
			}
			msgsnd(queueid, toParentMsg, lenOfMessage, 0);

			totalDispatches += 1;
			totalChildren  += 1;
			setForkTimer();
		}	

		// process in high priority queue
		if(msgrcv(queueid, toParentMsg, lenOfMessage, 2, IPC_NOWAIT) != -1){

			waitTime = diffFromSharedClock(toParentMsg->seconds, toParentMsg->nanosecs);

			if (totalLinecount < MAXOUTPUT){
				fprintf(logfile, "OSS: Dispatching process with PID %d from queue 0 at time %d:%d,\n", toParentMsg->pt, sharedClock->seconds, sharedClock->nanosecs);
				fprintf(logfile, "total time this dispatch was %d nanoseconds\n", waitTime);
				totalLinecount += 2;
			}

			procTable->processes[toParentMsg->pt].totalTimeInSystem += waitTime;
			totalDispatches += 1;

			if (totalLinecount < MAXOUTPUT){
				fprintf(logfile, "OSS: Receiving that process with PID %d ran for %d nanoseconds\n", toParentMsg->pt, toParentMsg->burst);
				if (toParentMsg->burst < (QUANTUM/2)){
					fprintf(logfile, "OSS: not using it's entire time quantum\n");
					totalLinecount += 1;
				}

				totalLinecount += 1;
			}

			procTable->processes[toParentMsg->pt].totalTimeInSystem += toParentMsg->burst;
			procTable->processes[toParentMsg->pt].totalCpuTimeUsed += toParentMsg->burst;

			if (totalLinecount < MAXOUTPUT){
				fprintf(logfile, "OSS: Putting process with PID %d into queue 0 \n", toParentMsg->pt);
				totalLinecount += 1;
			}

			toParentMsg->seconds = sharedClock->seconds;
			toParentMsg->nanosecs = sharedClock->nanosecs;
			toParentMsg->mtype = toParentMsg->pid;
			msgsnd(queueid, toParentMsg, lenOfMessage, 0);
		
		// process in low priority queue
		} else if(msgrcv(queueid, toParentMsg, lenOfMessage, 3, IPC_NOWAIT) != -1){
			
			waitTime = diffFromSharedClock(toParentMsg->seconds, toParentMsg->nanosecs);

			if (totalLinecount < MAXOUTPUT){
				fprintf(logfile, "OSS: Dispatching process with PID %d from queue 1 at time %d:%d,\n", toParentMsg->pt, sharedClock->seconds, sharedClock->nanosecs);
				fprintf(logfile, "total time this dispatch was %d nanoseconds\n", waitTime);
				totalLinecount += 2;
			}

			procTable->processes[toParentMsg->pt].totalTimeInSystem += waitTime;

			if (totalLinecount < MAXOUTPUT){
				fprintf(logfile, "OSS: Receiving that process with PID %d ran for %d nanoseconds\n", toParentMsg->pt, toParentMsg->burst);
				if (toParentMsg->burst < (QUANTUM)){
					fprintf(logfile, "OSS: not using it's entire time quantum\n");
					totalLinecount += 1;
				}

				totalLinecount += 1;
			}

			procTable->processes[toParentMsg->pt].totalTimeInSystem += toParentMsg->burst;
			procTable->processes[toParentMsg->pt].totalCpuTimeUsed += toParentMsg->burst;

			if (totalLinecount < MAXOUTPUT){
				fprintf(logfile, "OSS: Putting process with PID %d into queue 1 \n", toParentMsg->pt);
				totalLinecount += 1;
			}

			toParentMsg->seconds = sharedClock->seconds;
			toParentMsg->nanosecs = sharedClock->nanosecs;
			toParentMsg->mtype = toParentMsg->pid;
			msgsnd(queueid, toParentMsg, lenOfMessage, 0);

		// process terminating
		} else if(msgrcv(queueid, toParentMsg, lenOfMessage, 1, IPC_NOWAIT) != -1){
				
			if (totalLinecount < MAXOUTPUT){
				fprintf(logfile, "OSS: Process with PID %d is terminating at time %d:%d\n", toParentMsg->pt, sharedClock->seconds, sharedClock->nanosecs);
				totalLinecount += 1;
			}		

			reinitializeProcessInTable(toParentMsg->pt);
		}	

        incrementClock(CLOCKINCREMENTPERROUND);
        if (sharedClock->seconds >= 1000){
            sharedClock->nanosecs = 0;
            doneflag = 1
        }
        if(totalChildren >= 99){
           	doneflag = 1; 
        }

    }

    while(childCounter > 0){

    	printf("Child count: %d\n", childCounter);
    	sleep(2);

    }


	fprintf(logfile, "OSS: Average turnaround time for processes %d:%d \n", ((totalTimeInSystem / totalDispatches) / 1000000000), ((totalTimeInSystem / totalDispatches) % 1000000000));
	fprintf(logfile, "OSS: Average wait time for processes %d:%d \n", (((totalTimeInSystem - totalCpuTimeUsed) / totalDispatches) / 1000000000), (((totalTimeInSystem - totalCpuTimeUsed) / totalDispatches) % 1000000000));
	fprintf(logfile, "OSS: The CPU was idle for %d:%d\n", ((totalTimeInSystem - totalCpuTimeUsed) / 1000000000), ((totalTimeInSystem - totalCpuTimeUsed) % 1000000000));


    printf("Final Clock time is at %d:%d\n", sharedClock->seconds, sharedClock->nanosecs);

	tearDown();

	fclose(logfile);

	return 0;


}

int sigHandling(){

	//set up alarm after some time limit
	struct sigaction timerAlarm;

	timerAlarm.sa_handler = endAllProcesses;
	timerAlarm.sa_flags = 0;

	if ((sigemptyset(&timerAlarm.sa_mask) == -1) || (sigaction(SIGALRM, &timerAlarm, NULL) == -1)) {
		perror("Failed to set SIGALRM to handle timer alarm");
		return -1;
	}

	//set up handler for SIGINT
	struct sigaction controlc;

	controlc.sa_handler = endAllProcesses;
	controlc.sa_flags = 0;

	if ((sigemptyset(&controlc.sa_mask) == -1) || (sigaction(SIGINT, &controlc, NULL) == -1)) {
		perror("Failed to set SIGINT to handle control-c");
		return -1;
	}

	//set up handler for when child terminates
	struct sigaction workerFinished;

	workerFinished.sa_handler = childFinished;
	workerFinished.sa_flags = 0;

	if ((sigemptyset(&workerFinished.sa_mask) == -1) || (sigaction(SIGCHLD, &workerFinished, NULL) == -1)) {
		perror("Failed to set SIGCHLD to handle signal from child process");
		return -1;
	}


	return 1;
}

static void endAllProcesses(int signo){
	doneflag = 1;
	if(signo == SIGALRM){
		
		// printf("]\n\n\n\n\nKILLING ALL PROCESSES!!!!!\n\n\n\n\n\n");
		killpg(getpgid(getpid()), SIGINT);
	}
}

static void childFinished(int signo){
	pid_t finishedpid;
	while((finishedpid = waitpid(-1, NULL, WNOHANG))){
		if((finishedpid == -1) && (errno != EINTR)){
			break;
		} else {
			printf("Child %d finished!\n", finishedpid);
			childCounter -= 1;
		}
	}
}


int initPCBStructures(){
	//queue
	queueid = msgget(MSGQUEUEKEY, PERMS | IPC_CREAT);
	if (queueid == -1){
		return -1;
	} 

	// init message struct 
	toParentMsg = malloc(sizeof(mymsg_t));
	lenOfMessage = sizeof(mymsg_t) - sizeof(long);

	//init process table
	procTable = malloc(sizeof(processTable));

	// init clock
	shmclock = shmget(SHMCLOCKKEY, sizeof(clockStruct), 0666 | IPC_CREAT);
	sharedClock = (clockStruct *)shmat(shmclock, NULL, 0);
	if (shmclock == -1){
		return -1;
	}

	forkTime = malloc(sizeof(clockStruct));
	setForkTimer();

	systemClock -> seconds = 0;
	systemClock -> nanosecs = 0;


	// init process table
	int i;	
	for (i = 0; i < 18; i++){
		
		procTable->processes[i].pid = -1;
		procTable->processes[i].processPriority = 0;
	 	procTable->processes[i].totalCpuTimeUsed = 0;
	 	procTable->processes[i].totalTimeInSystem = 0;

	}

	for (i = 0; i < 18; i++){
		printf("Process %d in process table is: %d\n", i, procTable->processes[i].pid);		
	}

	return 0;
}


void tearDown(){
	shmdt(sharedClock);
	shmctl(shmclock, IPC_RMID, NULL);
	msgctl(queueid, IPC_RMID, NULL);
}



int addToProcessTable(int newPid){
	int i;
	for(i = 0; i < 18; i++){
		if(procTable->processes[i].pid < 0){
			procTable->processes[i].pid = newPid;
			return i;
		}
	}
	return -1;
}

void reinitializeProcessInTable(int finishedPid){
	totalTimeInSystem += procTable->processes[finishedpid].totalTimeInSystem;
	totalCpuTimeUsed += procTable->processes[finishedpid].totalCpuTimeUsed;

	procTable->processes[finishedpid].pid = -1;
	procTable->processes[finishedpid].processPriority = 0;
	procTable->processes[finishedpid].totalCpuTimeUsed = 0;
	procTable->processes[finishedpid].totalTimeInSystem = 0;
}

void incrementClock(int increment){
	systemClock->nanosecs += increment;
    if (systemClock->nanosecs >= 1000000000){
        systemClock->seconds += 1;
        systemClock->nanosecs = systemClock->nanosecs % 1000000000;
    } 
}

int checkIfTimeToFork(){	
	if ((sharedClock->nanosecs >= forkTime->nanosecs) && (sharedClock->seconds >= forkTime->seconds)){
		return 1;
	} else {
		if(sharedClock->seconds < 2 && sharedClock->nanosecs % 100000000 == 0){
	}
		return 0;
	}
}

void setForkTimer(){
	randForkTime = (rand() % 2000) * 1000000;

	forkTime->nanosecs = sharedClock->nanosecs + randForkTime;
	forkTime->seconds = sharedClock->seconds;

	while(forkTime->nanosecs >= 1000000000){
		forkTime->seconds += 1;
		forkTime->nanosecs = forkTime->nanosecs%1000000000;
	}
}

int decidePriority(){
	randForkTime = rand() % 100;

	// priority queue
	if (randForkTime < PRIORITYCONSTANT){
		//high priority
		return 0;
	} else {
		// low priority
		return 1;
	}
}

int diffFromSharedClock(int seconds, int nanosecs){
	int diffNano = sharedClock->nanosecs - nanosecs;
	int diffSecs = sharedClock->seconds - seconds;

	if (diffNano < 0){
		diffNano = diffNano * -1;
		diffSecs -= 1;
	}
	diffNano = convertToNano(diffSecs, diffNano);

	return diffNano;


}

int convertToNano(int seconds, int nanosecs){
	int totalNano = nanosecs + (1000000000 * seconds);
	return totalNano;
}






