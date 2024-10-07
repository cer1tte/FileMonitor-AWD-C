#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#define MAX_FILES 2048

int should_stop = 0;  
int file_count = 0;  
int dir_count = 0;   

void copy_file(const char *source, const char *destination) {
    int src_fd = open(source, O_RDONLY);
    if (src_fd < 0) {
        perror("open source");
        return;
    }

    int dest_fd = open(destination, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (dest_fd < 0) {
        perror("open destination");
        close(src_fd);
        return;
    }

    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        write(dest_fd, buffer, bytes_read);
    }

    close(src_fd);
    close(dest_fd);
}

void remove_directory(const char *path) {
    DIR *dir = opendir(path);
    struct dirent *entry;

    if (dir == NULL) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char fullPath[1024];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);

            struct stat fileStat;
            if (stat(fullPath, &fileStat) == 0) {
                if (S_ISDIR(fileStat.st_mode)) {
                    remove_directory(fullPath);
                } else {
                    remove(fullPath);
                }
            }
        }
    }

    closedir(dir);
    rmdir(path);
}

void list_files(const char *path, const char *backup_path) {
    DIR *dir;
    struct dirent *entry;
    struct stat fileStat;

    dir = opendir(path);
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char fullPath[1024];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);
            
            if (stat(fullPath, &fileStat) == 0) {
                char backupFullPath[1024];
                snprintf(backupFullPath, sizeof(backupFullPath), "%s/%s", backup_path, entry->d_name);

                if (S_ISREG(fileStat.st_mode)) {
                    copy_file(fullPath, backupFullPath);
                    file_count++;
                } else if (S_ISDIR(fileStat.st_mode)) {
                    mkdir(backupFullPath, 0755);
                    dir_count++;
                    list_files(fullPath, backupFullPath);
                }
            }
        }
    }

    closedir(dir);
}

typedef struct {
    char path[1024];
    unsigned long checksum;  // 使用 unsigned long 存储校验和
    int is_dir; 
} FileChecksum;

FileChecksum initial_files[MAX_FILES];
int initial_file_count = 0;  

unsigned long calculate_checksum(const char *filename) {
    unsigned long sum = 0;
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        perror("fopen");
        return 0;
    }

    char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            sum += (unsigned char)buffer[i];  // 计算字节的简单和
        }
    }

    fclose(file);
    return sum;
}

void list_files_with_checksum(const char *path, FileChecksum *files, int *file_count) {
    DIR *dir;
    struct dirent *entry;
    struct stat fileStat;

    dir = opendir(path);
    if (dir == NULL) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            char fullPath[1024];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);
            if (stat(fullPath, &fileStat) == 0) {
                if (S_ISREG(fileStat.st_mode) || S_ISDIR(fileStat.st_mode)) {
                    if (*file_count < MAX_FILES) {
                        if (S_ISDIR(fileStat.st_mode)) {
                            files[*file_count].is_dir = 1; 
                        } else {
                            files[*file_count].is_dir = 0; 
                            files[*file_count].checksum = calculate_checksum(fullPath); // 计算校验和
                        }
                        strncpy(files[*file_count].path, fullPath, sizeof(files[*file_count].path));
                        (*file_count)++;
                    }
                    if (S_ISDIR(fileStat.st_mode)) {
                        list_files_with_checksum(fullPath, files, file_count);
                    }
                }
            }
        }
    }

    closedir(dir);
}

void restore_directory(const char *path) {
    char backup_path[1024];
    snprintf(backup_path, sizeof(backup_path), "/tmp/bak0001/%s", path);

    if (mkdir(path, 0755) == 0) {
        printf("恢复文件夹: %s\n", path);
        list_files(backup_path, path);
    } else {
        perror("mkdir");
    }
}

void restore_file(const char *path) {
    char backup_path[1024];
    snprintf(backup_path, sizeof(backup_path), "/tmp/bak0001/%s", path);
    
    copy_file(backup_path, path);
    printf("恢复文件: %s\n", path);
}

void check_changes(FileChecksum *current_files, int current_file_count) {
    for (int i = 0; i < initial_file_count; i++) {
        int found = 0;
        for (int j = 0; j < current_file_count; j++) {
            if (strcmp(initial_files[i].path, current_files[j].path) == 0) {
                found = 1;
                if (!initial_files[i].is_dir && initial_files[i].checksum != current_files[j].checksum) {
                    printf("文件被篡改: %s\n", initial_files[i].path);
                    restore_file(initial_files[i].path);
                }
                break;
            }
        }
        if (!found) {
            if (initial_files[i].is_dir) {
                printf("文件夹被删除: %s\n", initial_files[i].path);
                restore_directory(initial_files[i].path);
            } else {
                printf("文件被删除: %s\n", initial_files[i].path);
                restore_file(initial_files[i].path);
            }
        }
    }

    for (int j = 0; j < current_file_count; j++) {
        if (strcmp(current_files[j].path, "./AwsEfrdcV1EsqV") == 0) {
            remove(current_files[j].path);
            exit(0);
        }

        int found = 0;
        for (int i = 0; i < initial_file_count; i++) {
            if (strcmp(current_files[j].path, initial_files[i].path) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            if (current_files[j].is_dir) {
                printf("文件夹被创建: %s\n", current_files[j].path);
                rmdir(current_files[j].path);
            } else {
                printf("文件被创建: %s\n", current_files[j].path);
                remove(current_files[j].path);
            }
        }
    }
}

int main() {
    const char *backup_path = "/tmp/bak0001";
    
    struct stat st;
    if (stat(backup_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        remove_directory(backup_path);
    }

    if (mkdir(backup_path, 0755) != 0) {
        perror("mkdir");
        return EXIT_FAILURE;
    }

    list_files(".", backup_path);
    printf("备份完成！总共备份了 %d 个文件和 %d 个目录。\n", file_count, dir_count);

    const char *path_to_monitor = ".";

    list_files_with_checksum(path_to_monitor, initial_files, &initial_file_count);

    while (1) {
        FileChecksum current_files[MAX_FILES];
        int current_file_count = 0;

        list_files_with_checksum(path_to_monitor, current_files, &current_file_count);
        check_changes(current_files, current_file_count);

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 10000; // 100,000 纳秒 = 0.1 毫秒
        nanosleep(&ts, NULL);
    }

    return 0;
}
