
Note - the simulation is built upon the last assignment's base (FTP-clone).
To simulate - POST a char file in char-mode to the server and observe the terminal.

Following is the README for FTP Assignment.

Folder structure:

1. Server:
.
├── config
│   └── allowedUsers.txt
├── logs
├── storage
│   ├── user1
│   ├── user2
│   ├── user3
│   ├── user4
│   └── user5
├── server.c
└── s (executable)

2. Client
.
├── local_storage
│   └── files to use in ftp-like transfers
├── client.c
└── c (executable)
------------------------------------------------------------------------------------------------

1. allowedUsers.txt contains <username>-<password> on every line for allowed users.
(No spaces allowed) (Check the sample file for details)

2. On the basis of the allowedUsers.txt, a folder corresponding to ever allowed user must be manually created in the storage folder on server side before launching the server.

3. All files that are downloaded from the server or those which are supposed to be sent to the server will reside in the local_storage folder on client machine.
 
------------------------------------------------------------------------------------------------
Client functioning:

1. Enter username and password when asked on client launch.

2. LIST
   Use this command to display all files uploaded on server by the user.

3. MODE <value>
   value can be BIN or CHAR. This switches the mode in which files will be transfered.

4. GET <filename>
   Downloads the file if present on server. Downloads it even if other client with same user logged in is downloading the same file.
   Displays error message if:
   a. Another client with the same user logged in is either uploading the same file or replacing it.
   b. File does not exist on server.

5. POST <filename>
   Uploads a file to the server.
   Displays error message if:
   a. Another client with same user logged in is uplaoding the same file.
   b. File already exists on server.

6. PATCH <filename>
   Replaces the file with the one we have locally. They must have same names.
   Displays error message if:
   a. File does not exist on server.
   b. Another client with the same user logged in is either downloading, uploading or updating the same file.

7. DELETE <filename>
   Deletes the file on server.
   Displays error message if:
   a. File does not exist on the server.
   b. Another client with the same user logged in is either downloading, uploading or updating the same file.

Running the programs (Run on linux machine):
1. gcc -o c client.c -lpthread
2. gcc -o s server.c -lpthread

3. ./s 8000
4. ./c 8000
