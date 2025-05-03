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

#define PORT 3030
#define BUFFER_SIZE 4096
#define S2_PORT 3032
#define S3_PORT 3034
#define S4_PORT 3036
#define MAX_FILES 1000  /* Maximum number of files to process */

// Function prototypes
void send_c_tar(int client_sock);
int stream_tar_from_server(int client_sock, const char *filetype, int server_port);

// Function to create directories recursively
void create_directories(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    int len = strlen(tmp);

    // Create directories for the given path
    for (int i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0777); // Create directory with full permissions
            tmp[i] = '/';
        }
    }
    mkdir(tmp, 0777); // Create the final directory
}

// Function to save data locally to a specified path
void save_locally(const char *filename, const char *data, int size, const char *dest_path) {
    const char *home = getenv("HOME");
    char full_path[1024];

    // Resolve destination path
    if (dest_path[0] == '~')
        snprintf(full_path, sizeof(full_path), "%s%s", home, dest_path + 1);
    else
        snprintf(full_path, sizeof(full_path), "%s/S1/%s", home, dest_path);

    create_directories(full_path); // Create necessary directories

    char file_path[1024];
    snprintf(file_path, sizeof(file_path), "%s/%s", full_path, filename);

    // Open file for writing
    FILE *fp = fopen(file_path, "wb");
    if (fp) {
        fwrite(data, 1, size, fp); // Write data to file
        fclose(fp);
        printf("Stored .c file at %s\n", file_path);
    } else {
        perror("Error writing .c file"); // Error handling for file write
    }
}

// Function to forward file data to a specified server
void forward_to_server(const char *filename, const char *data, int size, const char *dest_path, int server_port, const char *server_name) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket to server failed");
        return;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the server
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        return;
    }

    // Step 1: Send "UPLOAD" command
    char cmd[10] = "UPLOAD";
    send(sock, cmd, sizeof(cmd), 0);  // Notify server it's an upload

    // Step 2: Send filename, path, size, and data
    send(sock, filename, 256, 0);
    send(sock, dest_path, 256, 0);
    send(sock, &size, sizeof(int), 0);
    send(sock, data, size, 0);

    printf("Forwarded file to %s\n", server_name);
    close(sock); // Close the socket after sending
}

// Function to resolve file path, handling ~ expansion
void resolve_path(const char *input_path, char *resolved_path, size_t resolved_size) {
    const char *home = getenv("HOME");
    
    // Handle ~/ at the beginning
    if (strncmp(input_path, "~/", 2) == 0) {
        snprintf(resolved_path, resolved_size, "%s/%s", home, input_path + 2);
    }
    // Handle ~S1/ at the beginning (special case for this application)
    else if (strncmp(input_path, "~S1/", 4) == 0) {
        snprintf(resolved_path, resolved_size, "%s/S1/%s", home, input_path + 4);
    }
    // Handle absolute path
    else if (input_path[0] == '/') {
        strncpy(resolved_path, input_path, resolved_size);
        resolved_path[resolved_size - 1] = '\0';
    }
    // Handle relative path
    else {
        snprintf(resolved_path, resolved_size, "%s/S1/%s", home, input_path);
    }
}

// Extracts path components from an S1 path to be used for S2/S3/S4
void extract_path_components(const char *s1_path, char *server_path, size_t max_len) {
    const char *home = getenv("HOME");
    char s1_prefix[1024];
    snprintf(s1_prefix, sizeof(s1_prefix), "%s/S1/", home);
    
    // If the path starts with the home/S1 directory
    if (strncmp(s1_path, s1_prefix, strlen(s1_prefix)) == 0) {
        // Extract the part after HOME/S1/
        strncpy(server_path, s1_path + strlen(s1_prefix), max_len);
        server_path[max_len - 1] = '\0';
    } 
    // If it's a path like ~S1/something
    else if (strncmp(s1_path, "~S1/", 4) == 0) {
        strncpy(server_path, s1_path + 4, max_len);
        server_path[max_len - 1] = '\0';
    }
    // Otherwise just copy the relative path
    else {
        strncpy(server_path, s1_path, max_len);
        server_path[max_len - 1] = '\0';
    }
}

// Get file from another server (S2/S3/S4)
int get_file_from_server(int client_sock, const char *path, int server_port) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        int error_code = -1;
        send(client_sock, &error_code, sizeof(int), 0);
        perror("Socket creation failed");
        return 0;
    }
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    // Attempt to connect to the server
    if (connect(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        int error_code = -1;
        send(client_sock, &error_code, sizeof(int), 0);
        close(server_sock);
        perror("Connection to server failed");
        return 0;
    }
    
    // Send "DOWNLOAD" command to the server
    char cmd[10] = "DOWNLOAD";
    send(server_sock, cmd, sizeof(cmd), 0);
    
    // Extract path components after S1 prefix
    char server_path[512];
    resolve_path(path, server_path, sizeof(server_path));
    
    char relative_path[512];
    extract_path_components(server_path, relative_path, sizeof(relative_path));
    
    // Send the path to the server
    send(server_sock, relative_path, sizeof(relative_path), 0);
    
    // Get file size from the server
    int file_size = 0;
    recv(server_sock, &file_size, sizeof(int), 0);
    
    if (file_size <= 0) {
        // Forward the error to client
        send(client_sock, &file_size, sizeof(int), 0);
        close(server_sock);
        return 0;
    }
    
    // Send file size to client
    send(client_sock, &file_size, sizeof(int), 0);
    
    // Relay data from server to client
    char buffer[BUFFER_SIZE];
    int bytes_read = 0;
    int total_read = 0;
    
    while (total_read < file_size) {
        bytes_read = recv(server_sock, buffer, 
                          (file_size - total_read < BUFFER_SIZE) ? (file_size - total_read) : BUFFER_SIZE, 0);
        if (bytes_read <= 0) {
            perror("Error receiving data from server");
            break;
        }
        send(client_sock, buffer, bytes_read, 0);
        total_read += bytes_read;
    }
    
    close(server_sock);
    return 1;
}

// Function to handle file download requests
int handle_download(int client_sock, const char *path) {
    char *ext = strrchr(path, '.');
    char resolved_path[1024];
    
    // Check file extension
    if (!ext || (strcmp(ext, ".c") != 0 && 
                strcmp(ext, ".pdf") != 0 && 
                strcmp(ext, ".txt") != 0 && 
                strcmp(ext, ".zip") != 0)) {
        int error_code = -1;
        send(client_sock, &error_code, sizeof(int), 0);
        return 0;
 }
    
    // For .c files, resolve path and check locally
    if (strcmp(ext, ".c") == 0) {
        resolve_path(path, resolved_path, sizeof(resolved_path));
        
        printf("Looking for .c file at: %s\n", resolved_path);
        
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
        printf("Sent .c file to client: %s\n", resolved_path);
        return 1;
    }
    // For .pdf files, get from S2
    else if (strcmp(ext, ".pdf") == 0) {
        printf("Retrieving .pdf file from S2: %s\n", path);
        return get_file_from_server(client_sock, path, S2_PORT);
    }
    // For .txt files, get from S3
    else if (strcmp(ext, ".txt") == 0) {
        printf("Retrieving .txt file from S3: %s\n", path);
        return get_file_from_server(client_sock, path, S3_PORT);
    }
    // For .zip files, get from S4
    else if (strcmp(ext, ".zip") == 0) {
        printf("Retrieving .zip file from S4: %s\n", path);
        return get_file_from_server(client_sock, path, S4_PORT);
    }
    
    return 0;
}

/* ===== START OF REMOVE FUNCTIONALITY ===== */

// Function to remove file from S1, S2, or S3 (local or remote)
int handle_remove(int client_sock, const char *path) {
    char *ext = strrchr(path, '.');
    char resolved_path[1024];
    int status_code = 0;  // 0: Success, 1: File not found, 2: Permission denied

    // Check file extension
    if (!ext || (strcmp(ext, ".c") != 0 && 
                strcmp(ext, ".pdf") != 0 && 
                strcmp(ext, ".txt") != 0 && 
                strcmp(ext, ".zip") != 0)) {
        status_code = 1;  // File not found/supported
        send(client_sock, &status_code, sizeof(int), 0);
        return 0;
    }

    // For .c files, resolve path and remove locally
    if (strcmp(ext, ".c") == 0) {
        resolve_path(path, resolved_path, sizeof(resolved_path));
        
        printf("Attempting to remove .c file: %s\n", resolved_path);
        
        // Try to access the file first
        if (access(resolved_path, F_OK) != 0) {
            status_code = 1;  // File not found
            send(client_sock, &status_code, sizeof(int), 0);
            return 0;
        }

        // Try to remove the file
        if (remove(resolved_path) != 0) {
            status_code = 2;  // Permission denied or other error
            send(client_sock, &status_code, sizeof(int), 0);
            perror("Error removing .c file");
            return 0;
        }

        // File successfully removed
        status_code = 0;
        send(client_sock, &status_code, sizeof(int), 0);
        printf("Successfully removed .c file: %s\n", resolved_path);
        return 1;
    }

    // For .pdf files, forward remove request to S2
    else if (strcmp(ext, ".pdf") == 0) {
        // Create socket to S2
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            status_code = 2;  // Error code for failure
            send(client_sock, &status_code, sizeof(int), 0);
            perror("Socket creation to S2 failed");
            return 0;
        }

        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(S2_PORT);
        serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

 if (connect(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            status_code = 2;  // Error code for failure
            send(client_sock, &status_code, sizeof(int), 0);
            close(server_sock);
            perror("Connection to S2 failed");
            return 0;
        }

        // Send "REMOVE" command to S2
        char cmd[10] = "REMOVE";
        send(server_sock, cmd, sizeof(cmd), 0);

        // Extract path components after S1 prefix
        char server_path[512];
        resolve_path(path, server_path, sizeof(server_path));

        char relative_path[512];
        extract_path_components(server_path, relative_path, sizeof(relative_path));

        // Send the path to the server
        send(server_sock, relative_path, sizeof(relative_path), 0);

        // Get status code from S2
        recv(server_sock, &status_code, sizeof(int), 0);
        close(server_sock);

        // Forward status code to client
        send(client_sock, &status_code, sizeof(int), 0);
        printf("PDF file removal request forwarded to S2. Status: %d\n", status_code);
        return 1;
    }

    // For .txt files, forward remove request to S3
    else if (strcmp(ext, ".txt") == 0) {
        // Create socket to S3
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            status_code = 2;  // Error code for failure
            send(client_sock, &status_code, sizeof(int), 0);
            perror("Socket creation to S3 failed");
            return 0;
        }

        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(S3_PORT);
        serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            status_code = 2;  // Error code for failure
            send(client_sock, &status_code, sizeof(int), 0);
            close(server_sock);
            perror("Connection to S3 failed");
            return 0;
        }

        // Send "REMOVE" command to S3
        char cmd[10] = "REMOVE";
        send(server_sock, cmd, sizeof(cmd), 0);

        // Extract path components after S1 prefix
        char server_path[512];
        resolve_path(path, server_path, sizeof(server_path));

        char relative_path[512];
        extract_path_components(server_path, relative_path, sizeof(relative_path));

        // Send the path to the server
        send(server_sock, relative_path, sizeof(relative_path), 0);

        // Get status code from S3
        recv(server_sock, &status_code, sizeof(int), 0);
        close(server_sock);

        // Forward status code to client
        send(client_sock, &status_code, sizeof(int), 0);
        printf("TXT file removal request forwarded to S3. Status: %d\n", status_code);
        return 1;
    }

    // For .zip files, forward remove request to S4
    else if (strcmp(ext, ".zip") == 0) {
        // Create socket to S4
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (server_sock < 0) {
            status_code = 2;  // Error code for failure
            send(client_sock, &status_code, sizeof(int), 0);
            perror("Socket creation to S4 failed");
            return 0;
        }

        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(S4_PORT);
        serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            status_code = 2;  // Error code for failure
            send(client_sock, &status_code, sizeof(int), 0);
            close(server_sock);
            perror("Connection to S4 failed");
            return 0;
        }

        // Send "REMOVE" command to S4
        char cmd[10] = "REMOVE";
        send(server_sock, cmd, sizeof(cmd), 0);

        // Extract path components after S1 prefix
        char server_path[512];
        resolve_path(path, server_path, sizeof(server_path));

        char relative_path[512];
        extract_path_components(server_path, relative_path, sizeof(relative_path));

        // Send the path to the server
        send(server_sock, relative_path, sizeof(relative_path), 0);

        // Get status code from S4
        recv(server_sock , &status_code, sizeof(int), 0);
        close(server_sock);

        // Forward status code to client
        send(client_sock, &status_code, sizeof(int), 0);
        printf("ZIP file removal request forwarded to S4. Status: %d\n", status_code);
        return 1;
    }

    return 0; // Return 0 if no valid file type was found
}

/* ===== END OF REMOVE FUNCTIONALITY ===== */

// Modified handle_tarfetch to stream directly to client
void handle_tarfetch(int client_sock, const char *filetype) {
    if (strcmp(filetype, ".c") == 0) {
        printf("Creating .c tar file for client\n");
        send_c_tar(client_sock);
    } 
    else if (strcmp(filetype, ".pdf") == 0) {
        printf("Forwarding PDF tar request to S2\n");
        if (stream_tar_from_server(client_sock, filetype, S2_PORT) != 0) {
            int error = -1;
            send(client_sock, &error, sizeof(int), 0);
        }
    }
    else if (strcmp(filetype, ".txt") == 0) {
        printf("Forwarding TXT tar request to S3\n");
        if (stream_tar_from_server(client_sock, filetype, S3_PORT) != 0) {
            int error = -1;
            send(client_sock, &error, sizeof(int), 0);
        }
    }
    else {
        // Invalid file type
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
    }
}

// Function to create a tar file for .c files (local to S1) and send to client
void send_c_tar(int client_sock) {
    const char *home = getenv("HOME");
    char command[1024];
    char tar_name[] = "cfiles.tar";  // Name client will receive
    
    // Create tar file in /tmp directory to avoid permission issues
    char tmp_tar_path[1024];
    snprintf(tmp_tar_path, sizeof(tmp_tar_path), "/tmp/cfiles_%d.tar", getpid());
    
    // Create the tar file
    snprintf(command, sizeof(command), "tar -cf %s -C %s/S1 .", tmp_tar_path, home);
    int result = system(command);
    
    if (result != 0) {
        printf("Error creating .c tar file\n");
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        return;
    }
    
    // Open the tar file
    FILE *fp = fopen(tmp_tar_path, "rb");
    if (!fp) {
        perror("Error opening tar file");
        int error = -1;
        send(client_sock, &error, sizeof(int), 0);
        remove(tmp_tar_path); // Clean up temporary file
        return;
    }
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    rewind(fp);
    
    // Send file size to client
    send(client_sock, &file_size, sizeof(int), 0);
    
    // Send file data
    char buffer[BUFFER_SIZE];
    while (!feof(fp)) {
        size_t read = fread(buffer, 1, BUFFER_SIZE, fp);
        if (read > 0) {
            send(client_sock, buffer, read, 0);
        }
    }
    
    fclose(fp);
    remove(tmp_tar_path); // Clean up temporary file
    printf("Sent .c tar file to client (%d bytes)\n", file_size);
}

// Modified request_tar_from_server to stream directly to client
int stream_tar_from_server(int client_sock, const char *filetype, int server_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };

    // Attempt to connect to the server
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        return -1;
    }

    // Send TARFETCH command
    char cmd[10] = "TARFETCH";
    send(sock, cmd, sizeof(cmd), 0);
    send(sock, filetype, strlen(filetype)+1, 0);

    // Receive file size
    int file_size;
    if (recv(sock, &file_size, sizeof(int), 0) <= 0) {
        close(sock);
        perror("Failed to receive file size");
 return -1;
    }

    if (file_size <= 0) {
        close(sock);
        perror("Received invalid file size");
        return -1;
    }

    // Forward file size to client
    send(client_sock, &file_size, sizeof(int), 0);

    // Stream data from server to client
    char buffer[BUFFER_SIZE];
    int remaining = file_size;
    while (remaining > 0) {
        int to_read = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;
        int received = recv(sock, buffer, to_read, 0);
        if (received <= 0) {
            perror("Error receiving data from server");
            break;
        }
        
        // Forward to client
        int sent = send(client_sock, buffer, received, 0);
        if (sent <= 0) {
            perror("Error sending data to client");
            break;
        }
        
        remaining -= received;
    }

    close(sock);
    return (remaining == 0) ? 0 : -1;
}

// Function to get filenames from S2, S3, or S4
int get_filenames_from_server(const char *dir_path, char filenames[][256], int *count, int max_files, int server_port, const char *ext) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket connection to server failed");
        return 0;
    }
    
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    // Attempt to connect to the server
    if (connect(server_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection to server failed");
        close(server_sock);
        return 0;
    }
    
    // Send "LISTFILES" command
    char cmd[10] = "LISTFILES";
    send(server_sock, cmd, sizeof(cmd), 0);
    
    // Extract path components after S1 prefix
    char server_path[512];
    extract_path_components(dir_path, server_path, sizeof(server_path));
    
    // Send the path to the server
    send(server_sock, server_path, sizeof(server_path), 0);
    
    // Receive file count
    int file_count = 0;
    recv(server_sock, &file_count, sizeof(int), 0);
    
    if (file_count <= 0) {
        close(server_sock);
        return 0;
    }
    
    // Receive filenames
    for (int i = 0; i < file_count && *count < max_files; i++) {
        char filename[256];
        recv(server_sock, filename, sizeof(filename), 0);
        
        // Check if the file has the specified extension
        char *file_ext = strrchr(filename, '.');
        if (file_ext && strcmp(file_ext, ext) == 0) {
            strcpy(filenames[*count], filename);
            (*count)++;
        }
    }
    
    close(server_sock);
    return 1;
}

// Compare function for qsort to sort filenames alphabetically
int compare_filenames(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

// Function to handle the dispfnames command
int handle_dispfnames(int client_sock, const char *dir_path) {
    char resolved_path[1024];
    resolve_path(dir_path, resolved_path, sizeof(resolved_path));
    
    // Check if the directory exists
    DIR *dir = opendir(resolved_path);
    if (!dir) {
        perror("Directory open error");
        int error_code = -1;
        send(client_sock, &error_code, sizeof(int), 0);
        return 0;
    }
    closedir(dir);
    
    // Arrays to store filenames by type
    char c_files[MAX_FILES][256];
    char pdf_files[MAX_FILES][256];
    char txt_files[MAX_FILES][256];
    char zip_files[MAX_FILES][256];
    int c_count = 0, pdf_count = 0, txt_count = 0, zip_count = 0;
    
    // Get local .c files
    dir = opendir(resolved_path);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && c_count < MAX_FILES) {
        if (entry->d_type == DT_REG) {  // Regular file
            char *ext = strrchr(entry->d_name, '.');
            if (ext && strcmp(ext, ".c") == 0) {
                strcpy(c_files[c_count], entry->d_name);
                c_count++;
            }
        }
    }
    closedir(dir);
    
    // Get . pdf files from S2
    get_filenames_from_server(dir_path, pdf_files, &pdf_count, MAX_FILES, S2_PORT, ".pdf");
    
    // Get .txt files from S3
    get_filenames_from_server(dir_path, txt_files, &txt_count, MAX_FILES, S3_PORT, ".txt");
    
    // Get .zip files from S4
    get_filenames_from_server(dir_path, zip_files, &zip_count, MAX_FILES, S4_PORT, ".zip");
    
    // Sort each file type alphabetically
    qsort(c_files, c_count, sizeof(c_files[0]), compare_filenames);
    qsort(pdf_files, pdf_count, sizeof(pdf_files[0]), compare_filenames);
    qsort(txt_files, txt_count, sizeof(txt_files[0]), compare_filenames);
    qsort(zip_files, zip_count, sizeof(zip_files[0]), compare_filenames);
    
    // Calculate total file count
    int total_files = c_count + pdf_count + txt_count + zip_count;
       
    // Send total file count to client
    send(client_sock, &total_files, sizeof(int), 0);
    
    // Send filenames in specified order: .c, .pdf, .txt, .zip
    for (int i = 0; i < c_count; i++) {
        send(client_sock, c_files[i], sizeof(c_files[0]), 0);
    }
    
    for (int i = 0; i < pdf_count; i++) {
        send(client_sock, pdf_files[i], sizeof(pdf_files[0]), 0);
    }
    
    for (int i = 0; i < txt_count; i++) {
        send(client_sock, txt_files[i], sizeof(txt_files[0]), 0);
    }
    
    for (int i = 0; i < zip_count; i++) {
        send(client_sock, zip_files[i], sizeof(zip_files[0]), 0);
    }
    
    printf("Sent %d filenames to client for directory '%s'\n", total_files, dir_path);
    return 1;
}

// Main function to handle client requests
void prcclient(int client_sock) {
    while (1) {
        char cmd[10] = {0};
        int n = recv(client_sock, cmd, sizeof(cmd), 0);
        if (n <= 0) {
            close(client_sock);
            break;  // Client disconnected
        }

        if (strcmp(cmd, "DOWNLOAD") == 0) {
            char file_path[512] = {0};
            recv(client_sock, file_path, sizeof(file_path), 0);
            printf("Download request received for: %s\n", file_path);
            handle_download(client_sock, file_path);
        }

        else if (strcmp(cmd, "REMOVE") == 0) {
            char file_path[512] = {0};
            recv(client_sock, file_path, sizeof(file_path), 0);
            printf("Remove request received for: %s\n", file_path);
            handle_remove(client_sock, file_path);
        }

        else if (strcmp(cmd, "TARFETCH") == 0) {
            char filetype[10] = {0};
            recv(client_sock, filetype, sizeof(filetype), 0);
            printf("Tar request received for: %s files\n", filetype);
            handle_tarfetch(client_sock, filetype);
        }

        else if (strcmp(cmd, "LISTFILES") == 0) {
            char dir_path[512] = {0};
            recv(client_sock, dir_path, sizeof(dir_path), 0);
            
            printf("Directory listing request received for: %s\n", dir_path);
            
            // Handle list files request
            handle_dispfnames(client_sock, dir_path);
            close(client_sock);
            continue;            
        }

        else if (strcmp(cmd, "UPLOAD") == 0) {
            printf("UPLOAD command recognized\n");

            char filename[256] = {0}, dest_path[256] = {0};
            int file_size = 0;

            if (recv(client_sock, filename, sizeof(filename), 0) <= 0 ||
                recv(client_sock, dest_path, sizeof(dest_path), 0) <= 0 ||
                recv(client_sock, &file_size, sizeof(int), 0) <= 0) {
                printf("Upload data receive failed\n");
                break;
            }

            printf("Upload request received for: %s (%d bytes) to %s\n", filename, file_size, dest_path);

            char *file_data = malloc(file_size);
            int received = 0;
            while (received < file_size) {
                int r = recv(client_sock, file_data + received, file_size - received, 0);
                if (r <= 0) break;
                received += r;
            }

            char * ext = strrchr(filename, '.');
            if (ext && strcmp(ext, ".c") == 0) {
                save_locally(filename, file_data, file_size, dest_path);
            } else if (ext && strcmp(ext, ".pdf") == 0) {
                forward_to_server(filename, file_data, file_size, dest_path, S2_PORT, "Server2");
            } else if (ext && strcmp(ext, ".txt") == 0) {
                forward_to_server(filename, file_data, file_size, dest_path, S3_PORT, "Server3");
            } else if (ext && strcmp(ext, ".zip") == 0) {
                forward_to_server(filename, file_data, file_size, dest_path, S4_PORT, "Server4");
            } else {
                printf("Unsupported file type: %s\n", filename);
            }

            free(file_data);
        } else {
            printf("Unknown command: %s\n", cmd);
        }
    }

    close(client_sock);
    exit(0); // Child exits after client disconnects
}

// Main function to set up the server
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

    // Bind the socket to the specified port
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind error");
        close(server_sock);
        exit(1);
    }

    // Start listening for incoming connections
    if (listen(server_sock, 5) < 0) {
        perror("Listen error");
        close(server_sock);
        exit(1);
    }
    printf("S1 server listening on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_size = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);

        if (client_sock < 0) {
            perror("Accept failed");
            continue; // Continue to accept next connection
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            close(client_sock);
        } else if (pid == 0) {
            // Child process
            close(server_sock); // Child doesn't need the listener
            prcclient(client_sock); // Handle client requests
        } else {
            // Parent process
            close(client_sock); // Parent doesn't need this
        }
    }

    return 0; // Return success
}