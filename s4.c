#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <dirent.h>    /* For DIR, struct dirent, opendir(), readdir(), closedir() */
#include <sys/types.h> /* For additional type definitions */

#define PORT 3036
#define BUFFER_SIZE 4096

void save_file(const char *filename, char *file_data, int file_size, const char *dest_path) {
    char *ext = strrchr(filename, '.');
    if (!ext) ext = "";

    const char *home = getenv("HOME");
    char full_path[1024];

    if (strcmp(ext, ".zip") == 0) {
        snprintf(full_path, sizeof(full_path), "%s/S4/%s", home, dest_path + 5);

        // Create the directory
        char command[1024];
        snprintf(command, sizeof(command), "mkdir -p \"%s\"", full_path);
        system(command);

        // Save the file
        char file_path[1024];
        snprintf(file_path, sizeof(file_path), "%s/%s", full_path, filename);

        FILE *fp = fopen(file_path, "wb");
        if (fp) {
            fwrite(file_data, 1, file_size, fp);
            fclose(fp);
            printf("Stored .zip file at %s\n", file_path);
        } else {
            perror("Error writing file");
        }
    } else {
        printf("Invalid file format for Server 3\n");
    }
}

void create_directories(const char *path) {

    char tmp[1024];

    snprintf(tmp, sizeof(tmp), "%s", path);

    int len = strlen(tmp);



    for (int i = 1; i < len; i++) {

        if (tmp[i] == '/') {

            tmp[i] = '\0';

            mkdir(tmp, 0777);

            tmp[i] = '/';

        }

    }

    mkdir(tmp, 0777);

}

// Function to handle file download requests

int handle_download(int client_sock, const char *path) {

    const char *home = getenv("HOME");

    char resolved_path[1024];

    

    // Create path with S4 directory

    snprintf(resolved_path, sizeof(resolved_path), "%s/S4/%s", home, path);

    

    printf("Looking for ZIP file at: %s\n", resolved_path);

    

    // Open the file

    FILE *fp = fopen(resolved_path, "rb");

    if (!fp) {

        perror("File open error");

        int error_code = -1;

        send(client_sock, &error_code, sizeof(int), 0);

        return 0;

    }

    

    // Get file size

    fseek(fp, 0, SEEK_END);

    int file_size = ftell(fp);

    rewind(fp);

    

    // Send file size

    send(client_sock, &file_size, sizeof(int), 0);

    

    // Read and send file data

    char *buffer = malloc(file_size);

    fread(buffer, 1, file_size, fp);

    send(client_sock, buffer, file_size, 0);

    

    free(buffer);

    fclose(fp);

    printf("Sent ZIP file to S1: %s\n", resolved_path);

    return 1;

}


// Function to resolve file path

void resolve_path(const char *input_path, char *resolved_path, size_t resolved_size) {

    const char *home = getenv("HOME");

    // Handle ~/ at the beginning

    if (strncmp(input_path, "~/", 2) == 0) {

        snprintf(resolved_path, resolved_size, "%s/S4/%s", home, input_path + 2);

    }

    // Handle absolute path

    else if (input_path[0] == '/') {

        snprintf(resolved_path, resolved_size, "%s/S4%s", home, input_path);

    }

    // Handle relative path

    else {

        snprintf(resolved_path, resolved_size, "%s/S4/%s", home, input_path);

    }

}

// Handle file removal request

int handle_remove(int client_sock, const char *path) {

    char resolved_path[1024];

    resolve_path(path, resolved_path, sizeof(resolved_path));

    int status_code = 0;  // 0: Success, 1: File not found, 2: Permission denied/error

    printf("Attempting to remove ZIP file: %s\n", resolved_path);    

    // Check if file exists

    if (access(resolved_path, F_OK) != 0) {

        status_code = 1;  // File not found

        send(client_sock, &status_code, sizeof(int), 0);

        return 0;

    }

    // Try to remove the file

    if (remove(resolved_path) != 0) {

        status_code = 2;  // Permission denied or other error

        send(client_sock, &status_code, sizeof(int), 0);

        return 0;

    }

    // File successfully removed

    status_code = 0;

    send(client_sock, &status_code, sizeof(int), 0);

    printf("Successfully removed ZIP file: %s\n", resolved_path);

    return 1;

}

// Function to handle LISTFILES request for .zip files
void handle_list_files(int client_sock, const char *dir_path) {
    char resolved_path[1024];
    const char *home = getenv("HOME");
    
    printf("Received path from S1: '%s'\n", dir_path);
    
    // Convert ~/S1 to ~/S4 or ~/S1/folder to ~/S4/folder
    char adjusted_path[512] = {0};
    
    if (strncmp(dir_path, "~/S1", 4) == 0) {
        // Replace S1 with S4
        if (strlen(dir_path) > 4) {
            // Has subdirectory: ~/S1/folder -> folder
            snprintf(adjusted_path, sizeof(adjusted_path), "%s", dir_path + 5);
        } else {
            // Just ~/S1 -> empty (root of S4)
            adjusted_path[0] = '\0';
        }
    } else if (strncmp(dir_path, "~S1", 3) == 0) {
        // Handle ~S1 prefix (without slash)
        if (strlen(dir_path) > 3) {
            snprintf(adjusted_path, sizeof(adjusted_path), "%s", dir_path + 4);
        } else {
            adjusted_path[0] = '\0';
        }
    } else {
        // Any other path is treated as relative to S4 root
        snprintf(adjusted_path, sizeof(adjusted_path), "%s", dir_path);
    }
    
    printf("Adjusted path: '%s'\n", adjusted_path);
    
    // Resolve the full path for S4
    if (strlen(adjusted_path) > 0) {
        snprintf(resolved_path, sizeof(resolved_path), "%s/S4/%s", home, adjusted_path);
    } else {
        snprintf(resolved_path, sizeof(resolved_path), "%s/S4", home);
    }
    
    printf("Looking for ZIP files in: '%s'\n", resolved_path);
    
    
    // Check if the directory exists
    DIR *dir = opendir(resolved_path);
    if (!dir) {
        // Directory not found, send 0 file count
        int file_count = 0;
        perror("Directory open error");
        printf("Failed to open directory '%s'\n", resolved_path);
        send(client_sock, &file_count, sizeof(int), 0);
        return;
    }
    
    // Count and collect .zip files
    struct dirent *entry;
    char filenames[1000][256];  // Up to 1000 files
    int file_count = 0;
    
    while ((entry = readdir(dir)) != NULL && file_count < 1000) {
        if (entry->d_type == DT_REG) {  // Regular file
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".zip") == 0) {
                strcpy(filenames[file_count], entry->d_name);
                printf("Found ZIP file: %s\n", entry->d_name);
                file_count++;
            }
        }
    }
    closedir(dir);
    
    printf("Total ZIP files found: %d\n", file_count);
    
    // Send file count
    send(client_sock, &file_count, sizeof(int), 0);
    
    // Send each filename
    for (int i = 0; i < file_count; i++) {
        send(client_sock, filenames[i], sizeof(filenames[0]), 0);
        printf("Sent filename: %s\n", filenames[i]);
    }
    
    printf("S3: Sent %d .zip filenames to S1 for directory '%s'\n", file_count, dir_path);
}


int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket error");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind error");
        exit(1);
    }

    listen(server_sock, 5);
    printf("S4 server is listening on port %d...\n", PORT);

    while (1) {
        addr_size = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);

        //download starts here 
        char cmd[10] = {0};

        recv(client_sock, cmd, sizeof(cmd), 0);

        

        // Check if this is a download request

        if (strcmp(cmd, "DOWNLOAD") == 0) {

            char file_path[512] = {0};

            recv(client_sock, file_path, sizeof(file_path), 0);

            

            printf("Download request received from S1 for: %s\n", file_path);

            

            // Handle download request

            handle_download(client_sock, file_path);

            close(client_sock);

            continue;

        }

        // Check if this is a remove request

        if (strcmp(cmd, "REMOVE") == 0) {

            char file_path[512] = {0};

            recv(client_sock, file_path, sizeof(file_path), 0);            

            printf("Remove request received for: %s\n", file_path);

            // Handle remove request

            handle_remove(client_sock, file_path);

            close(client_sock);

            continue;

        }

        // Check if this is a list files request
        else if (strcmp(cmd, "LISTFILES") == 0) {
            char dir_path[512];
            recv(client_sock, dir_path, sizeof(dir_path), 0);
            
            printf("Directory listing request received for: %s\n", dir_path);
            
            // Handle list files request
            handle_list_files(client_sock, dir_path);
            close(client_sock);
            continue;
        }

        // If neither download nor remove, it's the original upload functionality

        if (strcmp(cmd, "UPLOAD") == 0) {
            char filename[256] = {0}, dest_path[256] = {0};
            int file_size = 0;
    
            recv(client_sock, filename, sizeof(filename), 0);
            recv(client_sock, dest_path, sizeof(dest_path), 0);
            recv(client_sock, &file_size, sizeof(int), 0);
    
            printf("Upload request received for: %s (%d bytes) to %s\n", filename, file_size, dest_path);
    
            char *file_data = malloc(file_size);
            if (!file_data) {
                perror("Memory allocation failed");
                close(client_sock);
                continue;
            }
    
            int received = 0;
            while (received < file_size) {
                int r = recv(client_sock, file_data + received, file_size - received, 0);
                if (r <= 0) break;
                received += r;
            }
    
            save_file(filename, file_data, file_size, dest_path);
            free(file_data);
            close(client_sock);
            continue;
        }
        
    }

    return 0;
}