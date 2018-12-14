
all: oss worker

oss: 
	gcc -Wall -pthread master.c -o oss 

worker:
	gcc -Wall -pthread worker.c -o worker 

clean:
	rm worker
	rm oss
	rm logfile
