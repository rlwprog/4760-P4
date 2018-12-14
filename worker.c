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

#define SHMCLOCKKEY	86868           /* Parent and child agree on common key for clock.*/
#define MSGQUEUEKEY	68686  			/* Parent and child agree on common key for msgqueue.*/

#define ACTION 50           		/* Randomized to decide whether process uses full quantum.*/
#define TERMFACTOR 5 				/* Percent chance that a child process will terminate instead of requesting/releasing a resource */
#define TERMTHRESHOLD 50000000		/* Amount of time that must pass for process to terminate */
#define BURSTQUANTUM 100000				/* Max time each process runs in nanoseconds*/

#define PERMS (mode_t)(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define FLAGS (O_CREAT | O_EXCL)

static volatile sig_atomic_t childDoneFlag = 0;

typedef struct {
	int seconds;
	int nanosecs;
} clockStruct;

typedef struct {
	long mtype;
	pid_t pid;
	int burst;
	int priority;
	int pt;
	int seconds;
	int nanosecs;
} mymsg_t;

//globals
static int queueid;

static clockStruct *startClock;
static clockStruct *sharedClock;
static mymsg_t *toParentMsg;


int lenOfMessage;
int shmclock;

int sigHandling();
int initPCBStructures();
static void endChild(int signo);
void tearDown();

int diffFromSharedClock(int seconds, int nanosecs);
int convertToNano(int seconds, int nanosecs);


int main (int argc, char *argv[]){

	srand(time(NULL) + getpid());

	pid_t pid = getpid();

	int totalTime = 0;
	int randomActionTaken = 0;
	int pTableId = 0;
	int queuePriority = 0;
	int burstTime = 0;
	int clockDifference = 0;


	sigHandling();
	initPCBStructures();

	// gets first message from parent
	msgrcv(queueid, toParentMsg, lenOfMessage, pid, 0);
	printf("Child %d received processIndex of %d\n", pid, toParentMsg->pt);

	pTableId = toParentMsg->pt;
	queuePriority = toParentMsg->priority;

	startClock->seconds = toParentMsg->seconds;
	startClock->nanosecs = toParentMsg->nanosecs;

	while(!childDoneFlag){
			randomActionTaken = rand() % 100;

			totalTime = diffFromSharedClock(startClock->seconds, startClock->nanosecs);
			
			// process randomly decides action
			if(randomActionTaken < TERMFACTOR && (totalTime > TERMTHRESHOLD)){
				childDoneFlag = 1;
				
				//send termination status to parent to deallocated resources
				toParentMsg->mtype = 1;
				toParentMsg->pid = pid;
				toParentMsg->burst = burstTime;
				toParentMsg->priority = queuePriority;
				toParentMsg->pt = pTableId;
				toParentMsg->seconds = childClock->seconds;
				toParentMsg->nanosecs = childClock->nanosecs;

				msgsnd(queueid, toParentMsg, lenOfMessage, 1);
				// if (msgrcv(queueid, toParentMsg, lenOfMessage, pid, 0) != -1){
				// }

			// if less, get full quantum
			} else if (randomActionTaken < ACTION){
				// low priority
				if (queuePriority == 1){
					burstTime = (BURSTQUANTUM);
				} else {
					burstTime = (BURSTQUANTUM / 2);
				}
			// if more, get partial quantum	
			} else {
				if (queuePriority == 1){
					burstTime = (rand() % BURSTQUANTUM) + 1;
				} else {
					burstTime = (rand() % (BURSTQUANTUM / 2));
				}
			}

			clockDifference = diffFromSharedClock(startClock->seconds, startClock->nanosecs);
			while(clockDifference < burstTime){
				clockDifference = diffFromSharedClock(startClock->seconds, startClock->nanosecs);
			}
			toParentMsg->seconds = sharedClock->seconds;
			toParentMsg->nanosecs = sharedClock->nanosecs;
			toParentMsg->pid = pid;
			toParentMsg->burst = burstTime;
			toParentMsg->priority = queuePriority;
			toParentMsg->pt = pTableId;

			// low priority
			if (queuePriority == 1){
				toParentMsg->mtype = 3;
				
				msgsnd(queueid, toParentMsg, lenOfMessage, 3);
				if (msgrcv(queueid, toParentMsg, lenOfMessage, pid, 0) != -1){
					printf("Error returning message from parent\n");
				}

				
			// high priority
			} else {
				toParentMsg->mtype = 2;
				
				msgsnd(queueid, toParentMsg, lenOfMessage, 2);
				if (msgrcv(queueid, toParentMsg, lenOfMessage, pid, 0) != -1){
					printf("Error returning message from parent\n");
				}
			}
		
	
		}
	
	
	exit(1);
	return 1;
}

int initPCBStructures(){
	// init clocks
	shmclock = shmget(SHMCLOCKKEY, sizeof(clockStruct), 0666 | IPC_CREAT);
	sharedClock = (clockStruct *)shmat(shmclock, NULL, 0);

	startClock = malloc(sizeof(clockStruct));

	//queues
	queueid = msgget(MSGQUEUEKEY, PERMS | IPC_CREAT);
	if (queueid == -1){
		return -1;
	} 

	// init to message struct 
	toParentMsg = malloc(sizeof(mymsg_t));
	lenOfMessage = sizeof(mymsg_t) - sizeof(long);

	return 0;
}

void tearDown(){
	shmdt(sharedClock);
	shmctl(shmclock, IPC_RMID, NULL);
 	msgctl(queueid, IPC_RMID, NULL);
}

int sigHandling(){

	//set up handler for ctrl-C
	struct sigaction controlc;

	controlc.sa_handler = endChild;
	controlc.sa_flags = 0;

	if ((sigemptyset(&controlc.sa_mask) == -1) || (sigaction(SIGINT, &controlc, NULL) == -1)) {
		perror("Failed to set SIGINT to handle control-c");
		return -1;
	}

	//set up handler for when child terminates
	struct sigaction sigParent;

	sigParent.sa_handler = endChild;
	sigParent.sa_flags = 0;

	if ((sigemptyset(&sigParent.sa_mask) == -1) || (sigaction(SIGCHLD, &sigParent, NULL) == -1)) {
		perror("Failed to set SIGCHLD to handle signal from child process");
		return -1;
	}


	return 1;
}

static void endChild(int signo){
		childDoneFlag = 1;
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

