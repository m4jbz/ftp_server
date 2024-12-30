#include <sys/socket.h> // FOR SOCKET RELATED FUNCTIONS
#include <netinet/in.h> // FOR SOCKET ADDRESS
#include <sys/stat.h>   // FOR FILE INFORMATION
#include <dirent.h>     // FOR READING DIRECTORY CONTENTS
#include <stdlib.h>     // FOR HANDLING MEMORY AND OTHER STUFF
#include <string.h>     // FOR STRING MANIPULATION
#include <unistd.h>     // FOR HANDLING OF FILE AND PROCESS
#include <stdio.h>      // FOR PRINTING ERRORS WITH perror()
#include <ctype.h>      // FOR STRING AND CHARACTER MANIPULATION

#define CONTROL_PORT 21 // FTP PORT
#define SIZE 1024 // SIZE OF THE BUFFER

// INFORMATION OF THE CLIENT
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

    // COPY THE USERNAME CONTENT INTO CLIENT->USERNAME VARIABLE,
    // THE -1 AT THE END IS FOR THE \'0' CHARACTER
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

// HANDLES THE PASV COMMAND, THIS IS NEEDED SO
// WE DON'T GET ANY PROBLEMS WITH THE FIREWALL OF THE SYSTEM
int create_pasv_socket(client_info *client)
{
    // CREATE SOCKET AND ADDRESS
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
    // LET THE SYSTEM TO CHOOSE THE PORT
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

    // GET THE PORT
    if (getsockname(server_fd, (struct sockaddr *)&addr, &addr_len) < 0) {
        perror("getsockname failed");
        close(server_fd);
        return -1;
    }
    int port = ntohs(addr.sin_port);

    char response[128];
    snprintf(response, sizeof(response), "227 Entering Passive Mode (127,0,0,1,%d,%d)\r\n", port / 256, port % 256);

    if (write(client->control_socket, response, strlen(response)) == -1) {
        perror("Write failed in PASV");
        close(server_fd);
        exit(1);
    }

    return server_fd;
}

void handle_list(client_info *client)
{
    // CREATE A NEW VARIABLE FOR THE DIRECTORY
    DIR *dir;
    struct dirent *entry;
    char data_buffer[SIZE];
    
    // CHECK IF WE HAVE A SOCKET FOR PASV
    if (client->data_socket == -1) {
        const char *response = "425 Use PASV or PORT first\r\n";
        write(client->control_socket, response, strlen(response));
        return;
    }
    
    const char *response = "150 File status okay; about to open data connection.\r\n";
    if (write(client->control_socket, response, strlen(response)) == -1) {
        perror("Write failed in LIST");
        exit(1);
    }

    // ACCEPT THE CLIENT CONNECTION ON THE PASSIVE SOCKET
    // IT WILL WAIT UNTIL THE USER ATTEMPT TO CONNECT
    int data_client = accept(client->data_socket, NULL, NULL); 
    // USES NULL BECAUSE THE ADDRESS
    // INFORMATION IS NOT IMPORTANT HERE
    if (data_client < 0) {
        perror("Accept failed in LIST");
        close(client->data_socket);
        client->data_socket = -1;
        return;
    }

    // HERE YOU PUT YOUR REMOTE DIRECTORY
    dir = opendir("/home/marco/");
    if (dir == NULL) {
        const char *err = "450 Requested file action not taken\r\n";
        write(client->control_socket, err, strlen(err));
        close(data_client);
        return;
    }

    // READ ALL ENTRIES IN THE DIRECTORY
    while ((entry = readdir(dir)) != NULL) {
        struct stat file_stat;
        stat(entry->d_name, &file_stat);

        // FORMAT: "PERMISSIONS SIZE NAME\R\N"
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

        if (write(data_client, data_buffer, strlen(data_buffer)) == -1) {
            perror("Write failed while reading the directory");
            exit(1);
        }
    }

    closedir(dir);
    close(data_client);
    close(client->data_socket);
    client->data_socket = -1;

    response = "Closing data connection. Requested file action successful\r\n";
    if (write(client->control_socket, response, strlen(response)) == -1) {
        perror("Write failed while closing the connection");
        exit(1);
    }
} 

// THIS HANDLES EACH FTP COMMAND THAT WE ADD
// BY NOW IT KINDA SUCKS THE WAY THAT I'M HANDLING
// THIS BUT I'M GOING TO CHANGE IT
void handle_command(client_info *client, char *buffer)
{
    // SEPARATE THE BUFFER INTO THE 
    // ACTUAL COMMAND AND THE ARGUMENTS
    char command[32] = {0};
    char arg[SIZE] = {0};

    // CUT THE BUFFER SO IT HAS NO SPACES CHARACTERS
    trim(buffer);
    // GIVE IT A NEW FORMAT
    sscanf(buffer, "%s %s", command, arg);

    printf("Received command: %s, arg: %s\n", command, arg);

    // MAKE ALL COMMANDS UPPERCASE
    for (int i = 0; command[i]; i++) {
        command[i] = toupper(command[i]);
    }

    // UGLY ASS HANDLING I KNOW
    if (strcmp(command, "USER") == 0) {
        handle_user(client, arg);
    } else if (strcmp(command, "PASS") == 0) {
        handle_password(client, arg);
    } else if (!client->is_authenticated) {
        const char *response = "Not logged in\r\n";
        write(client->control_socket, response, strlen(response));
    } else if(strcmp(command, "SYST") == 0) {
        // THIS IS NECESSARY FOR CURRENT FTP CLIENTS
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
    // CREATE SOCKET
    int server_fd;
    struct sockaddr_in address;
    int addr_len = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket failed");
        exit(1);
    }

    // SET SOCKET OPTIONS (ALLOW REUSE OF ADDRESS)
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt() failed");
        exit(1);
    }

    // CREATE ADDRESS
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(CONTROL_PORT); // IT USES PORT 21

    if (bind(server_fd, (struct sockaddr *)&address, addr_len) < 0) {
        perror("Bind failed in MAIN");
        exit(1);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed in MAIN");
        exit(1);
    }

    printf("FTP Server listening on port: %d\n", CONTROL_PORT);

    while (1) {
        int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addr_len);
        if (client_socket < 0) {
            perror("Accept failed in MAIN");
            continue;
        }
    
        // CREATE A NEW CLIENT AND INITIALIZE IT
        client_info client = {
            .control_socket = client_socket,
            .data_socket = 1,
            .is_authenticated = 0
        };

        const char *welcome = "220 Service ready for new user.\r\n";
        if (write(client_socket, welcome, strlen(welcome)) == -1) {
            perror("Write failed in MAIN");
            exit(1);
        }

        char buffer[SIZE];

        while (1) {
            // INITIATE THE BUFFER WITH 0
            memset(buffer, 0, SIZE);
            // READ AND STORE THE CLIENT INPUT IN BYTES
            int read_size = read(client_socket, buffer, SIZE - 1);

            // CHECK IF AN ERROR HAS OCURRED OR THE CLIENT HAS LEFT THE SERVER
            if (read_size < 0) {
                printf("Client disconnected\n");
                break;
            }

            // HANDLE THE INPUT (BUFFER)
            handle_command(&client, buffer);
        }

        close(client_socket);
    }

    return 0;
}
