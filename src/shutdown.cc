// #include <netinet/in.h>
#include <cstddef>
#include <cstdio>
#include <csignal>
#include <unistd.h>
#include <cstring>
#include <string>
#include <iostream>

#include <sys/reboot.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "control-cmds.h"
#include "service-constants.h"

// shutdown:  shut down the system
// This utility communicates with the dinit daemon via a unix socket (/dev/initctl).

void do_system_shutdown(ShutdownType shutdown_type);
static void unmount_disks();
static void swap_off();

int main(int argc, char **argv)
{
    using namespace std;
    
    bool show_help = false;
    bool sys_shutdown = false;
    
    auto shutdown_type = ShutdownType::POWEROFF;
        
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--help") == 0) {
                show_help = true;
                break;
            }
            
            if (strcmp(argv[i], "--system") == 0) {
                sys_shutdown = true;
            }
            else if (strcmp(argv[i], "-r") == 0) {
                shutdown_type = ShutdownType::REBOOT;
            }
            else if (strcmp(argv[i], "-h") == 0) {
                shutdown_type = ShutdownType::HALT;
            }
            else if (strcmp(argv[i], "-p") == 0) {
                shutdown_type = ShutdownType::POWEROFF;
            }
            else {
                cerr << "Unrecognized command-line parameter: " << argv[i] << endl;
                return 1;
            }
        }
        else {
            // time argument? TODO
            show_help = true;
        }
    }

    if (show_help) {
        cout << "dinit-shutdown :   shutdown the system" << endl;
        cout << "  --help           : show this help" << endl;
        cout << "  -r               : reboot" << endl;
        cout << "  -h               : halt system" << endl;
        cout << "  -p               : power down (default)" << endl;
        cout << "  --system         : perform shutdown immediately, instead of issuing shutdown" << endl;
        cout << "                     command to the init program. Not recommended for use" << endl;
        cout << "                     by users." << endl;
        return 1;
    }
    
    if (sys_shutdown) {
        do_system_shutdown(shutdown_type);
        return 0;
    }
    
    int socknum = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socknum == -1) {
        perror("socket");
        return 1;
    }

    const char *naddr = "/dev/dinitctl";
    
    struct sockaddr_un name;
    name.sun_family = AF_UNIX;
    strcpy(name.sun_path, naddr);
    int sunlen = offsetof(struct sockaddr_un, sun_path) + strlen(naddr) + 1; // family, (string), nul
    
    int connr = connect(socknum, (struct sockaddr *) &name, sunlen);
    if (connr == -1) {
        perror("connect");
        return 1;
    }
    
    // Build buffer;
    //uint16_t sname_len = strlen(service_name);
    int bufsize = 2;
    char * buf = new char[bufsize];
    
    buf[0] = DINIT_CP_SHUTDOWN;
    buf[1] = static_cast<char>(shutdown_type);
    
    cout << "Issuing shutdown command..." << endl; // DAV
    
    // TODO make sure to write the whole buffer
    int r = write(socknum, buf, bufsize);
    if (r == -1) {
        perror("write");
    }
    
    // Wait for ACK/NACK
    r = read(socknum, buf, 1);
    // TODO: check result
    
    return 0;
}

void do_system_shutdown(ShutdownType shutdown_type)
{
    using namespace std;
    
    int reboot_type = 0;
    if (shutdown_type == ShutdownType::REBOOT) reboot_type = RB_AUTOBOOT;
    else if (shutdown_type == ShutdownType::POWEROFF) reboot_type = RB_POWER_OFF;
    else reboot_type = RB_HALT_SYSTEM;
    
    // Write to console rather than any terminal, since we lose the terminal it seems:
    close(STDOUT_FILENO);
    int consfd = open("/dev/console", O_WRONLY);
    if (consfd != STDOUT_FILENO) {
        dup2(consfd, STDOUT_FILENO);
    }
    
    cout << "Sending TERM/KILL to all processes..." << endl; // DAV
    
    // Send TERM/KILL to all (remaining) processes
    kill(-1, SIGTERM);
    sleep(1);
    kill(-1, SIGKILL);
    
    // cout << "Sending QUIT to init..." << endl; // DAV
    
    // Tell init to exec reboot:
    // TODO what if it's not PID=1? probably should have dinit pass us its PID
    // kill(1, SIGQUIT);
    
    // TODO can we wait somehow for above to work?
    // maybe have a pipe/socket and we read from our end...
    
    // TODO: close all ancillary file descriptors.
    
    // perform shutdown
    cout << "Turning off swap..." << endl;
    swap_off();
    cout << "Unmounting disks..." << endl;
    unmount_disks();
    sync();
    
    cout << "Issuing shutdown via kernel..." << endl;
    reboot(reboot_type);
}

static void unmount_disks()
{
    pid_t chpid = fork();
    if (chpid == 0) {
        // umount -a -r
        //  -a : all filesystems (except proc)
        //  -r : mount readonly if can't unmount
        execl("/bin/umount", "/bin/umount", "-a", "-r", nullptr);
    }
    else if (chpid > 0) {
        int status;
        waitpid(chpid, &status, 0);
    }
}

static void swap_off()
{
    pid_t chpid = fork();
    if (chpid == 0) {
        // swapoff -a
        execl("/sbin/swapoff", "/sbin/swapoff", "-a", nullptr);
    }
    else if (chpid > 0) {
        int status;
        waitpid(chpid, &status, 0);
    }
}