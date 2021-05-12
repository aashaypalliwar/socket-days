// gcc -o c client.c -lpthread
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h> 
#include <sys/socket.h> 
#include <netinet/in.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define SIZE 256
#define WINDOW_SIZE 3
#define TIMEOUT_VALUE 0.2
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RED     "\x1b[31m"

int mode = 0;

//Declaration - utility functions
void reformatMsg(char * msg);
void reformatAuth(char * msg);
void printMsg(char * s);
void extractParams(char *buf, char *op, char *arg);
int interceptor(char * buf);

void listHandler(int socket);
void deleteHandler(int socket);
void charModeRecvr(int socket, char * arg);
void binModeRecvr(int socket, char * arg);
void getHandler(int socket, char * arg);
void binModeSender(int socket, char * arg);



char checksum(char * a, int size) {
  unsigned int p = 0;
  unsigned char c;
  int i = 0;
  for(i = 0; i<size; i++) {
    c = (unsigned char) a[i];
    p = p + c;
    c = p;
    c = c>>8;
    p = p & 255;
    p = p + c;
  }

  char cs = ~(char)p;
  return cs;
}

void enframe(char * frame, char * content, int packetNo) {
  frame[0] = (char)packetNo;
  
  int i = 0;
  for(i = 0; i<SIZE; i++) {
    frame[i+1] = content[i];
  }

  frame[SIZE+1] = checksum(content, SIZE);

}

void introduceError(char * frame, int * isCorrupted, int * shouldBeDropped, int packetsSentSoFar) {
  
  // int randomNumber = rand()%100 + 1;

  // if(randomNumber > 70) {
  //   frame[SIZE-2] = !frame[SIZE-2];
  //   *isCorrupted = 1;
  // }

  // if(randomNumber > 90) {
  //   *shouldBeDropped = 1;
  // }

  if((packetsSentSoFar + 1)%3 == 0) {
    frame[SIZE-2] = !frame[SIZE-2];
    *isCorrupted = 1;
  }

  if((packetsSentSoFar + 1)%4 == 0) {
    *shouldBeDropped = 1;
  }

}

void copyFrame(char * dest, char * src) {
  
  int i;
  for(i = 0; i<SIZE+2; i++) {
    dest[i] = src[i];
  }

}

void deframeWithoutCheck(char * ackFrame, int * packetNo, char * ackMsg) {
  *packetNo = ackFrame[0];
  int i;
  for(i = 1; i<SIZE+1; i++) {
    ackMsg[i-1] = ackFrame[i];
  }
}

typedef struct slot {
  char * frame;
  int seq_no;
  int isAcked;
  int isNacked; 
  int wasSent;
  float size;
  time_t sendAt;
  struct slot * next;
} slot;

typedef struct window {
  struct slot * front;
  int last_seq_no;
  FILE * fp;
  int isFileFullyLoaded;
  int isEmpty;
  float sentSize;
  int sendAttempts;
  time_t lastAccess;
} window;

typedef struct asset {
  window * w;
  int socket;
  int shouldContinue;
} asset;

slot * newSlot(char * new_frame, int seq_no, float size) { 
    slot * temp = (slot *) malloc(sizeof(slot)); 
    temp->frame = (char *) malloc((SIZE+2) * sizeof(char));
    copyFrame(temp->frame, new_frame);
    temp->next = NULL;
    temp->seq_no = seq_no;
    temp->isAcked = 0;
    temp->isNacked = 0;
    temp->wasSent = 0;
    temp->size = size;
    time(&(temp->sendAt));
    return temp; 
}

window * createWindow(FILE * fp) { 
    window * w = (window *)malloc(sizeof(window)); 
    w->front = NULL;
    w->last_seq_no = -1;
    w->fp = fp;
    w->isFileFullyLoaded = 0;
    w->isEmpty = 0;
    w->sentSize = 0;
    w->sendAttempts = 0;
    time(&(w->lastAccess));
    return w; 
}

void deleteLeadingAckedSlots(window * w) {
  slot * front = w->front;
  while(front!=NULL && front->isAcked == 1) {
    slot * temp = front;
    w->sentSize = w->sentSize + temp->size;
    front = front->next;
    free(temp);
  }

  w->front = front;
  if(front == NULL) {
    w->isEmpty = 1;
  }
}

void insertSlotAtEnd(window * w, slot * s){
  slot * temp = w->front;
  if(temp == NULL) {
    w->front = s;
    w->isEmpty = 0;
    return;
  }
  while(temp->next != NULL) {
    temp = temp->next;
  }
  temp->next = s;
}

int count(window * w) {
  slot * temp = w->front;
  int count = 0;
  while(temp != NULL){
    count++;
    temp = temp->next;
  }
  return count;
}

void updateWindow(window * w) {

  time(&(w->lastAccess));

  deleteLeadingAckedSlots(w);

  if(w->isFileFullyLoaded == 1) return;

  FILE * fp = w->fp;
  int slotsAvailable = WINDOW_SIZE - count(w);
  
  int i = 0;
  while(i < slotsAvailable) {

    char content[SIZE] = {0};
    char frame[SIZE + 2] = {0};

    char * str = fgets(content, SIZE, fp);
    if(str == NULL) {
      w->isFileFullyLoaded = 1;
      break;
    };
    
    w->last_seq_no = (w->last_seq_no + 1)%(WINDOW_SIZE * 2 + 1);
    enframe(frame, content, w->last_seq_no);
    slot * s = newSlot(frame, w->last_seq_no, strlen(content));
    insertSlotAtEnd(w, s);    

    i++;
  }

}

void sendFrames(window * w, int socket) {

  time(&(w->lastAccess));

  slot * s = w->front;
  while(s != NULL) {
    
    time_t checked_at;
    time(&checked_at);
    double timeSinceLastTry = difftime(checked_at, s->sendAt);
    int timedOut = timeSinceLastTry > TIMEOUT_VALUE && (s->isAcked == 0); 

    if(s->wasSent == 0 || s->isNacked == 1 || timedOut == 1) {

      if(s->wasSent == 0) {
        printf("Sending Packet#%d", s->seq_no);
      } else if (s->isNacked == 1) {
        printf("Resending packet#%d as it was NACKed", s->seq_no);
      } else {
        printf("Packet#%d timed out, resending", s->seq_no);
      }

      int isCorrupted = 0;
      int isDropped = 0;
      char temp_frame[SIZE + 2] = {0};
      copyFrame(temp_frame, s->frame);
      introduceError(temp_frame, &isCorrupted, &isDropped, w->sendAttempts);
      
      if(isDropped != 1) {
        send(socket, temp_frame, SIZE + 2, 0);
      } else {
        printf(ANSI_COLOR_RED " - This frame will be dropped" ANSI_COLOR_RESET);
      }

      if(isCorrupted == 1) {
        printf(ANSI_COLOR_RED " - This frame will be corrupted" ANSI_COLOR_RESET);
      }

      printf("\n");

      char content[SIZE] = {0};
      int p = 0;
      deframeWithoutCheck(s->frame, &p, content);

      s->wasSent = 1;
      s->isNacked = 0;
      time(&(s->sendAt));
      w->sendAttempts++;
    }

    s = s->next;
  }

} 

void updateAsAcked(window * w, int packetNo) {
  slot * s = w->front;
  while(s != NULL) {
    if(s->seq_no == packetNo) {
      s->isAcked = 1;
      s->isNacked = 0;
      break;
    }
    s = s->next;
  }
}

void updateAsNacked(window * w, int packetNo) {
  slot * s = w->front;
  while(s != NULL) {
    if(s->seq_no == packetNo) {
      s->isAcked = 0;
      s->isNacked = 1;
      break;
    }
    s = s->next;
  }
}

void printWindowStatus(window * w) {
  printf("\n--------------------------------------------\n");
  slot * s = w->front;
  while(s != NULL) {
    printf("\n");
    int p = 0;
    char content[SIZE] = {0};
    deframeWithoutCheck(s->frame, &p, content);
    // printf("Packet from frame = %d\n",p);
    printf("packet from slot = %d\n", s->seq_no);
    printf("isAcked = %d\n", s->isAcked);
    printf("isNacked = %d\n", s->isNacked);
    printf("wasSent = %d\n", s->wasSent);
    // printf("Content:\n%s\n", content);
    s = s->next;
  }
  printf("--------------------------------------------\n");
}

void * deadlockResolver(void *arg) {
  asset * a = (asset *) arg;
  window * w = a->w;
  int socket = a->socket;

  while(a->shouldContinue == 1) {
    time_t curr_time;
    time(&curr_time);
    double elapsedTime = difftime(curr_time, w->lastAccess);
    if(elapsedTime > 4 * TIMEOUT_VALUE) {
      printf(ANSI_COLOR_RED "Time out\n" ANSI_COLOR_RESET);
      sendFrames(w, socket);
    }
  }

  printf("Closing timer\n");
  pthread_exit(NULL);
}

void charModeSender(int socket, char * arg) {
  char path[100] = "local_storage/";
  strcat(path, arg);

  struct stat st;
  stat(path, &st);
  float total_size = st.st_size + 1;
  float old_value = 0;

  char fileSize[SIZE] = {0};
  gcvt(total_size-1, 8, fileSize);
  send(socket, fileSize, SIZE, 0);


  printf(ANSI_COLOR_MAGENTA "Uploading file %s - %f Bytes\n", arg, total_size);
  printf(ANSI_COLOR_RESET);

  FILE * fp = fopen(path, "r");
  if (fp == NULL) {
    perror("Error while reading file.");
    exit(1);
  }

  char ackFrame[SIZE+2] = {0};
  char ackMsg[SIZE] = {0};

  window * w = createWindow(fp);
  asset * a = (asset *) malloc(sizeof(asset));
  a->socket = socket;
  a->w = w;
  a->shouldContinue = 1;

  pthread_t id;
  pthread_create(&id, NULL, deadlockResolver, a);

  updateWindow(w);
  int packetNo;

  while(w->isEmpty != 1) {

    sendFrames(w, socket);

    recv(socket, ackFrame, sizeof(ackFrame), 0);

    deframeWithoutCheck(ackFrame, &packetNo, ackMsg);

    if(strcmp(ackMsg, "ACK") == 0) {
      updateAsAcked(w, packetNo);
      
      printf("Packet #%d -", packetNo);
      printf(ANSI_COLOR_GREEN " %s\n", ackMsg);
      printf(ANSI_COLOR_RESET);

    } else if(strcmp(ackMsg, "NACK") == 0) {
      updateAsNacked(w, packetNo);

      printf("Packet #%d -", packetNo);
      printf(ANSI_COLOR_RED " %s\n", ackMsg);
      printf(ANSI_COLOR_RESET);
    }

 
    updateWindow(w);
    if((w->sentSize - old_value) > total_size/10) {
      printf(ANSI_COLOR_GREEN "%.2f%% done.\n", (w->sentSize * 100)/total_size);
      printf(ANSI_COLOR_RESET);
      old_value = w->sentSize;
    }
  }

  fclose(fp);
  printf(ANSI_COLOR_GREEN "100%% done.\n"ANSI_COLOR_RESET);

  a->shouldContinue = 0;

  char frame[SIZE+2] = {0};
  char delim[SIZE] = "$$END$$";
  w->last_seq_no = (w->last_seq_no + 1)%(WINDOW_SIZE * 2 + 1);
  enframe(frame, delim, w->last_seq_no);
  send(socket, frame, sizeof(frame), 0);
}

void postHandler(int socket, char * arg) {

  char first_response[SIZE] = {0};
  recv(socket, first_response, SIZE, 0);
  recv(socket, first_response, SIZE, 0);

  printf(ANSI_COLOR_MAGENTA "Response -\n" ANSI_COLOR_RESET);
  printf("%s\n", first_response);

  if(strcmp(first_response, "The file already exists, please use PATCH to replace the file.") == 0) {
    return;
  }

  if(strcmp(first_response, "Another of your client is uploading the same file. Please try again later.") == 0) {
    return;
  }

  char * reply = (char *) malloc (SIZE);
  int size = SIZE;
  getdelim(&reply, (size_t * )&size, '\n', stdin);
  reformatMsg(reply);
  send(socket, reply, size, 0);

  if(strcmp(reply, "YES") == 0) {

    char modeMsg[SIZE] = {0};
    if(mode == 1) {
      sprintf(modeMsg, "BIN");
    } else {
      sprintf(modeMsg, "CHAR");
    }
    send(socket, modeMsg, SIZE, 0);

    if(mode == 0) {
      charModeSender(socket, arg);
    } else {
      binModeSender(socket, arg);
    }
  }

  char final_response[SIZE] = {0};
  recv(socket, final_response, SIZE, 0);
  
  printf(ANSI_COLOR_MAGENTA "Response -\n" ANSI_COLOR_RESET);
  printf("%s\n", final_response);
  free(reply);
}

void patchHandler(int socket, char * arg) {

  char first_response[SIZE] = {0};
  recv(socket, first_response, SIZE, 0);
  recv(socket, first_response, SIZE, 0);

  printf(ANSI_COLOR_MAGENTA "Response -\n" ANSI_COLOR_RESET);
  printf("%s\n", first_response);

  if(strcmp(first_response, "The file to patch doesn't exist. Upload using POST instead.") == 0) {
    return;
  }

  if(strcmp(first_response, "The file is being used by other client. Please try again later.") == 0) {
    return;
  }

  char * reply = (char *) malloc (SIZE);
  int size = SIZE;
  getdelim(&reply, (size_t * )&size, '\n', stdin);
  reformatMsg(reply);
  send(socket, reply, size, 0);

  if(strcmp(reply, "YES") == 0) {

    char modeMsg[SIZE] = {0};
    if(mode == 1) {
      sprintf(modeMsg, "BIN");
    } else {
      sprintf(modeMsg, "CHAR");
    }
    send(socket, modeMsg, SIZE, 0);

    if(mode == 0) {
      charModeSender(socket, arg);
    } else {
      binModeSender(socket, arg);
    }
  }

  char final_response[SIZE] = {0};
  recv(socket, final_response, SIZE, 0);
  // recv(socket, final_response, SIZE, 0);
  
  printf(ANSI_COLOR_MAGENTA "Response -\n" ANSI_COLOR_RESET);
  printf("%s\n", final_response);
  free(reply);
}

/* The controller delegates the response handling work to appropriate functions.*/
void controller(char * message, int socket, int * exitClient) {

  char op[SIZE/2] = {0}, arg[SIZE/2] = {0};
  extractParams(message, op, arg);
 
  if(strcmp(op, "LIST") == 0) {
    listHandler(socket);
  } else if (strcmp(op, "GET") == 0){
    getHandler(socket, arg);
  } else if (strcmp(op, "POST") == 0){
    postHandler(socket, arg);
  } else if (strcmp(op, "PATCH") == 0){
    patchHandler(socket, arg);
  } else if (strcmp(op, "DELETE") == 0){
      deleteHandler(socket);
  } else if (strcmp(op, "CLOSE") == 0){
      char response[SIZE] = {0};
      recv(socket, response, SIZE, 0);
      printf("%s\n", response);
      * exitClient = 1;
  } else {
      char response[SIZE] = {0};
      recv(socket, response, SIZE, 0);
      recv(socket, response, SIZE, 0);
      printf("%s\n", response);    
  }
}

int main(int argc, char *argv[]){

  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(client_fd < 0){
    printf("Socket creation failed.\n");
    exit(1);
	} else {
  	printf("Client Socket created successfully.\n");
	}

  int status;

	struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port =  htons (atoi(argv[1]));


  int size = SIZE;
  int exitClient = 0;
  char * message = (char *) malloc (size);
  char * reply = (char *) malloc (size);

  char * username = (char *) malloc (size/2);
  char * password = (char *) malloc (size/2);

  while(1) {

    status = connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(status < 0){
      printf("Failed to connect.\n");
      exit(1);
    }

    printf(ANSI_COLOR_MAGENTA "Enter username:\n" ANSI_COLOR_RESET);
    getdelim(&username, (size_t * )&size, '\n', stdin);
    printf(ANSI_COLOR_MAGENTA "Enter password:\n" ANSI_COLOR_RESET);
    getdelim(&password, (size_t * )&size, '\n', stdin);
    
    sprintf(reply, "%s %s^", username, password);
    reformatAuth(reply);
    printf("Authenticating\n");
    send(client_fd, reply, size, 0);
    recv(client_fd, message, size, 0);
    printf("%s\n", message);

    if(strcmp(message,"Incorrect credentials") == 0) continue;
    if(strcmp(message,"Logged In") != 0) {
      printf("Something went wrong. Please try again.\n");
      continue;  
    };

    printf("Available options:\n");
    printf("To list all files on server - LIST\n");
    printf("To download a file - GET <filename>\n");
    printf("To upload a file - POST <filename>\n");
    printf("To update a file by new one - PATCH <filename>\n");
    printf("To delete a file - DELETE <filename>\n");
    printf("To close the connection - CLOSE\n\n");

    while (1) {

      printf(ANSI_COLOR_MAGENTA "Enter command:\n" ANSI_COLOR_RESET);
      getdelim(&reply, (size_t * )&size, '\n', stdin);
      reformatMsg(reply);

      if(interceptor(reply) == 0) {
        continue;
      }

      send(client_fd, reply, size, 0);
      controller(reply, client_fd, &exitClient);

      if(exitClient) break;      
    }
    if(exitClient) break;
  }

  printf("Closing client. Thanks\n");
  close(client_fd);
  return 0;
}

//Definitions
void reformatMsg(char * msg) {
  int i = 0;
  while(msg[i] != '\n') {
    i++;
  }
  msg[i] = 0;
}

void reformatAuth(char * msg) {
  int i = 0;
  while(i<SIZE) {
    if(msg[i] == '\n') {
      msg[i] = 0;
    }
    i++;
  }
}

void printMsg(char * s){
  int size = SIZE;
  int j = 0;
  printf("-----start-----\n");
  for(j=0; j<size; j++){
    printf("%c - %d\n", s[j],s[j]);
  }
  printf("-----end-----\n");
}

void extractParams(char *buf, char *op, char *arg) {

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
  
  char vals[2][SIZE/2] = {0};
  char * temp;
  i = 0;

  temp = strtok (new_buff, " ");
  while (temp != NULL)
  {
    strcpy(vals[i], temp);
    i++;
    if(i == 2) break;
    temp = strtok (NULL, " ");
  }

  strcpy(op, vals[0]);
  strcpy(arg, vals[1]);

}

int interceptor(char * buf) {
  char op[SIZE/2] = {0}, arg[SIZE/2] = {0};
  extractParams(buf, op, arg);

  if(strcmp(op, "MODE") == 0) {
    if(strcmp(arg, "BIN") == 0){
      printf("Setting mode to BIN\n");
      mode = 1;
    } else {
      printf("Setting mode to CHAR\n");
      mode = 0;
    }
    return 0;
  }

  if (strcmp(op, "POST") == 0 || strcmp(op, "PATCH") == 0){
    struct dirent *de; 
    char location[70] = {0};
    sprintf(location, "local_storage");
    DIR *dr = opendir(location);
    if (dr == NULL) { 
      printf("Could not open required directory" ); 
      exit(1); 
    }

    int found = 0;

    while ((de = readdir(dr)) != NULL) {
      if(strcmp(de->d_name, arg) == 0) {
        found = 1;
        break;
      }
    }  
    closedir(dr);

    if(found == 0) printf("File not found locally.\n");
    return found;    
  }

  if(strcmp(op, "GET") == 0 || strcmp(op, "LIST") || strcmp(op, "CLOSE")) {
    return 1;
  }

  printf("Invalid Command\n");
  return 0;
}

void listHandler(int socket) {

  char * response = (char *) malloc (SIZE);
  recv(socket, response, SIZE, 0);
  recv(socket, response, SIZE, 0);

  printf(ANSI_COLOR_MAGENTA "Response -\n" ANSI_COLOR_RESET);
  printf("%s\n", response);
  free(response);

}

void deleteHandler(int socket) {

  char first_response[SIZE] = {0};
  recv(socket, first_response, SIZE, 0);
  recv(socket, first_response, SIZE, 0);

  printf("%s\n", first_response);

  if(strcmp(first_response, "The file does not exist") == 0) {
    return;
  }

  if(strcmp(first_response, "File is being used. Try again later") == 0) {
    return;
  }

  char * reply = (char *) malloc (SIZE);
  int size = SIZE;
  getdelim(&reply, (size_t * )&size, '\n', stdin);
  reformatMsg(reply);
  send(socket, reply, size, 0);

  char final_response[SIZE] = {0};
  recv(socket, final_response, SIZE, 0);
  
  printf(ANSI_COLOR_MAGENTA "Response -\n" ANSI_COLOR_RESET);
  printf("%s\n", final_response);
  free(reply);
}

void charModeRecvr(int socket, char * arg) {
  char fileSize[SIZE] = {0};
  recv(socket, fileSize, SIZE, 0);
  float total_size = atof(fileSize);
  float sent_size = 0;
  float old_value = 0;

  char path[100] = "local_storage/";
  strcat(path, arg);

  printf(ANSI_COLOR_MAGENTA "Downloading file %s - %.2f Bytes\n", arg, total_size);

  FILE * fp = fopen(path, "w");
  if (fp == NULL) {
    perror("Error while reading file.");
    exit(1);
  }

  char content[SIZE] = {0};
  int n;
  while (1) {
    n = recv(socket, content, SIZE, 0);
    if (n <= 0){
      break;
    }
    if(strcmp(content, "$$END$$") == 0) break;
    fprintf(fp, "%s", content);

    sent_size += strlen(content);
    if((sent_size - old_value) > total_size/10) {
      // printf("\33[2K\r");
      printf(ANSI_COLOR_GREEN "%.2f%% done.\n", (sent_size * 100)/total_size);
      old_value = sent_size;
    }
    bzero(content, SIZE);
  }
  fclose(fp);
  // printf("\33[2K\r");
  printf(ANSI_COLOR_GREEN "100%% done.\n"ANSI_COLOR_RESET);

}

void binModeRecvr(int socket, char * arg) {
  char fileSize[SIZE] = {0};
  recv(socket, fileSize, SIZE, 0);
  long long int total_size = atoi(fileSize);
  float actual_size = atof(fileSize);
  float recvd_size = 0;
  float old_value = 0;

  char path[100] = "local_storage/";
  strcat(path, arg);

  printf(ANSI_COLOR_MAGENTA "Downloading file %s - %lld Bytes\n", arg, total_size);

  FILE * fp = fopen(path, "wb");
  if (fp == NULL) {
    perror("Error while reading file.");
    exit(1);
  }

  long long int  num = 0;
  while (total_size > 0) {

    char content[SIZE] = {0};
    num =  total_size < SIZE ? total_size : SIZE;
    if (num < 1){
      break;
    }    
    num = recv(socket, content, num, 0);
    fwrite(content, 1, num, fp);

    total_size -= num;
    recvd_size += num;
    if((recvd_size - old_value) > actual_size/10) {
      // printf("\33[2K\r");
      printf(ANSI_COLOR_GREEN "%.2f%% done.\n", (recvd_size * 100)/actual_size);
      old_value = recvd_size;
    }
  }
  fclose(fp);
  // printf("\33[2K\r");
  printf(ANSI_COLOR_GREEN "100%% done.\n"ANSI_COLOR_RESET);

}

void getHandler(int socket, char * arg) {

  char first_response[SIZE] = {0};
  recv(socket, first_response, SIZE, 0);
  recv(socket, first_response, SIZE, 0);

  printf(ANSI_COLOR_MAGENTA "Response -\n" ANSI_COLOR_RESET);
  printf("%s\n", first_response);

  if(strcmp(first_response, "The file does not exist") == 0) {
    return;
  }

  if(strcmp(first_response, "The file is currently being written to. Try again later.") == 0) {
    return;
  }

  char * reply = (char *) malloc (SIZE);
  int size = SIZE;
  getdelim(&reply, (size_t * )&size, '\n', stdin);
  reformatMsg(reply);
  send(socket, reply, size, 0);

  if(strcmp(reply, "YES") == 0) {

    char modeMsg[SIZE] = {0};
    if(mode == 1) {
      sprintf(modeMsg, "BIN");
    } else {
      sprintf(modeMsg, "CHAR");
    }
    send(socket, modeMsg, SIZE, 0);    
    
    if(mode == 0) {
      charModeRecvr(socket, arg);
    } else {
      binModeRecvr(socket, arg);
    }

  }

  char final_response[SIZE] = {0};
  recv(socket, final_response, SIZE, 0);
  
  printf(ANSI_COLOR_MAGENTA "Response -\n" ANSI_COLOR_RESET);
  printf("%s\n", final_response);
  free(reply);
}


void binModeSender(int socket, char * arg) {

  char path[100] = "local_storage/";
  strcat(path, arg);

  struct stat st;
  stat(path, &st);
  long long int total_size = (long long int) st.st_size;
  float actual_size = st.st_size;
  float sent_size = 0;
  float old_value = 0;

  char fileSize[SIZE] = {0};
  gcvt(total_size, 8, fileSize);
  send(socket, fileSize, SIZE, 0);


  printf(ANSI_COLOR_MAGENTA "Uploading file %s - %lld Bytes\n", arg, total_size);

  FILE * fp = fopen(path, "rb");
  if (fp == NULL) {
    perror("Error while reading file.");
    exit(1);
  }

  long long int num = 0;
  while(total_size > 0) {

    char content[SIZE] = {0};
    num =  total_size < SIZE ? total_size : SIZE;
    num = fread(content, 1, num, fp);
    if (num < 1) break;
    send(socket, content, num, 0);
    total_size -= num;
    sent_size += num;

    if((sent_size - old_value) > actual_size/10) {
      // printf("\33[2K\r");
      printf(ANSI_COLOR_GREEN "%.2f%% done.\n", (sent_size * 100)/actual_size);
      old_value = sent_size;
    }

  }
  printf(ANSI_COLOR_GREEN "100%% done.\n"ANSI_COLOR_RESET);

  fclose(fp);
}
