/*
 * File:   copy.c
 * Author: 
 * Family Name: FRIMPONG
 * Given Name: RICHMOND
 * Created on June 28, 2015, 4:58 AM
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <sys/time.h>

#define TEN_MILLI_NANO_SEC 10000000
#define TRUE 1

typedef struct {
    char data;
    off_t offset; /*tell the current position of data been read or written*/
} BufferItem;

BufferItem *buffer; /*Global circular bounded buffer*/

/*global accessible variables*/
FILE *file, *outfile;
FILE * logfile;
pthread_mutex_t result = PTHREAD_MUTEX_INITIALIZER;
pthread_attr_t attr;
/*semaphores*/
sem_t mutex;
sem_t empty;
sem_t full;
/************/
ssize_t offset = 0;
int in = 0;
int out = 0;
int buffer_size;
int counter = 0;
struct timespec ts;
int exit_condtion;
/*helper functions*/
void get_sleep(struct timespec t);
int produce_item(BufferItem *item, int data);
int consume_item(BufferItem *item);
void init();
clock_t start , end , total_time;

void* IN(void *thread_number) {/*producer function to read bytes into buffer*/
    int x;
    x = *((int *) thread_number);
    while (TRUE) {
        get_sleep(ts);
	/*critical section of the producer thread*/
        sem_wait(&empty); //if there is empty slots to fill wait
        sem_wait(&mutex); //obtain locks for reading
        pthread_mutex_lock(&result);
        int tell = 0;
        if (!feof(file)) { /* if there is more data in the file*/
            tell = ftell(file); // gets the offset 
            buffer[in].offset = tell; //position of the bytes read soo far
	    get_sleep(ts);
            int data = fgetc(file); // gets the data
            produce_item(buffer, data); // puts data in the buffer
            if (fseek(logfile, ftell(logfile), SEEK_SET) == -1) { //writes thread information to a file
                fprintf(stderr, "error setting output file position to \n");
                exit(-1);
            }
            fprintf(logfile, "read_byte\tPT%d\tO%ld\tB%d\t%d\n", x, buffer[in].offset, buffer[in].data, in);
            //printf("read_byte\tPT%d\tO%ld\tB%d\t%d\n", x, buffer[in].offset, buffer[in].data, in);
            fprintf(logfile, "producer\tPT%d\tO%ld\tB%d\t%d\n", x, buffer[in].offset, buffer[in].data, in);
            //printf("producer\tPT%d\tO%ld\tB%d\t%d\n", x, buffer[in].offset, buffer[in].data, in);
        }
        pthread_mutex_unlock(&result);
        sem_post(&mutex);
        sem_post(&full); // signal to wake a consumer thread in sleep mode to enter its critical section
        if (feof(file)) { // if there is no more data in the file
            pthread_mutex_unlock(&result);
            sem_post(&mutex);
            sem_post(&full);
            break;  // breaks out of the producer thread
        }
    }/*end of critical section in producer thread*/
    fflush(stdout);
    pthread_exit(NULL);
}

void* OUT(void* thread_number) { /*consumer function to write bytes from buffer into a file*/
    int x;
    x = *((int *) thread_number);
    while (TRUE) {
        get_sleep(ts);
	/*critical section of consumer thread*/
        /*nothing to consume*/
        sem_wait(&full); //signals for empty slots 
        sem_wait(&mutex); // obtain writing locks
        pthread_mutex_lock(&result);
        //exit_condtion = consume_item(buffer); // return a negative number if there is nothing to write
	exit_condtion = buffer[out].data;
        if (fseek(outfile, buffer[out].offset, SEEK_SET) == -1) {
            fprintf(stderr, "error setting outfile file data:%c position to %u\n", buffer[out].data, (unsigned int) buffer[out].offset);
            exit(-1);
        }
	get_sleep(ts);
       /*fseek functions here makes sure we are writing the correct offset to the copy and logfiles*/
        if (fputc(buffer[out].data, outfile) == EOF) {
            fprintf(stderr, "error writing byte %c at offset:%ld to output file\n", buffer[out].data, buffer[out].offset);
            exit(-2);
        }
        if (fseek(logfile, ftell(logfile), SEEK_SET) == -1) {
            fprintf(stderr, "error setting output file position to \n");
            exit(-3);
        }
        fprintf(logfile, "write_byte\tCT%d\tO%ld\tB%d\t%d\n", x, buffer[out].offset, buffer[out].data, out);
        //printf("write_byte\tCT%d\tO%ld\tB%d\t%d\n", x, buffer[out].offset, buffer[out].data, out);
        fprintf(logfile, "consumer\tCT%d\tO%ld\tB%d\t%d\n", x, buffer[out].offset, buffer[out].data, out);
        //printf("consumer\tCT%d\tO%ld\tB%d\t%d\n", x, buffer[out].offset, buffer[out].data, out);
	exit_condtion =	(int)buffer[out].data;
        out = (out + 1) % buffer_size; 
        counter--;
        pthread_mutex_unlock(&result);
        sem_post(&mutex);
        sem_post(&empty); //wakes a thread waiting to get in the critical session
        //printf("Waiting empty consumer value:%d\tCounter value:%d\n", exit_condtion,counter);
        if (exit_condtion == -1 ) {
            fflush(stdout);
	    pthread_exit(NULL);
        }
    }/*end of critical section in consumer thread*/
    fflush(stdout);
    pthread_exit(NULL);
}
/********************************MAIN PROGRAM STARTING POINT******************************************/
int main(int argc, char *argv[]) {
    start = clock();
    if (argc < 1) {
        fprintf(stderr, "Program Usage <program name> <args . . . . . . .>\n");
        exit(0);
    }
    buffer_size = atoi(argv[5]);
    buffer = (BufferItem *) malloc(sizeof (BufferItem) * buffer_size); //allocates memory for bounded buffer
    int produce = atoi(argv[1]); //number of producer threads
    int consume = atoi(argv[2]); // number of consumer threads
    pthread_t producer[produce]; //creates producer threads
    pthread_t consumer[consume]; //creates consumer threads

    init(); // semaphore , pthread attributes initialization call
   
   /*Main open the file and pass the file pointer to the IN thread*/
    file = fopen(argv[3], "r");
    if (file == NULL) {
        perror("Can not open file . . .");
        exit(-3);
    }
    outfile = fopen(argv[4], "w+");
    if (outfile == NULL) {
        perror("Can not open file for writing. . .");
        exit(-4);
    }
    logfile = fopen(argv[6], "w+");
    if (logfile == NULL) {
        perror("Can not open file for writing. . .");
        exit(-5);
    }
    //creating producer and consumer thread
    int thread = 1;
    for (thread = 1; thread <= produce; thread++) {
        //get_sleep(ts);
        pthread_create(&producer[thread], &attr, IN, (void *) &thread);
    }
    for (thread = 1; thread <= consume; thread++) {
        //get_sleep(ts);
        pthread_create(&consumer[thread], &attr, OUT, (void *) &thread);
    }
    //joining thread
    for (thread = 1; thread <= produce; thread++) {
        pthread_join(producer[thread], NULL);
    }
    for (thread = 1; thread <= consume; thread++) {
        pthread_join(consumer[thread], NULL);
    }
    /*clean up code*/
    pthread_mutex_destroy(&result);
    sem_destroy(&mutex);
    sem_destroy(&empty);
    sem_destroy(&full);
    fclose(file);
    fclose(outfile);
    fclose(logfile);
    free(buffer);
    end = clock();
    total_time = (double)(end - start) / CLOCKS_PER_SEC / 60.0;
    //printf("Time elapsed by CPU:%f seconds \n" , (double)total_time );
    //printf("Exiting program >>>>> main thread joined . . . . . \n");
    pthread_exit(NULL);
    return 0;
}
/* nano sleep function*/
void get_sleep(struct timespec t) {
    t.tv_sec = (time_t) (rand() % 1000) / 100000;
    t.tv_nsec = rand() % (TEN_MILLI_NANO_SEC + 1);
    nanosleep(&t, NULL);
}
/* producer function to store byte in the buffer which will later be used by producer thread*/
int produce_item(BufferItem *item, int data) {
    if (counter < buffer_size) {
        item[in].data = data;
        in = (in + 1) % buffer_size;
        counter++;
        return 0;
    } else {
        return -1;
    }
}
/* This function determines if there are more bytes to write in the consumer thread*/
/* if there is nothing to read it return -1 which signals consumer thread to end*/
int consume_item(BufferItem *item) {
    int x = 0;		
    if (counter > 0) {
        item = buffer;
        return 0;
    }
    else if((x = item[out].data) < 0){
    	 return -1;
    }  	 
    else {
        return -1;
    }
}

void init() {
    sem_init(&mutex, 0, 1); //1 because its a lock
    sem_init(&empty, 0, buffer_size);
    sem_init(&full, 0, 0);
    pthread_attr_init(&attr);
}
