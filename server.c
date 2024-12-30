#include <asm-generic/socket.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <ctype.h>
#include <dirent.h>     // FOR READING DIRECTORY CONTENTS
#include <sys/stat.h>   // FOR FILE INFORMATION

#define CONTROL_PORT 21
#define DATA_PORT 20
#define SIZE 1024

typedef struct  {
    int control_socket;
    int data_socket;
    char username[64];
    int is_authenticated;
} client_info;

// THIS FUNCTION TRIM THE STRING PASSED THROUGH THE STR POINTER
void trim(char *str)
{
    // CREATE AN END POINTER FOR THE LAST CHARACTER OF THE STRING
    char *end;
    // IT SEARCH FOR SPACES AND IF IT FINDS IT, MOVES TO THE NEXT
    // CHARACTER AND SO ON UNTIL IT FINDS A NON-SPACE CHARACTER
    while (isspace(*str)) str++;
    // THE END POINTER NOW POINTS TO THE LAST CHARACTER WITHOUT
    // COUNTING \'0'
    end = str + strlen(str) - 1;
    // IT SEARCH FOR SPACES AND IF IT FINDS IT, MOVES TO ONE BEFORE
    // THAT CHARACTER AND SO ON UNTIL IT FINDS A NON-SPACE CHARACTER
    while (end > str && isspace(*end)) end--;
    // IT ADDS THE \'0' CHARACTER AT THE END OF THE STR
    *(end + 1) = '\0';
}

void handle_quit(client_info *client)
{
    const char *response = "221 Goodbye\r\n"; 
    write(client->control_socket, response, strlen(response));

    if (close(client->control_socket) < 0) {
        perror("Close failed");
        exit(1);
    }
} 

void handle_user(client_info *client, const char *username)
{
    // CHECK CREDENTIALS
    if (strcmp(username, "admin") != 0) {
        const char *response = "430 Invalid username\r\n";
        write(client->control_socket, response, strlen(response));

        return;
    }

    // COPY THE USERNAME CONTENT INTO CLIENT->USERNAME VARIABLE, THE -1
    // AT THE END IS FOR THE \'0' CHARACTER
    stpncpy(client->username, username, sizeof(client->username) - 1);
    const char* response = "331 Username OK, password needed\r\n";
    // SENT THE RESPONSE
    write(client->control_socket, response, strlen(response));
}

void handle_password(client_info *client, const char *password)
{
    // CHECK CREDENTIALS
    if (strcmp(password, "admin") != 0) {
        const char *response = "430 Invalid password\r\n";
        write(client->control_socket, response, strlen(response));

        return;
    }
    
    // IF CREDENTIALS IS CORRECT AUTHENTICATE THE USER
    client->is_authenticated = 1;
    const char *response = "230 User logged in, proceed\r\n";
    write(client->control_socket, response, strlen(response));
}

int create_pasv_socket(client_info *client)
{
    int server_fd;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == 0) {
        perror("Socket failed");
        exit(1);
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed in PASV");
        close(server_fd);
        exit(1);
    }

    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(1);
    }

    if (getsockname(server_fd, (struct sockaddr *)&addr, &addr_len) < 0) {
        perror("getsockname failed");
        close(server_fd);
        return -1;
    }

    int port = ntohs(addr.sin_port);
    char response[128];
    snprintf(response, sizeof(response), "227 Entering Passive Mode (127,0,0,1,%d,%d)\r\n", port / 256, port % 256);

    write(client->control_socket, response, strlen(response));

    return server_fd;
}

void handle_list(client_info *client)
{
    DIR *dir;
    struct dirent *entry;
    char data_buffer[SIZE];
    
    if (client->data_socket == -1) {
        const char *response = "425 Use PASV or PORT first\r\n";
        write(client->control_socket, response, strlen(response));
        return;
    }
    
    const char *response = "150 File status okay; about to open data connection.\r\n";
    write(client->control_socket, response, strlen(response));

    int data_client = accept(client->data_socket, NULL, NULL);
    if (data_client < 0) {
        perror("Accept failed in LIST");
        close(client->data_socket);
        client->data_socket = -1;
        return;
    }

    dir = opendir(".");
    if (dir == NULL) {
        const char *err = "450 Requested file action not taken\r\n";
        write(client->control_socket, err, strlen(err));
        close(data_client);
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        struct stat file_stat;
        stat(entry->d_name, &file_stat);

        // Format: "permissions size name\r\n"
        snprintf(data_buffer, SIZE, "%c%c%c%c%c%c%c%c%c%c %8ld %s\r\n",
                (S_ISDIR(file_stat.st_mode)) ? 'd' : '-',
                (file_stat.st_mode & S_IRUSR) ? 'r' : '-',
                (file_stat.st_mode & S_IWUSR) ? 'w' : '-',
                (file_stat.st_mode & S_IXUSR) ? 'x' : '-',
                (file_stat.st_mode & S_IRGRP) ? 'r' : '-',
                (file_stat.st_mode & S_IWGRP) ? 'w' : '-',
                (file_stat.st_mode & S_IXGRP) ? 'x' : '-',
                (file_stat.st_mode & S_IROTH) ? 'r' : '-',
                (file_stat.st_mode & S_IWOTH) ? 'w' : '-',
                (file_stat.st_mode & S_IXOTH) ? 'x' : '-',
                file_stat.st_size,
                entry->d_name);

        write(data_client, data_buffer, strlen(data_buffer));
    }

    closedir(dir);
    close(data_client);
    close(client->data_socket);
    client->data_socket = -1;

    response = "Closing data connection. Requested file action successful\r\n";
    write(client->control_socket, response, strlen(response));
} 

void handle_command(client_info *client, char *buffer)
{
    char command[32] = {0};
    char arg[SIZE] = {0};

    // IT CUT THE BUFFER SO IT HAS NO SPACES CHARACTERS
    trim(buffer);
    // AND THEN IT GIVES HIM A NEW FORMAT
    sscanf(buffer, "%s %s", command, arg);

    printf("Received command: %s, arg: %s\n", command, arg);

    for (int i = 0; command[i]; i++) {
        command[i] = toupper(command[i]);
    }

    if (strcmp(command, "USER") == 0) {
        handle_user(client, arg);
    } else if (strcmp(command, "PASS") == 0) {
        handle_password(client, arg);
    } else if (!client->is_authenticated) {
        const char *response = "Not logged in\r\n";
        write(client->control_socket, response, strlen(response));
    } else if(strcmp(command, "SYST") == 0) {
        const char *response = "215 UNIX Type: L8\r\n";
        write(client->control_socket, response, strlen(response));
    } else if (strcmp(command, "LIST") == 0) {
        handle_list(client);
    } else if (strcmp(command, "PASV") == 0) {
        int pasv_sock = create_pasv_socket(client);
        if(pasv_sock < 0) {
            const char *pasv_err = "425 Can't open data connection\r\n";
            write(client->control_socket, pasv_err, strlen(pasv_err));
        }
        client->data_socket = pasv_sock;
    } else if (strcmp(command, "QUIT") == 0) {
        handle_quit(client);
    } else {
        const char *response = "Syntax error, command unrecognized\r\n";
        write(client->control_socket, response, strlen(response));
    }
}

int main()
{
    int server_fd;
    struct sockaddr_in address;
    int addr_len = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket failed");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt() failed");
        exit(1);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(CONTROL_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, addr_len) < 0) {
        perror("Bind failed in MAIN");
        exit(1);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(1);
    }

    printf("FTP Server listening on port: %d\n", CONTROL_PORT);

    while (1) {
        int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addr_len);

        if (client_socket < 0) {
            perror("Accept failed");
            continue;
        }
    
        client_info client = {
            .control_socket = client_socket,
            .is_authenticated = 0
        };

        const char *welcome = "220 Service ready for new user.\r\n";
        write(client_socket, welcome, strlen(welcome));

        char buffer[SIZE];

        while (1) {
            memset(buffer, 0, SIZE);
            int read_size = read(client_socket, buffer, SIZE - 1);

            if (read_size < 0) {
                printf("Client disconnected\n");
                break;
            }

            handle_command(&client, buffer);
        }

        close(client_socket);
    }

    return 0;
}
