#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <libgen.h>

#define PORT 3030          // Define the port number for the server
#define BUFFER_SIZE 4096   // Define the buffer size for data transfer

int main() {
    char command[1024], filename[256], dest_path[256];

    // Main loop for the client
    while (1) {
        printf("w25client$ "); // Prompt for user input
        fgets(command, sizeof(command), stdin); // Read command from user
        command[strcspn(command, "\n")] = 0; // Remove newline character

        // Check for upload command
        if (strncmp(command, "uploadf", 7) == 0) {
            char src_path[512], dest_path[512];
            // Parse the command to get source and destination paths
            if (sscanf(command, "uploadf %s %s", src_path, dest_path) != 2) {
                printf("Invalid syntax. Use: uploadf source_path destination_path\n");
                continue;
            }
            if (!(strncmp(dest_path, "~/S1", 4) == 0 || strncmp(dest_path, "~S1", 3) == 0)) {
                 printf("Invalid destination path. Use ~/S1 or ~S1\n");
                 continue;
            }
        
            // Open the source file for reading
            FILE *fp = fopen(src_path, "rb");
            if (!fp) {
                perror("File open failed");
                continue;
            }
        
            // Get the size of the file
            fseek(fp, 0, SEEK_END);
            int file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
        
            // Allocate memory to hold the file data
            char *file_data = malloc(file_size);
            if (!file_data) {
                perror("Memory allocation failed");
                fclose(fp);
                continue;
            }
        
            // Read the file data into memory
            fread(file_data, 1, file_size, fp);
            fclose(fp);
        
            // Extract the filename from the source path
            char *path_copy = strdup(src_path);
            char *filename = basename(path_copy);
        
            // Create a socket for communication with the server
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT);
            server_addr.sin_addr.s_addr = INADDR_ANY; // Connect to any available address
        
            // Connect to the server
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("Connect failed");
                free(file_data);
                free(path_copy);
                close(sock);
                continue;
            }
        
            // Send UPLOAD command and file details to the server
            char cmd[10] = "UPLOAD";
            send(sock, cmd, sizeof(cmd), 0);
            send(sock, filename, 256, 0);
            send(sock, dest_path, 256, 0);
            send(sock, &file_size, sizeof(int), 0);
            send(sock, file_data, file_size, 0);
        
            printf("Uploaded '%s' (%d bytes) to server path '%s'.\n", filename, file_size, dest_path);
        
            // Clean up resources
            free(file_data);
            free(path_copy);
            close(sock);
        } 
        // Check for download command
        else if (strncmp(command, "downlf", 6) == 0) {
            // Extract the full file path from the command
            char full_path[512];
            if (sscanf(command, "downlf %511[^\n]", full_path) != 1) {
                printf("Invalid syntax. Use: downlf path_to_file\n");
                continue;
            }

            // Check if the file has a valid extension
            char *ext = strrchr(full_path, '.');
            if (!ext || (strcmp(ext, ".c") != 0 && strcmp(ext, ".pdf") != 0 && 
                          strcmp(ext, ".txt") != 0 && strcmp(ext, ".zip") != 0)) {
                printf("Unsupported file type. Only .c, .pdf, .txt, and .zip files are supported.\n");
                continue;
            }

            // Create a socket for communication with the server
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT);
            server_addr.sin_addr.s_addr = INADDR_ANY; // Connect to any available address

            // Connect to the server
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("Connect failed");
                close(sock);
                continue;
            }

            // Send DOWNLOAD command and the requested file path to the server
            char cmd[10] = "DOWNLOAD";
            send(sock, cmd, sizeof(cmd), 0);
            send(sock, full_path, sizeof(full_path), 0);

            // Receive the size of the file from the server
            int file_size = 0;
            int bytes_received = recv(sock, &file_size, sizeof(int), 0);
            if (bytes_received <= 0) {
                printf("Error receiving file size.\n");
                close(sock);
                continue;
            }

            // Check if the file exists on the server
            if (file_size < 0) {
                printf("File not found on server.\n");
                close(sock);
                continue;
            }

            printf("Receiving file of size %d bytes...\n", file_size);

            // Allocate memory to receive the file data
            char *file_data = malloc(file_size);
            if (!file_data) {
                perror("Failed to allocate memory");
                close(sock);
                continue;
            }

            // Receive the file data in chunks
            int received = 0;
            while (received < file_size) {
                int r = recv(sock, file_data + received, file_size - received, 0);
                if (r <= 0) {
                    printf("Connection error while receiving data.\n");
                    break;
                }
                received += r;
            }

            // Check if the entire file was received
            if (received < file_size) {
                printf("Warning: Only received %d of %d bytes\n", received, file_size);
                free(file_data);
                close(sock);
                continue;
            }

            // Extract the filename from the full path
            char *path_copy = strdup(full_path);
            if (!path_copy) {
                perror("Memory allocation error");
                free(file_data);
                close(sock);
                continue;
            }
            
            char *base_name = basename(path_copy);
            char local_filename[256];
            strncpy(local_filename, base_name, sizeof(local_filename) - 1);
            local_filename[sizeof(local_filename) - 1] = '\0';
            free(path_copy);
            
            // Save the received data to a file in the current directory
            FILE *fp = fopen(local_filename, "wb");
            if (!fp) {
                perror("Failed to create file");
                free(file_data);
                close(sock);
                continue;
            }

            // Write the received data to the file
            size_t written = fwrite(file_data, 1, file_size, fp);
            if (written != file_size) {
                printf("Warning: Only wrote %zu of %d bytes to file\n", written, file_size);
            }
            
            fclose(fp);
            printf("Downloaded '%s' to current directory (%d bytes).\n", local_filename, file_size);

            // Clean up resources
            free(file_data);
            close(sock);

        } 
        // Check for remove command
        else if (strncmp(command, "removef", 7) == 0) {
            // Extract the file path from the command
            char full_path[512];
            if (sscanf(command, "removef %511[^\n]", full_path) != 1) {
                printf("Invalid syntax. Use: removef path_to_file\n");
                continue;
            }

            // Check if the file has a valid extension
            char *ext = strrchr(full_path, '.');
            if (!ext || (strcmp(ext, ".c") != 0 && 
                         strcmp(ext, ".pdf") != 0 && 
                         strcmp(ext, ".txt") != 0 && 
                         strcmp(ext, ".zip") != 0)) {
                printf("Unsupported file type. Only .c, .pdf, and .txt files can be removed.\n");
                continue;
            }

            // Create a socket for communication with the server
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT);
            server_addr.sin_addr.s_addr = INADDR_ANY; // Connect to any available address

            // Connect to the server
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("Connect failed");
                close(sock);
                continue;
            }

            // Send REMOVE command and the file path to the server
            char cmd[10] = "REMOVE";
            send(sock, cmd, sizeof(cmd), 0);
            send(sock, full_path, sizeof(full_path), 0);

            // Receive status code from the server
            int status_code = 0;
            recv(sock, &status_code, sizeof(int), 0);

            // Check the status of the remove operation
            if (status_code == 0) {
                printf("File '%s' successfully removed.\n", full_path);
            } else if (status_code == 1) {
                printf("File '%s' not found.\n", full_path);
            } else if (status_code == 2) {
                printf("Permission denied to remove file '%s'.\n", full_path);
            } else {
                printf("Unknown error occurred while trying to remove '%s'.\n", full_path);
            }

            close(sock);
        } 
        // Check for download tar command
        else if (strncmp(command, "downltar", 8) == 0) {
            char filetype[10];
            // Parse the command to get the file type
            if (sscanf(command, "downltar %9s", filetype) != 1) {
                printf("Invalid syntax. Use: downltar .filetype (.c/.pdf/.txt)\n");
                continue;
            }

            // Validate the file type
            if (strcmp(filetype, ".c") != 0 && strcmp(filetype, ".pdf") != 0 && strcmp(filetype, ".txt") != 0) {
                printf("Unsupported file type. Only .c, .pdf, and .txt are supported for tar download.\n");
                continue;
            }

            // Create a socket for communication with the server
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT);
            server_addr.sin_addr.s_addr = INADDR_ANY; // Connect to any available address

            // Connect to the server
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("Connect failed");
                close(sock);
                continue;
            }

            // Send TARFETCH command and the file type to the server
            char cmd[10] = "TARFETCH";
            send(sock, cmd, sizeof(cmd), 0);
            send(sock, filetype, sizeof(filetype), 0);

            // Receive the size of the tar file from the server
            int file_size = 0;
            int bytes_received = recv(sock, &file_size, sizeof(int), 0);
            if (bytes_received <= 0) {
                printf("Error receiving tar file size.\n");
                close(sock);
                continue;
            }

            // Check if the tar file exists
            if (file_size < 0) {
                printf("Tar file not found or could not be created.\n");
                close(sock);
                continue;
            }

            printf("Receiving tar file of size %d bytes...\n", file_size);

            // Allocate memory to receive the tar file data
            char *file_data = malloc(file_size);
            if (!file_data) {
                perror("Memory allocation error");
                close(sock);
                continue;
            }

            // Receive the tar file data in chunks
            int received = 0;
            while (received < file_size) {
                int r = recv(sock, file_data + received, file_size - received, 0);
                if (r <= 0) {
                    printf("Connection error while receiving tar file.\n");
                    break;
                }
                received += r;
            }

            // Check if the entire tar file was received
            if (received < file_size) {
                printf("Warning: Only received %d of %d bytes\n", received, file_size);
                free(file_data);
                close(sock);
                continue;
            }

            // Determine the local tar file name based on the file type
            char tar_name[64];
            if (strcmp(filetype, ".c") == 0)
                strcpy(tar_name, "cfiles.tar");
            else if (strcmp(filetype, ".pdf") == 0)
                strcpy(tar_name, "pdf.tar");
            else
                strcpy(tar_name, "text.tar");

            // Save the received tar file data to a local file
            FILE *fp = fopen(tar_name, "wb");
            if (!fp) {
                perror("Failed to create tar file locally");
                free(file_data);
                close(sock);
                continue;
            }

            // Write the received tar data to the file
            size_t written = fwrite(file_data, 1, file_size, fp);
            if (written != file_size) {
                printf("Warning: Only wrote %zu of %d bytes to file\n", written, file_size);
            }

            fclose(fp);
            printf("Downloaded '%s' to current directory (%d bytes).\n", tar_name, file_size);

            // Clean up resources
            free(file_data);
            close(sock);
        }
        // Check for display filenames command
        else if (strncmp(command, "dispfn ames", 10) == 0) {
            // Extract the directory path from the command
            char dir_path[512];
            if (sscanf(command, "dispfnames %511[^\n]", dir_path) != 1) {
                printf("Invalid syntax. Use: dispfnames pathname\n");
                continue;
            }
            
            // Create a socket for communication with the server
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in server_addr;
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(PORT);
            server_addr.sin_addr.s_addr = INADDR_ANY; // Connect to any available address

            // Connect to the server
            if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
                perror("Connect failed");
                close(sock);
                continue;
            }

            // Send LISTFILES command and the directory path to the server
            char cmd[10] = "LISTFILES";
            send(sock, cmd, sizeof(cmd), 0);
            send(sock, dir_path, sizeof(dir_path), 0);

            // Receive the count of files in the directory
            int file_count = 0;
            recv(sock, &file_count, sizeof(int), 0);
            
            // Check for errors in directory access
            if (file_count < 0) {
                printf("Error: Directory not found or access denied.\n");
                close(sock);
                continue;
            }
            
            // Handle case where no files are found
            if (file_count == 0) {
                printf("No files found in directory '%s'\n", dir_path);
                close(sock);
                continue;
            }
            
            printf("Files in '%s':\n", dir_path);
            
            // Receive and display each filename
            for (int i = 0; i < file_count; i++) {
                char filename[256] = {0};
                recv(sock, filename, sizeof(filename), 0);
                printf("%s\n", filename);
            }
            
            close(sock);
        } 
        // Check for exit command
        else if (strcmp(command, "exit") == 0) {
            break; // Exit the loop and terminate the program
        } else {
            printf("Unknown command.\n"); // Handle unknown commands
        }
    }

    return 0; // Return success
}