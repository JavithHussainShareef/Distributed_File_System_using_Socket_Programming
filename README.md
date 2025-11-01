# Distributed File System using Socket Programming

##  Overview

This project is a simplified **Distributed File System (DFS)** implemented using **socket programming in C**. It simulates a system where a central server (S1) communicates with clients and distributes files to other servers (S2, S3, and S4) based on file types.

- S1: Central Server (client-facing)
- S2: PDF File Server
- S3: TXT File Server
- S4: ZIP File Server

All client communications happen only with S1, and the actual file routing to other servers is done internally and transparently.

##  Learning Outcomes

- Apply operating system concepts for systems programming.
- Design socket-based inter-process communication in distributed environments.
- Use kernel-level functionalities to create scalable system-level applications.
- Translate abstract project specifications into a working software design.

##  Architecture

- S1 receives all file uploads from the client.
- Depending on file type:
  - `.c` files are stored locally in S1
  - `.pdf` → S2
  - `.txt` → S3
  - `.zip` → S4
- The client is unaware of the existence of S2, S3, and S4 and always interacts with S1.

##  Technologies Used

- C Programming
- Unix/Linux Sockets (TCP)
- Fork-based concurrency (for handling multiple client connections)
- File I/O operations
- Linux terminal

##  Supported Commands (Client Side)

- `uploadf <filename> <destination_path>`  
  Uploads a file from client to S1 (which internally routes based on file extension).
  
- `downlf <filename>`  
  Downloads a file from the appropriate server (via S1) to the client’s working directory.
  
- `listf <path>`  
  Lists all files in a specified server path.

- `movf <oldpath> <newpath>`  
  Moves a file from one folder to another on the server side.

- `delf <filename>`  
  Deletes a file from the system.

##  Directory Structure
- ~/S1 # Stores all .c files
- ~/S2 # Stores .pdf files (routed from S1)
- ~/S3 # Stores .txt files (routed from S1)
-  ~/S4 # Stores .zip files (routed from S1)


##  How to Run

Each process (S1, S2, S3, S4, client) should run in a separate terminal or machine.

1. Compile all source files using `gcc`.
2. Start S2, S3, and S4 servers.
3. Start the S1 server.
4. Start the client program and execute supported commands.

##  Notes

- All socket communication uses TCP.
- Client never directly connects to S2/S3/S4.
- Directory creation is handled if non-existent during file upload.
- Files are deleted from S1 after transfer (except `.c` files).



---



