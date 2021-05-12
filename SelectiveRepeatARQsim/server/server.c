// gcc -o s server.c -lpthread
#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h> 
#include <sys/socket.h> 
#include <netinet/in.h>
#include <string.h>
#include <semaphore.h>
#include <time.h> 
#include <dirent.h>
#include <sys/stat.h>

#define SIZE 256
#define MAX_CONN 5
#define WINDOW_SIZE 3 
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RED     "\x1b[31m"

typedef struct file {
  char * name;
  float size;
  int status; // 0 - not in use; 1 - being read; 2 - being patched to; 3 - deleted; 4 - being uploaded
  int activeReaders;
  struct file * next;
} file;

typedef struct file_list {
  struct file * front;
} file_list;

typedef struct user {
  char * username;
  sem_t * lock;
  struct file_list * files;
  struct user * next;
} user;

typedef struct user_list { 
  struct user *front; 
} user_list; 

int socket_fd;
sem_t conn_available;
char logFileLocation[100] = {0};
user_list * users;

//Declarations for utility and debug functions
file * getFile(file_list * fileList, char * name);
file * newFile(char * name);
file_list * createFileList();
int initServer(int port);
int lookup(char *username, char *password); 
user * getUser(user_list * userList, char * name);
user * newUser(char * name);
user_list * createUserList();
void *newCalcInstance(void *arg);
void _log(char message[]);
void addFile(file_list * fileList, char * name, char * relpath);
void addUser(user_list * userList, char * username);
void calculate(char * op, float arg1, float arg2, float * ans, int * status);
void extractCreds(char *buf, char *username, char * password);
void extractParams(char *buf, char *op, char *arg);
void getTimeStamp(char *stamp);
void initUserList();
void insertFile(file_list * fileList, file * f); 
void printFileList(file_list * l);
void printMsg(char * s);
void printUserList(user_list * l);
void setStatus(file_list * fileList, char * name, int status);
void listHandler(int socket, user * u);
void deleteHandler(char * name, user * u, int socket);
void charModeSender(int socket, user * u, file * f);
void binModeSender(int socket, user * u, file * f);
void getHandler(char * name, user * u, int socket);

void copyContent(char * dest, char * src) {
  
  int i;
  for(i = 0; i<SIZE; i++) {
    dest[i] = src[i];
  }

}

typedef struct slot {
  char * content;
  int seq_no;
  int isVacant; 
  struct slot * next;
} slot;

typedef struct window {
  struct slot * front;
  int last_seq_no;
  FILE * fp;
  int eofReached;
} window;

slot * newSlot(int seq_no) { 
    slot * temp = (slot *) malloc(sizeof(slot)); 
    temp->content = (char *) malloc((SIZE) * sizeof(char));
    temp->next = NULL;
    temp->seq_no = seq_no;
    temp->isVacant = 1;
    return temp; 
}

void insertSlotAtEnd(window * w, slot * s){
  slot * temp = w->front;
  if(temp == NULL) {
    w->front = s;
    return;
  }
  while(temp->next != NULL) {
    temp = temp->next;
  }
  temp->next = s;
}

void consumeLeadingAckedSlots(window * w) {
  
  slot * front = w->front;
  while(front!=NULL && front->isVacant == 0) {
    slot * temp = front;
    if(strcmp(temp->content, "$$END$$") == 0) {
      w->eofReached = 1;
      return;
    } else {
      fprintf(w->fp, "%s", temp->content);
    }
    front = front->next;
    free(temp);

    w->front = front;

    w->last_seq_no = (w->last_seq_no + 1)%(WINDOW_SIZE * 2 + 1);
    slot * s = newSlot(w->last_seq_no);
    insertSlotAtEnd(w, s);

  }


}

window * createWindow(FILE * fp) { 
    window * w = (window *)malloc(sizeof(window)); 
    w->front = NULL;
    w->last_seq_no = WINDOW_SIZE - 1;
    w->fp = fp;
    w->eofReached = 0;

    int numberOfSlots = WINDOW_SIZE;
    int i;
    for(i = 0; i<numberOfSlots; i++) {
      slot * s = newSlot(i);
      insertSlotAtEnd(w, s);
    }

    return w; 
}

void updateSlotWithContent(window * w, int seq_no, char * content) {
  slot * s = w->front;
  while(s != NULL) {
    if(s->seq_no == seq_no) {
      copyContent(s->content, content);
      s->isVacant = 0;
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

    printf("Seq no = %d\n", s->seq_no);
    printf("isVacant = %d\n", s->isVacant);
    if(s->isVacant == 0) printf("content:\n%s\n", s->content);
    s = s->next;
  }
  printf("--------------------------------------------\n");
}

int validateChecksum(char * a, int size) {
  unsigned int p = 0;
  unsigned char c;
  int i;
  for(i = 1; i<size; i++) {
    c = (unsigned char) a[i];
    p = p + c;
    c = p;
    c = c>>8;
    p = p & 255;
    p = p + c;
  }

  unsigned char cs = ~(unsigned char)p;

  // printf("%d\n", cs);
  if(cs == 0) return 1;
  else return 0;
}

void deframe(char * frame, char * content, int * isCorrupted) {

  if(validateChecksum(frame, SIZE+2) == 1) {
    // printf("yo\n");
    *isCorrupted = 0;
    // *packetNo = frame[0];
    // printf("packet deframed = %d\n",frame[0]);
    int i;
    for(i = 1; i<SIZE+1; i++) {
      content[i-1] = frame[i];
    }
  } else {
    * isCorrupted = 1;
  }

}

void enframe(char * frame, char * content, int packetNo) {
  frame[0] = (char)packetNo;
  
  int i = 0;
  for(i = 0; i<SIZE; i++) {
    frame[i+1] = content[i];
  }

}

void charModeRecvr(int socket, user * u, file * f) {
  char fileSize[SIZE] = {0};
  recv(socket, fileSize, SIZE, 0);
  float total_size = atof(fileSize);

  char path[100] = "storage/";
  strcat(path, u->username);
  strcat(path, "/");
  strcat(path, f->name);

  FILE * fp = fopen(path, "w");
  if (fp == NULL) {
    perror("Error while reading file.");
    exit(1);
  }

  window * w = createWindow(fp);

  int expectedPacketNo = 0;
  int packetNo = 0;
  char content[SIZE] = {0};
  char frame[SIZE+2] = {0};
  char ack[SIZE] = {0};
  char nack[SIZE] = {0};
  strcpy(ack, "ACK");
  strcpy(nack, "NACK");
  int n;
  int l = 0;
  while (1) {

    n = recv(socket, frame, SIZE+2, 0);
    if (n <= 0){
      break;
    }

    int isCorrupted;
    deframe(frame, content, &isCorrupted);
    packetNo = frame[0];

    if(!isCorrupted) {

      updateSlotWithContent(w, packetNo, content);
      consumeLeadingAckedSlots(w);

      if(w->eofReached == 1) break;

      printf("Packet#%d - ", packetNo);
      printf(ANSI_COLOR_GREEN "Received packet, now ACKing\n" ANSI_COLOR_RESET);
      
      bzero(frame, SIZE+2);
      enframe(frame, ack, packetNo);
      send(socket, frame, SIZE+2, 0);

    } else {
      printf("Packet#%d - ", packetNo);
      printf( ANSI_COLOR_RED "Bad packet, so NACKing\n" ANSI_COLOR_RESET);
      bzero(frame, SIZE+2);
      enframe(frame, nack, packetNo);
      send(socket, frame, SIZE+2, 0);
    }

    bzero(content, SIZE);
    bzero(frame, SIZE+2);
    l++;
    // if(l > 7) break;
  }
  fclose(fp); 
}

void binModeRecvr(int socket, user * u, file * f) {

  char fileSize[SIZE] = {0};
  recv(socket, fileSize, SIZE, 0);
  long long int total_size = atoi(fileSize);

  char path[100] = "storage/";
  strcat(path, u->username);
  strcat(path, "/");
  strcat(path, f->name);

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
  }
  fclose(fp);
}

void postHandler(char * name, user * u, int socket) {

  int canClaim = 0;
  int fileStatus = 0;
  file * f;

  /* Lock the user's file and check if the new file can be uploaded */ 
  sem_wait(u->lock);

  f = getFile(u->files, name);

  if(f != NULL && f->status == 3) {
    canClaim = 1;
    f->status = 4;
    fileStatus = f->status;
  } else if(f != NULL) {
    canClaim = 0;
    fileStatus = f->status;
  } else if(f == NULL) {
    f = newFile(name);
    f->status = 4;
    insertFile(u->files, f);
    fileStatus = 4;
    canClaim = 1;
  }

  sem_post(u->lock);

  if(canClaim == 0) {
    if(fileStatus != 4) {
      char response[SIZE] = "The file already exists, please use PATCH to replace the file.";
      send(socket, response, SIZE, 0);
      char log_line[100] = {0};
      sprintf(log_line, "%s - POST %s , file already exists\n", u->username, name);
      _log(log_line);
    } else {
      char response[SIZE] = "Another of your client is uploading the same file. Please try again later.";
      send(socket, response, SIZE, 0);
      char log_line[100] = {0};
      sprintf(log_line, "%s - POST %s , Another client uploading the same file.\n", u->username, name);
      _log(log_line);
    }
  } else {

    char confirmation[SIZE] = "Enter YES if you really want to upload the file ";
    strcat(confirmation, f->name);

    send(socket, confirmation, SIZE, 0);
    char reply[SIZE] = {0};
    recv(socket, reply, SIZE, 0);

    if(strcmp(reply, "YES") == 0) {

      char path[100] = "storage/";
      strcat(path, u->username);
      strcat(path, "/");
      strcat(path, f->name);

      int mode = 0;
      char modeMsg[SIZE] = {0};
      recv(socket, modeMsg, SIZE, 0);

      char modeLine[100] = {0};
      if(strcmp(modeMsg, "BIN") == 0) {
        mode = 1;
        sprintf(modeLine, "%s - POST request in BIN mode\n", u->username);
        _log(modeLine);
      } else {
        sprintf(modeLine, "%s - POST request in CHAR mode\n", u->username);
        _log(modeLine);
      }

      if(mode == 0) {
        charModeRecvr(socket, u, f);
      } else {
        binModeRecvr(socket, u, f);
      }

      char final_response[SIZE] = "File uploaded succesfully";
      send(socket, final_response, SIZE, 0);

      char log_line[100] = {0};
      sprintf(log_line, "%s - POST %s Operation succeeded.\n", u->username, name);
      _log(log_line);
      
      sem_wait(u->lock);
      f->status = 0;
      struct stat st;
      stat(path, &st);
      f->size = st.st_size;  
      sem_post(u->lock);

    } else {
      sem_wait(u->lock);
      f->status = 3;
      sem_post(u->lock);
      char final_response[SIZE] = "File upload aborted";
      send(socket, final_response, SIZE, 0);      
    }


  }
}

void patchHandler(char * name, user * u, int socket) {

  int canClaim = 0;
  int fileStatus = 0;
  file * f;

  /* Lock the user's file and check if the file can be patched */
  sem_wait(u->lock);

  f = getFile(u->files, name);
  if(f != NULL) {
    fileStatus = f->status;
  } else {
    fileStatus = 3;
  }

  if(fileStatus == 0) {
    fileStatus = 2;
    f->status = 2;
    canClaim = 1;
  }

  sem_post(u->lock);

  if(canClaim == 0) {
    if(fileStatus == 3) {
      char response[SIZE] = "The file to patch doesn't exist. Upload using POST instead.";
      send(socket, response, SIZE, 0);
      char log_line[100] = {0};
      sprintf(log_line, "%s - PATCH %s , file doesn't exists\n", u->username, name);
      _log(log_line);
    } else {
      char response[SIZE] = "The file is being used by other client. Please try again later.";
      send(socket, response, SIZE, 0);
      char log_line[100] = {0};
      sprintf(log_line, "%s - PATCH %s , Another client using the same file.\n", u->username, name);
      _log(log_line);
    }
  } else {

    char confirmation[SIZE] = "Enter YES if you really want to replace the file.";
    strcat(confirmation, f->name);

    send(socket, confirmation, SIZE, 0);
    char reply[SIZE] = {0};
    recv(socket, reply, SIZE, 0);

    if(strcmp(reply, "YES") == 0) {


      int mode = 0;
      char modeMsg[SIZE] = {0};
      recv(socket, modeMsg, SIZE, 0);

      char modeLine[100] = {0};
      if(strcmp(modeMsg, "BIN") == 0) {
        mode = 1;
        sprintf(modeLine, "%s - PATCH request in BIN mode\n", u->username);
        _log(modeLine);
      } else {
        sprintf(modeLine, "%s - PATCH request in CHAR mode\n", u->username);
        _log(modeLine);
      }

      if(mode == 0) {
        charModeRecvr(socket, u, f);
      } else {
        binModeRecvr(socket, u, f);
      }

      char final_response[SIZE] = "File updated succesfully";
      send(socket, final_response, SIZE, 0);

      char log_line[100] = {0};
      sprintf(log_line, "%s - PATCH %s Operation succeeded.\n", u->username, name);
      _log(log_line);
      
      sem_wait(u->lock);
      f->status = 0;
      sem_post(u->lock);

    } else {
      sem_wait(u->lock);
      f->status = 0;
      sem_post(u->lock);
      char final_response[SIZE] = "File update aborted";
      send(socket, final_response, SIZE, 0);      
    }
  }
}

/* The controller delegates the request to appropriate request handler */
void controller(char * op, char * arg, int * status, int socket, user * u) {
  if(strcmp(op, "LIST") == 0) {
      listHandler(socket, u);
      *status = 1;
  } else if (strcmp(op, "GET") == 0){
      getHandler(arg, u, socket);
      *status = 1;
  } else if (strcmp(op, "POST") == 0){
      postHandler(arg, u, socket);
      *status = 1;
  } else if (strcmp(op, "PATCH") == 0){
      patchHandler(arg, u, socket);
      *status = 1;
  } else if (strcmp(op, "DELETE") == 0){
      deleteHandler(arg, u,socket);
      *status = 1;
  } 
}

/* On every new connection, a thread is spwaned and executes the connectionHandler function */
void *connectionHandler(void *arg) {
  int *connIDptr = (int *) arg;
  int connID = *connIDptr;
  int newSocket = *connIDptr;

  char response[SIZE] = {0};
  char buf[SIZE] = {0};
  char username[SIZE/2] = {0};
  char password[SIZE/2] = {0};
  char log_line[250] = {0};

  struct sockaddr_in client_addr;
  socklen_t cliSize;

  recv(newSocket, buf, SIZE, 0);
  extractCreds(buf, username, password);

  int validated = lookup(username, password);
  if(!validated) {

    sprintf(log_line, "Authentication for user - %s has failed.\n", username);
    _log(log_line);

    char auth_err[SIZE] = "Incorrect credentials";
    send(newSocket, auth_err, SIZE, 0);

  } else {

    sprintf(log_line, "%s has logged in.\n", username);
    _log(log_line);
    strcpy(response, "Logged In");
    send(newSocket, response, SIZE, 0);

    user * u = getUser(users, username);

    while(1) {

      int status = 0;
      char op[SIZE/2] = {0}, arg[SIZE/2] = {0};

      recv(newSocket, buf, SIZE, 0);
      extractParams(buf, op, arg);

      if(strcmp(op, "CLOSE") == 0) {
        strcpy(response, "Closing Service. Thanks!");
        send(newSocket, response, SIZE, 0);
        break;
      } else {
        strcpy(response, op);
        send(newSocket, response, SIZE, 0);
      }

      controller(op, arg, &status, newSocket, u);

      if (status != 1) {
        char err[SIZE] = "Invalid command";
        send(newSocket, err, SIZE, 0);
      }
    }

    sprintf(log_line, "%s has closed its connection to the file service - %d\n", username, connID);
    _log(log_line);
    
  }

  close(newSocket);
  sem_post(&conn_available);
  pthread_exit(NULL);

}

int main(int argc, char *argv[]){

	sem_init(&conn_available, 0, MAX_CONN);

  //Logging
  char stamp[23] = {0};
  getTimeStamp(stamp);
  sprintf(logFileLocation, "logs/%s", stamp);

  // Initialize file-registry
  initUserList();

  socket_fd = initServer(atoi(argv[1]));
  struct sockaddr_in client_addr;
  socklen_t cliSize;

  pthread_t id;
  int socket;
  while(1){

    sem_wait(&conn_available);
    socket = accept(socket_fd, (struct sockaddr*)&client_addr, &cliSize);
    if(socket < 0){
      _log("Something went wrong while accepting");
      sem_post(&conn_available);
      continue;
    } else {
      _log("A new connection has been received\n");
    }

    char log_line[70] = {0};
    sprintf(log_line, "Providing service instance - %d for the new connection\n", socket);
    _log(log_line);

    pthread_create(&id, NULL, connectionHandler, &socket);

  }

	sem_destroy(&conn_available); 
  return 0;
}

//Definitions of utility and debug functions
void printMsg(char * s){
  int size = SIZE;
  int j = 0;
  printf("-----start-----\n");
  for(j=0; j<size; j++){
    printf("%c - %d\n", s[j],s[j]);
  }
  printf("-----end-----\n");
}

int initServer(int port) {

  struct sockaddr_in server_addr;
  int socket_fd, bindStatus;
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);

  if(socket_fd < 0){
    _log("Socket creation failed.\n");
    exit(1);
  } else {
    _log("Socket creation succeeded.\n");
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port =  htons(port);

  bindStatus = bind(socket_fd, (struct sockaddr*) & server_addr, sizeof(server_addr)) < 0;
  if (bindStatus < 0) {
    _log("Error while binding.\n");
    exit(1);
  } else {
    char message[70] = {0};
    sprintf(message, "Bind to port %d successful.\n", port);
    _log(message);
  }

  if(listen(socket_fd, 5) == 0){
    char message[70] = {0};
    sprintf(message, "Started listening on port %d\n", port);
    _log(message);
  }else{
    _log("Something went wrong when attempting to listen.\n");
  }

  return socket_fd;
}

void getTimeStamp(char *stamp) {

  time_t t = time(NULL);
  struct tm tm = *localtime(&t);
  char time_stamp[22] = {0};
  sprintf(time_stamp, "[%d-%02d-%02d_%02d:%02d:%02d]", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  strcpy(stamp, time_stamp);

}

void _log(char message[]) {

  FILE * logFile = fopen(logFileLocation, "a");
  if(logFile == NULL) {
    printf("Error while creating log file\n");
    exit(1);
  }

  char stamp[23] = {0};
  getTimeStamp(stamp);
  fprintf(logFile, "%s - %s", stamp, message);
  printf(ANSI_COLOR_BLUE "%s - ", stamp);
  printf( ANSI_COLOR_RESET "%s", message);

  fclose(logFile);

}

int lookup(char *username, char *password) {

  FILE * users = fopen("config/allowedUsers.txt", "r");
  if(users == NULL) {
    printf("Error while opening allowed-users configuration file\n");
    exit(1);
  }

  char *line;
  size_t len = 0;
  int validated = 0;

  while ((getline(&line, &len, users)) != -1) {

    char buff[100] = {0};
    int i = 0;
    while(line[i] != '\0') {
      buff[i] = line[i];
      i++;
    }

    while(i<100) {
      buff[i] = '\0';
      i++;
    }

    i = 0;
    while(i<100) {
      if(buff[i] == '\n')
        buff[i] = '\0';
      i++;
    }

    char * temp;
    char vals[2][50];
    i = 0;
    temp = strtok (buff, "-");
    while (temp != NULL) {
      strcpy(vals[i], temp);
      i++;
      if(i == 2) break;
      temp = strtok (NULL, "-");
    }

    if(strcmp(vals[0], username) == 0 && strcmp(vals[1], password) == 0) {
      validated = 1;
      break;
    }
  }

  fclose(users);
  return validated;

}

file * newFile(char * name) { 
    file * temp = (file *) malloc(sizeof(file)); 
    temp->name = (char *) malloc(50 * sizeof(char));
    strcpy(temp->name, name);
    temp->next = NULL;
    temp->size = 0;
    temp->status = 0;
    temp->activeReaders = 0;
    return temp; 
}

file_list * createFileList() { 
    file_list * l = (file_list *)malloc(sizeof(file_list)); 
    l->front = NULL;
    return l; 
}

user_list * createUserList() { 
    user_list * l = (user_list *)malloc(sizeof(user_list)); 
    l->front = NULL;
    return l; 
}

void addFile(file_list * fileList, char * name, char * path) {

  file * f = newFile(name);

  char relpath[100] = {0};
  strcpy(relpath, path);
  struct stat st;
  strcat(relpath, "/");
  strcat(relpath, name);
  stat(relpath, &st);
  f->size = st.st_size;

  file * temp = fileList->front;

  if(temp == NULL) {
    fileList->front = f;
    return;
  }

  while(temp->next != NULL) {
    temp = temp->next;
  }

  temp->next = f;
}

void insertFile(file_list * fileList, file * f) {

  file * temp = fileList->front;

  if(temp == NULL) {
    fileList->front = f;
    return;
  }

  while(temp->next != NULL) {
    temp = temp->next;
  }

  temp->next = f;
}

void addUser(user_list * userList, char * username) {

  user * u = newUser(username);
  user * temp = userList->front;

  if(temp == NULL) {
    userList->front = u;
    return;
  }

  while(temp->next != NULL) {
    temp = temp->next;
  }

  temp->next = u;

}

void setStatus(file_list * fileList, char * name, int status) {
  file * temp = fileList->front;
  while(temp != NULL){
    if(strcmp(temp->name, name) == 0) {
      break;
    }
    temp = temp->next;
  }

  if(temp != NULL) {
    temp->status = status;
  }
}

void printFileList(file_list * l){
  file * temp = l->front;
  while (temp != NULL) {
    printf("{\n");
    printf("\tName - %s\n", temp->name);
    printf("\tSize - %f\n", temp->size);
    printf("\tStatus - %d\n", temp->status);
    printf("}\n");
    temp = temp->next;
  }
}

void printUserList(user_list * l) {
  user * temp = l->front;
  while (temp != NULL) {
    printf("{\n");
    printf("\tName - %s\n", temp->username);
    printFileList(temp->files);
    printf("}\n");
    temp = temp->next;
  }
}

user * newUser(char * name) { 

    user * temp = (user *) malloc(sizeof(user)); 
    temp->username = (char *) malloc(50 * sizeof(char));
    strcpy(temp->username, name);

    temp->lock = (sem_t *) malloc(sizeof(sem_t));
  	sem_init(temp->lock, 0, 1);

    temp->files = createFileList();
    struct dirent *de; 
    char location[70] = {0};
    sprintf(location, "storage/%s", temp->username);
    DIR *dr = opendir(location);
    if (dr == NULL) { 
      printf("Could not open required directory" ); 
      exit(1); 
    }

    char relpath[100] = "storage/";
    strcat(relpath, name); 

    while ((de = readdir(dr)) != NULL) {
      if(strcmp(de->d_name, ".") != 0 && strcmp(de->d_name, "..")) {
        addFile(temp->files, de->d_name, relpath); 
      }
    }  
    closedir(dr);

    temp->next = NULL;

    return temp; 
}

user * getUser(user_list * userList, char * name) {
  user * temp = userList->front;
  if(temp == NULL) return NULL;

  while(temp != NULL) {
    if(strcmp(temp->username, name) == 0) break;
    temp = temp->next;
  }

  return temp;

}

file * getFile(file_list * fileList, char * name) {
  file * temp = fileList->front;
  if(temp == NULL) return NULL;

  while(temp != NULL) {
    if(strcmp(temp->name, name) == 0) break;
    temp = temp->next;
  }

  return temp;

}

void initUserList() {
  users = createUserList();
  FILE * user_config = fopen("config/allowedUsers.txt", "r");
  if(users == NULL) {
    printf("Error while opening allowed-users configuration file\n");
    exit(1);
  }

  char *line;
  size_t len = 0;
  int validated = 0;

  while ((getline(&line, &len, user_config)) != -1) {

    char buff[100] = {0};
    int i = 0;
    while(line[i] != '\0') {
      buff[i] = line[i];
      i++;
    }

    while(i<100) {
      buff[i] = '\0';
      i++;
    }

    i = 0;
    while(i<100) {
      if(buff[i] == '\n')
        buff[i] = '\0';
      i++;
    }

    char * temp;
    char vals[2][50];
    i = 0;
    temp = strtok (buff, "-");
    while (temp != NULL) {
      strcpy(vals[i], temp);
      i++;
      if(i == 2) break;
      temp = strtok (NULL, "-");
    }

    addUser(users, vals[0]);

  }

  fclose(user_config);
}

void extractCreds(char *buf, char *username, char * password) {

  int size = SIZE;
  char new_buff[size];
  int i = 0;
  while(i<size){
    if(buf[i] != 0) {
      new_buff[i] = buf[i];
    }else if(buf[i] == '^') {
      break;
    }
    else {
      new_buff[i] = ' ';
    } 
    i++;
  }

  while(i<size){
    new_buff[i] = 0;
    i++;
  }

  char vals[2][10];
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

  strcpy(username, vals[0]);
  strcpy(password, vals[1]);

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



void listHandler(int socket, user * u) {

  /* Lock the file-registry before capturing the state of the files */
  sem_wait(u->lock);

  file_list * files = u->files;
  int count = 0;
  file * temp = files->front;
  char response[SIZE] = {0};
  char row[SIZE/8] = {0};
  while(temp != NULL) {
    if(temp->status != 3) {
      sprintf(row, "%s | %.2f Bytes\n", temp->name, temp->size);
      strcat(response, row);
      strcpy(row, " ");
      count++;
    }
    temp = temp->next;
  }

  sem_post(u->lock);
  /* Unlock and proceed to sending the information to client */
  
  if(count == 0) {
    sprintf(response, "You have not uploaded any files");
  }

  send(socket, response, SIZE, 0);

  char log_line[100] = {0};
  sprintf(log_line, "%s - LIST\n", u->username);
  _log(log_line);

}

void deleteHandler(char * name, user * u, int socket) {

  /* Lock the user's file before starting the deletion routine */
  sem_wait(u->lock);

  int fileStatus = 0;
  file * f = getFile(u->files, name);
  if(f == NULL) fileStatus = 3;
  if(f != NULL) fileStatus = f->status;

  if(fileStatus == 3) {

    char response[SIZE] = "The file does not exist";
    send(socket, response, SIZE, 0);
    char log_line[100] = {0};
    sprintf(log_line, "%s - DELETE %s , file doesn't exist\n", u->username, name);
    _log(log_line);

  } else if(fileStatus != 0) {

    char response[SIZE] = "File is being used. Try again later";
    send(socket, response, SIZE, 0);
    char log_line[100] = {0};
    sprintf(log_line, "%s - DELETE %s , File in use, deletion aborted\n", u->username, name);
    _log(log_line);

  } else if (fileStatus == 0) {

    char confirmation[SIZE] = "Enter YES if you really want to delete file ";
    strcat(confirmation, f->name);

    send(socket, confirmation, SIZE, 0);
    char reply[SIZE] = {0};
    recv(socket, reply, SIZE, 0);

    if(strcmp(reply, "YES") == 0) {
      char path[100] = "storage/";
      strcat(path, u->username);
      strcat(path, "/");
      strcat(path, f->name);
      remove(path);
      setStatus(u->files, name, 3);

      char final_response[SIZE] = "File deleted succesfully";
      send(socket, final_response, SIZE, 0);

      char log_line[100] = {0};
      sprintf(log_line, "%s - DELETE %s Operation succeeded.\n", u->username, name);
      _log(log_line);

    } else {
      char final_response[SIZE] = "File deletion aborted";
      send(socket, final_response, SIZE, 0);      
    } 
  }

  sem_post(u->lock);
  /* Unlock the user's file after deletion routine finishes */

}

void charModeSender(int socket, user * u, file * f) {

    char fileSize[SIZE] = {0};
    gcvt(f->size, 8, fileSize);
    send(socket, fileSize, SIZE, 0);

    char path[100] = "storage/";
    strcat(path, u->username);
    strcat(path, "/");
    strcat(path, f->name);

    FILE * fp = fopen(path, "r");
    if (fp == NULL) {
      perror("Error while reading file.");
      exit(1);
    }

    char content[SIZE] = {0};
    while(fgets(content, SIZE, fp) != NULL) {
      if (send(socket, content, sizeof(content), 0) == -1) {
        perror("Error while sending file.");
        exit(1);
      }
      bzero(content, SIZE);
    }
    fclose(fp);
    
    char delim[SIZE] = "$$END$$";
    send(socket, delim, SIZE, 0);

}

void binModeSender(int socket, user * u, file * f) {

    char fileSize[SIZE] = {0};
    gcvt(f->size, 8, fileSize);
    send(socket, fileSize, SIZE, 0);

    char path[100] = "storage/";
    strcat(path, u->username);
    strcat(path, "/");
    strcat(path, f->name);

    FILE * fp = fopen(path, "rb");
    if (fp == NULL) {
      perror("Error while reading file.");
      exit(1);
    }

    long long int total_size = (long long int) f->size;

    long long int num = 0;
    while(total_size > 0) {

      char content[SIZE] = {0};
      num =  total_size < SIZE ? total_size : SIZE;
      num = fread(content, 1, num, fp);
      if (num < 1) break;
      send(socket, content, num, 0);
      total_size -= num;

    }
    fclose(fp);
}

void getHandler(char * name, user * u, int socket) {

  int fileStatus = 0;
  file * f;

  /* Lock user's file and check if the file can be downloaded */
  sem_wait(u->lock);

  f = getFile(u->files, name);
  if(f == NULL) fileStatus = 3;
  if(f != NULL) fileStatus = f->status;
  
  if(fileStatus == 0 || fileStatus == 1) {
    fileStatus = 1;
    f->status = 1;
    f->activeReaders += 1;
  }

  sem_post(u->lock);

  if(fileStatus == 3) {

    char response[SIZE] = "The file does not exist";
    send(socket, response, SIZE, 0);
    char log_line[100] = {0};
    sprintf(log_line, "%s - GET %s , file doesn't exist\n", u->username, name);
    _log(log_line);

  } else if (fileStatus == 2) {

    char response[SIZE] = "The file is currently being written to. Try again later.";
    send(socket, response, SIZE, 0);
    char log_line[100] = {0};
    sprintf(log_line, "%s - GET %s , file is being written. GET denied\n", u->username, name);
    _log(log_line);  

  } else if (fileStatus == 1){

    char confirmation[SIZE] = "Enter YES if you really want to download the file ";
    strcat(confirmation, f->name);

    send(socket, confirmation, SIZE, 0);
    char reply[SIZE] = {0};
    recv(socket, reply, SIZE, 0);

    if(strcmp(reply, "YES") == 0) {

      int mode = 0;
      char modeMsg[SIZE] = {0};
      recv(socket, modeMsg, SIZE, 0);

      char modeLine[100] = {0};
      if(strcmp(modeMsg, "BIN") == 0) {
        mode = 1;
        sprintf(modeLine, "%s - GET request in BIN mode\n", u->username);
        _log(modeLine);
      } else {
        sprintf(modeLine, "%s - GET request in CHAR mode\n", u->username);
        _log(modeLine);
      }

      if(mode == 0) {
        charModeSender(socket, u, f);
      } else {
        binModeSender(socket, u, f);
      }

      char final_response[SIZE] = "File downloaded succesfully";
      send(socket, final_response, SIZE, 0);

      char log_line[100] = {0};
      sprintf(log_line, "%s - GET %s Operation succeeded.\n", u->username, name);
      _log(log_line);

    } else {
      char final_response[SIZE] = "File download aborted";
      send(socket, final_response, SIZE, 0);      
    }

    sem_wait(u->lock);
    f->activeReaders -= 1;
    if(f->activeReaders == 0) f->status = 0;  
    sem_post(u->lock);
  }
}

