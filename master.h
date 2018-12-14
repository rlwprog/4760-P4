typedef struct {
	int seconds;
	int nanosecs;
} clockStruct;

typedef struct {
	struct pcb processes[18];
} processTable;

typedef struct pcb {
	int pid;
	int processPriority;
	int totalCpuTimeUsed;
	int totalTimeInSystem;
} PCB;

typedef struct {
	long mtype;
	pid_t pid;
	int burst;
	int priority;
	int pt;
	int seconds;
	int nanosecs;
} mymsg_t;

int sigHandling();

static void endAllProcesses(int signo);
static void childFinished(int signo);

int initPCBStructures();
void tearDown();

int addToProcessTable(int newPid);
void reinitializeProcessInTable(int tableIndex);

void incrementClock(int increment);

int checkIfTimeToFork();
void setForkTimer();

int decidePriority();

int diffFromSharedClock(int seconds, int nanosecs);
int convertToNano(int seconds, int nanosecs);
