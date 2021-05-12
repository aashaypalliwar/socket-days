#include <stdio.h>
#include <stdlib.h> 
#include <unistd.h> 
#include <sys/socket.h> 
#include <netinet/in.h>
#include <string.h>
#define PORT 4562
#define SIZE 32

void reformatMsg(char * msg) {
  int i = 0;
  while(msg[i] != '\n') {
    i++;
  }
  msg[i] = 0;
}

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

int main(){

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
  server_addr.sin_port =  htons (PORT);


  int size = SIZE;
  char * message = (char *) malloc (size);
  char * reply = (char *) malloc (size);

  status = connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
  if(status < 0){
    printf("Failed to connect.\n");
    exit(1);
  }

  recv(client_fd, message, size, 0);
  printf("Connected to %s\n",message);

  while (1) {
    printf("Enter command:\n");
    getdelim(&reply, (size_t * )&size, '\n', stdin);
    reformatMsg(reply);
    send(client_fd, reply, size, 0);
    recv(client_fd, message, size, 0);

    if(strcmp(message,"CLOSING") == 0) break;
    printf("Answer = %s\n", message);
  }

  printf("Closing connection to calculator\n");
  close(client_fd);
  return 0;
}
