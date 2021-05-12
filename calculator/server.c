#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h> 
#include <sys/socket.h> 
#include <netinet/in.h>
#include <string.h>
#include <math.h>
#include <semaphore.h> 

#define PORT 4562
#define SIZE 32
#define maxCal 3

int socket_fd;
sem_t calcAvailable;

//Debug utility
void printMsg(char * s){
  int size = SIZE;
  int j = 0;
  printf("-----start-----\n");
  for(j=0; j<size; j++){
    printf("%c - %d\n", s[j],s[j]);
  }
  printf("-----end-----\n");
}

int initServer() {

  struct sockaddr_in server_addr;
  int socket_fd, bindStatus;
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);

  if(socket_fd < 0){
    printf("Socket creation failed.\n");
    exit(1);
  } else {
    printf("Socket creation succeeded.\n");
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port =  htons(PORT);

  bindStatus = bind(socket_fd, (struct sockaddr*) & server_addr, sizeof(server_addr)) < 0;
  if (bindStatus < 0) {
    printf("Error while binding.\n");
    exit(1);
  } else {
    printf("Bind to port %d successful.\n", PORT);
  }

  if(listen(socket_fd, 5) == 0){
    printf("Started listening on port %d\n", PORT);
  }else{
    printf("Something went wrong when attempting to listen.\n");
  }

  return socket_fd;
}

void extractParams(char *buf, char *op, float *arg1, float *arg2) {

  int size = SIZE;
  char new_buff[size];
  int i = 0;
  while(i<size){
    if(buf[i] != 0) {
      new_buff[i] = buf[i];
    }else{
      new_buff[i] = ' ';
    } 
    i++;
  }

  new_buff[size-1] = '\0';
  
  char vals[3][10];
  char * temp;
  i = 0;

  temp = strtok (new_buff, " ");
  while (temp != NULL)
  {
    strcpy(vals[i], temp);
    i++;
    if(i == 3) break;
    temp = strtok (NULL, " ");
  }

  strcpy(op, vals[0]);
  *arg1 = atof(vals[1]);
  *arg2 = atof(vals[2]);

}

void calculate(char * op, float arg1, float arg2, float * ans, int * status){
  if(strcmp(op, "ADD") == 0) {
    *ans = arg1 + arg2;
    *status = 1;
  } else if (strcmp(op, "SUB") == 0){
    *ans = arg1 - arg2;
    *status = 1;
  } else if (strcmp(op, "MUL") == 0){
    *ans = arg1 * arg2;
    *status = 1;
  } else if (strcmp(op, "DIV") == 0){
    if(arg2 != 0) {
      *ans = arg1 / arg2;
      *status = 1;
    }
  } else if (strcmp(op, "MOD") == 0){
    if(arg2 != 0) {
      *ans = (int) arg1 % (int)arg2;
      *status = 1;
    }
  } else if (strcmp(op, "POW") == 0){
    if(arg1 != 0 || arg2 != 0) {
      *ans = pow(arg1, arg2);
      *status = 1;
    }
  } else if (strcmp(op, "EXP") == 0){
      *ans = exp(arg1);
      *status = 1;
  } else if (strcmp(op, "LN") == 0){
    if(arg1 != 0) {
      *ans = log(arg1);
      *status = 1;
    }
  }
}

void *newCalcInstance(void *arg) {
  int *calcIDptr = (int *) arg;
  int calcID = *calcIDptr;
  int newSocket = *calcIDptr;

  struct sockaddr_in client_addr;
  socklen_t cliSize;
  char err[SIZE] = "Invalid request";

  char welcomeMsg[SIZE];
  sprintf(welcomeMsg, "Calculator - %d", calcID);
  send(newSocket, welcomeMsg, SIZE, 0);

  while(1) {
    float arg1 = 0.0, arg2 = 0.0, ans = 0.0;
    int status = 0;
    char op[5];
    char ansStr[SIZE] = {0};
    char buf[SIZE] = {0};

    recv(newSocket, buf, SIZE, 0);
    extractParams(buf, op, &arg1, &arg2);

    if(strcmp(op, "CLOSE") == 0) {
      strcpy(ansStr, "CLOSING");
      send(newSocket, ansStr, SIZE, 0);
      break;
    }

    calculate(op, arg1, arg2, &ans, &status);
    gcvt(ans, 16, ansStr); 

    if (status == 1) {
      send(newSocket, ansStr, SIZE, 0);
    } else {
      send(newSocket, err, SIZE, 0);
    }
  }
  printf("Closing calculator - %d\n", calcID);
  close(newSocket);
  sem_post(&calcAvailable);
  pthread_exit(NULL);

}


int main(){

	sem_init(&calcAvailable, 0, maxCal);
  socket_fd = initServer();
  struct sockaddr_in client_addr;
  socklen_t cliSize;

  pthread_t id;
  int socket;
  while(1){

    sem_wait(&calcAvailable);
    socket = accept(socket_fd, (struct sockaddr*)&client_addr, &cliSize);
    if(socket < 0){
      printf("Something went wrong while accepting");
      continue;
    }

    printf("A new calculator was requested. Providing calc no - %d\n", socket);
    pthread_create(&id, NULL, newCalcInstance, &socket);

  }

	sem_destroy(&calcAvailable); 
  return 0;
}
