#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define READ_UID_PATH       "./bin/readUID"
#define READ_BLOCK_PATH     "./bin/readBlock"
#define READ_SECTOR_PATH    "./bin/readSector"

static void print_help();
static int run_program(const char *path, char *const child_argv[]);

int main(void)
{
    char line[128];

    fprintf(stdout, "RFID Control Terminal\n");
    fprintf(stdout, "Type 'help' for commands.\n");
    fprintf(stdout, "Type 'exit' or 'quit' to terminate.\n\n");


    while (1) {
        char *cmd = NULL;

        fprintf(stdout, "rfidctl> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        line[strcspn(line, "\n")] = '\0';       // fgets()가 저장한 '\n' 제거

        cmd = strtok(line, " ");
        if (cmd == NULL) {
            continue;
        }

        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            break;
        }
        else if (strcmp(cmd, "help") == 0) {
            print_help();
        }
        else if (strcmp(cmd, "uid") == 0) {
            char *extra  = strtok(NULL, " ");
            
            if(extra != NULL) {
                fprintf(stdout, "Usage: uid\n");
                continue;
            }

            char *args[] = {
                READ_UID_PATH,
                NULL
            };

            run_program(READ_UID_PATH, args);

        }
        else if (strcmp(cmd, "readblock") == 0) {
            char *sector = strtok(NULL, " ");
            char *block  = strtok(NULL, " ");
            char *extra  = strtok(NULL, " ");

            if (sector == NULL || block == NULL || extra != NULL) {
                fprintf(stdout, "Usage: readblock <sector> <block>\n");
                continue;
            }

            if (strtoul(sector, NULL, 0) > 15 || strtoul(block, NULL, 0) > 3) {
                fprintf(stdout, "Invalid sector/block (0 <= sector < 15, 0 <= block < 4\n");
                continue;
            }

            char *args[] = {
                READ_BLOCK_PATH,
                sector,
                block,
                NULL
            };

            run_program(READ_BLOCK_PATH, args);

        }
        else if (strcmp(cmd, "readsector") == 0) {
            char *sector = strtok(NULL, " ");
            char *extra  = strtok(NULL, " ");

            if (sector == NULL || extra != NULL) {
                fprintf(stdout, "Usage: readsector <sector>\n");
                continue;
            }

            if (strtoul(sector, NULL, 0) > 15) {
                fprintf(stdout, "Invalid sector (0 <= sector < 15)\n");
                continue;
            }

            char *args[] = {
                READ_SECTOR_PATH,
                sector,
                NULL
            };

            run_program(READ_SECTOR_PATH, args);

        }
        else {
            printf("Unknown command: %s\n", cmd);
            printf("Type 'help' for commands.\n");
        }
    }

    return EXIT_SUCCESS;
}

static void print_help(void)
{
    printf("Commands:\n");
    printf("  uid                               Read UID\n");
    printf("  readblock <sector> <block>        Read block\n");
    printf("  readsector <sector>               Read sector\n");
    printf("  help                              Show help\n");
    printf("  exit | quit                       erminate rfidctl\n");
    printf("\n");

    printf("Examples:\n");
    printf("  uid\n");
    printf("  readblock 0 0\n");
    printf("  readsector 0\n");
}

static int run_program(const char *path, char *const child_argv[])
{
    pid_t pid;
    int status;

    pid = fork();

    if (pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }
    else if (pid == 0) {
        execv(path, child_argv);

        perror("execv");    // execv 성공 시 여기로 돌아오지 않기 때문
        _exit(EXIT_FAILURE);
    }
    else {
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}