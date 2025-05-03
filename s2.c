#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <dirent.h>    /* For DIR, struct dirent, opendir(), readdir(), closedir() */
#include <sys/types.h> /* For additional type definitions */

#define PORT 3032  // S2 port
#define BUFFER_SIZE 4096

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

void save_file(const char *filename, const char *data, int size, const char *dest_path) {
    const char *home = getenv("HOME");
    char full_path[1024];

    // Handle "~/S1/..." case
    if (strncmp(dest_path, "~/S1/", 5) == 0) {
        snprintf(full_path, sizeof(full_path), "%s/S2/%s", home, dest_path + 5);
    }
    // Handle generic "~/" case
    else if (strncmp(dest_path, "~/", 2) == 0) {
        snprintf(full_path, sizeof(full_path), "%s/S2/%s", home, dest_path + 2);
    }
    // Handle absolute or other paths
    else {
        snprintf(full_path, sizeof(full_path), "%s/S2/%s", home, dest_path);
    }

    // Create directories if needed
    create_directories(full_path);

    // Final file path
    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/%s", full_path, filename);

    // Write file
    FILE *fp = fopen(file_path, "wb");
    if (fp) {
        fwrite(data, 1, size, fp);
        fclose(fp);
        printf("Stored PDF file at %s\n", file_path);
    } else {
        perror("Error writing PDF file");
    }
}


// Function to handle file download requests
int handle_download(int client_sock, const char *path) {
    const char *home = getenv("HOME");
    char resolved_path[1024];
    
    // Create path with S2 directory
    snprintf(resolved_path, sizeof(resolved_path), "%s/S2/%s", home, path);
    
    printf("Looking for PDF file at: %s\n", resolved_path);
    
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
    printf("Sent PDF file to S1: %s\n", resolved_path);
    return 1;
}

// Function to resolve file path

void resolve_path(const char *input_path, char *resolved_path, size_t resolved_size) {

    const char *home = getenv("HOME");

    // Handle ~/ at the beginning

    if (strncmp(input_path, "~/", 2) == 0) {

        snprintf(resolved_path, resolved_size, "%s/S2/%s", home, input_path + 2);

    }

    // Handle absolute path (should not happen in this context)

    else if (input_path[0] == '/') {

        snprintf(resolved_path, resolved_size, "%s/S2%s", home, input_path);

    }

    // Handle relative path

    else {

        snprintf(resolved_path, resolved_size, "%s/S2/%s", home, input_path);

    }

}

// Handle file removal request

int handle_remove(int client_sock, const char *path) {

    char resolved_path[1024];

    resolve_path(path, resolved_path, sizeof(resolved_path));

    int status_code = 0;  // 0: Success, 1: File not found, 2: Permission denied/error

    printf("Attempting to remove PDF file: %s\n", resolved_path);

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

    printf("Successfully removed PDF file: %s\n", resolved_path);

    return 1;

}


// Function to create a tar file of all PDF files in S2 directory
int create_pdf_tar(const char *tar_path) {
    const char *home = getenv("HOME");
    char command[2048];  // Increased buffer size for safety
    
    // Create the tar file with full path
    snprintf(command, sizeof(command), 
             "cd %s/S2 && tar -cf %s $(find . -type f -name '*.pdf' 2>/dev/null)", 
             home, tar_path);
    
    int ret = system(command);
    if (ret != 0) {
        printf("tar command failed with status %d\n", ret);
        return -1;
    }
    
    // Verify the tar file was created
    if (access(tar_path, F_OK) != 0) {
        printf("Tar file was not created: %s\n", tar_path);
        return -1;
    }
    
    return 0;
}

// Function to handle TARFETCH requests from S1
void handle_tarfetch(int client_sock) {
    // Create unique temp file name using PID
    char tar_path[1024];
    snprintf(tar_path, sizeof(tar_path), "/tmp/pdf_temp_%d.tar", getpid());
    
    // Create the tar file
    if (create_pdf_tar(tar_path) != 0) {
        printf("Error creating PDF tar file\n");
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        return;
    }
    
    // Open the tar file
    FILE *fp = fopen(tar_path, "rb");
    if (!fp) {
        perror("Error opening tar file");
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        remove(tar_path);  // Clean up failed file
        return;
    }
    
    // Get file size
    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("Error seeking tar file");
        fclose(fp);
        remove(tar_path);
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        return;
    }
    
    long file_size = ftell(fp);
    if (file_size < 0) {
        perror("Error getting tar file size");
        fclose(fp);
        remove(tar_path);
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        return;
    }
    rewind(fp);
    
    // Send file size to S1
    if (send(client_sock, &file_size, sizeof(int), 0) <= 0) {
        perror("Error sending file size");
        fclose(fp);
        remove(tar_path);
        return;
    }
    
    // Send file data
    char buffer[BUFFER_SIZE];
    size_t total_sent = 0;
    while (!feof(fp)) {
        size_t read = fread(buffer, 1, BUFFER_SIZE, fp);
        if (read > 0) {
            ssize_t sent = send(client_sock, buffer, read, 0);
            if (sent <= 0) {
                perror("Error sending file data");
                break;
            }
            total_sent += sent;
        }
        if (ferror(fp)) {
            perror("Error reading tar file");
            break;
        }
    }
    
    fclose(fp);
    remove(tar_path); // Clean up temporary file
    
    if (total_sent == file_size) {
        printf("Successfully sent PDF tar file to S1 (%ld bytes)\n", file_size);
    } else {
        printf("Warning: Only sent %zu of %ld bytes\n", total_sent, file_size);
    }
}

// Function to handle LISTFILES request for .pdf files
void handle_list_files(int client_sock, const char *dir_path) {
    char resolved_path[1024];
    const char *home = getenv("HOME");
    
    printf("DEBUG: Received path from S1: '%s'\n", dir_path);
    
    // Convert ~/S1 to ~/S2 or ~/S1/folder to ~/S2/folder
    char adjusted_path[512] = {0};
    
    if (strncmp(dir_path, "~/S1", 4) == 0) {
        // Replace S1 with S2
        if (strlen(dir_path) > 4) {
            // Has subdirectory: ~/S1/folder -> folder
            snprintf(adjusted_path, sizeof(adjusted_path), "%s", dir_path + 5);
        } else {
            // Just ~/S1 -> empty (root of S2)
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
        // Any other path is treated as relative to S2 root
        snprintf(adjusted_path, sizeof(adjusted_path), "%s", dir_path);
    }
    
    printf("DEBUG: Adjusted path: '%s'\n", adjusted_path);
    
    // Resolve the full path for S2
    if (strlen(adjusted_path) > 0) {
        snprintf(resolved_path, sizeof(resolved_path), "%s/S2/%s", home, adjusted_path);
    } else {
        snprintf(resolved_path, sizeof(resolved_path), "%s/S2", home);
    }
    
    printf("DEBUG: Looking for PDF files in: '%s'\n", resolved_path);
    
    //printf("DEBUG: Looking for PDF files in: '%s'\n", resolved_path);
    
    // Check if the directory exists
    DIR *dir = opendir(resolved_path);
    if (!dir) {
        // Directory not found, send 0 file count
        int file_count = 0;
        perror("DEBUG: Directory open error");
        printf("DEBUG: Failed to open directory '%s'\n", resolved_path);
        send(client_sock, &file_count, sizeof(int), 0);
        return;
    }
    
    // Count and collect .pdf files
    struct dirent *entry;
    char filenames[1000][256];  // Up to 1000 files
    int file_count = 0;
    
    while ((entry = readdir(dir)) != NULL && file_count < 1000) {
        if (entry->d_type == DT_REG) {  // Regular file
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".pdf") == 0) {
                strcpy(filenames[file_count], entry->d_name);
                printf("DEBUG: Found PDF file: %s\n", entry->d_name);
                file_count++;
            }
        }
    }
    closedir(dir);
    
    printf("DEBUG: Total PDF files found: %d\n", file_count);
    
    // Send file count
    send(client_sock, &file_count, sizeof(int), 0);
    
    // Send each filename
    for (int i = 0; i < file_count; i++) {
        send(client_sock, filenames[i], sizeof(filenames[0]), 0);
        printf("DEBUG: Sent filename: %s\n", filenames[i]);
    }
    
    printf("S2: Sent %d .pdf filenames to S1 for directory '%s'\n", file_count, dir_path);
}


int main() {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket error");
        exit(1);
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = INADDR_ANY
    };

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_sock, 5);
    printf("S2 server listening on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);

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

        if (strcmp(cmd, "TARFETCH") == 0) {
            char filetype[10] = {0};
            recv(client_sock, filetype, sizeof(filetype), 0);
            
            if (strcmp(filetype, ".pdf") == 0) {
                printf("Tar request received for PDF files\n");
                handle_tarfetch(client_sock);
            } else {
                // Only PDF files are supported on S2
                printf("Unsupported file type for tar request: %s\n", filetype);
                int error = -1;
                send(client_sock, &error, sizeof(int), 0);
            }
            
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

    // Upload Functionality        
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