#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>



#define DEFAULT_FIFO "/tmp/log_server_fifo"
#define DEFAULT_LOG "/tmp/log_server.log"
#define BUFFER_SIZE 1024
#define DEFAULT_ALARM_INTERVAL 5


typedef struct {
    unsigned long messages, 
                  bytes,
                  alarms;
} Stats;

Stats stats = {0, 0, 0};
int alarm_interval_global = DEFAULT_ALARM_INTERVAL;


volatile sig_atomic_t got_sigint = 0;
volatile sig_atomic_t got_sigterm = 0;
volatile sig_atomic_t got_sigusr1 = 0;
volatile sig_atomic_t got_sigalrm = 0;
volatile sig_atomic_t got_sighup = 0;

void handle_signal(int signo) {
    if (signo == SIGINT) {
        got_sigint = 1;
    } else if (signo == SIGTERM) {
        got_sigterm = 1;
    } else if (signo == SIGUSR1) {
        got_sigusr1 = 1;
    } else if (signo == SIGALRM) {
        got_sigalrm = 1;
    } else if (signo == SIGHUP) {
        got_sighup = 1;
    }
}



FILE *log_stream = NULL;

void log_msg(const char *format, ...) {
    va_list args;

    va_start(args, format);
    vfprintf(log_stream, format, args);
    va_end(args);

    fflush(log_stream);
}

void print_stats(const char *reason) {
    log_msg("\n[stats: %s]\n", reason);
    log_msg("messages: %lu\n", stats.messages);
    log_msg("bytes: %lu\n", stats.bytes);
    log_msg("alarms: %lu\n", stats.alarms);
}

int daemonize_process(void) {
    pid_t pid;
    pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }


    if (setsid() == -1) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }


    if (chdir("/") == -1) {
        return -1;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);


    if (open("/dev/null", O_RDONLY) == -1) {
        return -1;
    }

    if (open("/dev/null", O_WRONLY) == -1) {
        return -1;
    }

    if (open("/dev/null", O_WRONLY) == -1) {
        return -1;
    }
    return 0;
}




void process_signals(void) {
    if (got_sigalrm) {
        got_sigalrm = 0;
        stats.alarms++;
        log_msg("[alarm_diagnostic] server is alive\n");
        alarm((unsigned int)alarm_interval_global);
    }

    if (got_sigusr1) {
        got_sigusr1 = 0;
        print_stats("SIGUSR1");
    }
}

int process_sighup(const char *log_path, int *is_daemon) {
    if (!got_sighup) {
        return 0;
    }
    got_sighup = 0;


    if (*is_daemon) {
        log_msg("[signal] SIGHUP ignored: already daemon\n");
        return 0;
    }


    if (daemonize_process() == -1) {
        log_msg("daemonize error: %s\n", strerror(errno));
        return -1;
    }

    if (log_stream != NULL) {
        fclose(log_stream);
    }

    log_stream = fopen(log_path, "a");
    if (log_stream == NULL) {
        return -1;
    }

    *is_daemon = 1;


    log_msg("[daemon] switched to daemon mode via SIGHUP\n");
    print_stats("SIGHUP daemonization");

    return 0;
}



int main(int argc, char* argv[]) {
    const char *fifo_path = DEFAULT_FIFO,
               *log_path = DEFAULT_LOG;
    int alarm_interval = DEFAULT_ALARM_INTERVAL;
    int daemon_mode = 0;
    int opt;
    while ((opt = getopt(argc, argv, "f:l:n:d")) != -1) {
        switch (opt) {
            case 'f':
                fifo_path = optarg;
                break;
            case 'l':
                log_path = optarg;
                break;  
            case 'n':
                alarm_interval = atoi(optarg);
                if (alarm_interval <= 0) {
                    fprintf(stderr, "Alarm interval can't be negative or zero\n");
                    return EXIT_FAILURE;
                }

                break;
            case 'd':
                daemon_mode = 1;
                break;
            default:
                fprintf(stderr, "Wrong format. Usage: %s [-f fifo_path] [-l log_path] [-n seconds] [-d]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    alarm_interval_global = alarm_interval;


    if (daemon_mode) {
        if (daemonize_process() == -1) {
            perror("daemonize");
            return EXIT_FAILURE;
        }
    }

    if (daemon_mode) {
        log_stream = fopen(log_path, "a");
        if (log_stream == NULL) {
            perror("fopen");
            return EXIT_FAILURE;
        }
    } else {
        log_stream = stdout;
    }
    



    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        log_msg("sigaction SIGINT error: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        log_msg("sigaction SIGTERM error: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        log_msg("sigaction SIGUSR1 error: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        log_msg("sigaction SIGALRM error: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if (sigaction(SIGHUP, &sa, NULL) == -1) {
        log_msg("sigaction SIGHUP error: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }


    struct sigaction ignore_sa;
    memset(&ignore_sa, 0, sizeof(ignore_sa));

    ignore_sa.sa_handler = SIG_IGN;
    sigemptyset(&ignore_sa.sa_mask);

    if (sigaction(SIGQUIT, &ignore_sa, NULL) == -1) {
        log_msg("sigaction SIGQUIT error: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }




    int fifo_fd;
    char buffer[BUFFER_SIZE + 1];
    int created_fifo = 0;

    if (mkfifo(fifo_path, 0600) == -1) {
        if (errno == EEXIST) {
            struct stat st;

            if (stat(fifo_path, &st) == -1) {
                log_msg("stat error: %s\n", strerror(errno));
                return EXIT_FAILURE;
            }

            if (!S_ISFIFO(st.st_mode)) {
                log_msg("Error: %s exists but is not FIFO\n", fifo_path);
                return EXIT_FAILURE;
            }

            log_msg("FIFO already exists: %s\n", fifo_path);
        } else {
            log_msg("mkfifo error: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
    } else {
        created_fifo = 1;
        log_msg("FIFO created: %s\n", fifo_path);
    }
    log_msg("Log server started.\n");
    alarm((unsigned int)alarm_interval);
    



    log_msg("\nWaiting for data\n");
    int is_daemon = daemon_mode;
    char last_char = '\n';
    while (!got_sigint && !got_sigterm) {
        process_signals();
        if (process_sighup(log_path, &is_daemon) == -1) {
            return EXIT_FAILURE;
        }
        if (got_sigint || got_sigterm) {
            break;
        }

        fifo_fd = open(fifo_path, O_RDONLY);

        process_signals();
        if (process_sighup(log_path, &is_daemon) == -1) {
            if (fifo_fd != -1) {
                close(fifo_fd);
            }
            return EXIT_FAILURE;
        }
        if (got_sigint || got_sigterm) {
            if (fifo_fd != -1) {
                close(fifo_fd);
            }
            break;
        }

        if (fifo_fd == -1) {
            if (errno == EINTR) {
                process_signals();

                if (process_sighup(log_path, &is_daemon) == -1) {
                    return EXIT_FAILURE;
                }   

                if (got_sigint || got_sigterm) {
                    break;
                }

                continue;
            }

            log_msg("open error: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }


        ssize_t bytes_read;
        int got_data = 0;
        while (1) {
            process_signals();
            if (process_sighup(log_path, &is_daemon) == -1) {
                close(fifo_fd);
                return EXIT_FAILURE;
            }
            if (got_sigterm) {
                break;
            }

            bytes_read = read(fifo_fd, buffer, BUFFER_SIZE);

            process_signals();
            if (process_sighup(log_path, &is_daemon) == -1) {
                close(fifo_fd);
                return EXIT_FAILURE;
            }
        
            

            if (bytes_read > 0) {
                got_data = 1;
                stats.bytes += (unsigned long)bytes_read;

                
                last_char = buffer[bytes_read - 1];
                buffer[bytes_read] = '\0';
                log_msg("%s", buffer);


                process_signals();
                if (process_sighup(log_path, &is_daemon) == -1) {
                    close(fifo_fd);
                    return EXIT_FAILURE;
                }
                if (got_sigterm) {
                    break;
                }
                continue;
            }

            if (bytes_read == 0) {
                break;
            }

            if (bytes_read == -1) {
                if (errno == EINTR) {
                    process_signals();

                    if (process_sighup(log_path, &is_daemon) == -1) {
                        return EXIT_FAILURE;
                    }

                
                    if (got_sigterm) {
                        break;
                    }

                    if (got_sigint) {
                        continue;
                    }

                    continue;
                }

                log_msg("read error: %s\n", strerror(errno));
                close(fifo_fd);
                

                if (created_fifo) {
                    unlink(fifo_path);
                }

                if (log_stream != NULL) {
                    fclose(log_stream);
                }

                return EXIT_FAILURE;
            }
        }

        if (got_data && last_char != '\n') {
            log_msg("\n");
        }

        
        close(fifo_fd);
        if (got_data) {
            stats.messages++;
        }

        if (got_sigterm || got_sigint) {
            break;
        }

        log_msg("\nWaiting for data\n");
    }




    if (got_sigterm) {
        log_msg("\nServer stopped by SIGTERM.\n");
    } else if (got_sigint) {
        log_msg("\nServer stopped by SIGINT.\n");
    } else {
        log_msg("\nServer stopped.\n");
    }

    print_stats("shutdown");


    if (created_fifo) {
        unlink(fifo_path);
    }

    if (log_stream != NULL) {
        fclose(log_stream);
    }

    return EXIT_SUCCESS;
}