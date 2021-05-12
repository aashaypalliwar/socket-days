// gcc -o c client.c
#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h> 
#include <sys/socket.h> 
#include <netinet/in.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define SIZE 256
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_GREEN   "\x1b[32m"

int mode = 0;

//Declaration - utility functions
void reformatMsg(char * msg);
void reformatAuth(char * msg);
void printMsg(char * s);
void extractParams(char *buf, char *op, char *arg);
int interceptor(char * buf);

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

void charModeSender(int socket, char * arg) {
  char path[100] = "local_storage/";
  strcat(path, arg);

  struct stat st;
  stat(path, &st);
  float total_size = st.st_size + 1;
  float sent_size = 0;
  float old_value = 0;

  char fileSize[SIZE] = {0};
  gcvt(total_size-1, 8, fileSize);
  send(socket, fileSize, SIZE, 0);


  printf(ANSI_COLOR_MAGENTA "Uploading file %s - %f Bytes\n", arg, total_size);

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

  char delim[SIZE] = "$$END$$";
  send(socket, delim, SIZE, 0);
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
