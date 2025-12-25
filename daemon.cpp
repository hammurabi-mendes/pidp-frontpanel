#include "daemon.h"

#include <cstdlib>
#include <csignal>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

bool daemonize(const char *working_directory) {
	// Fork and exit the parent process
	// This is to ensure the current process is not a session leader
	pid_t pid = fork();

	if(pid < 0) {
		return false;
	}

	if(pid > 0) {
		exit(0);
	}

	// Create a new session and process group
	// The current process is the leader of the new process group, with no controlling terminal
	if(setsid() < 0) {
		return false;
	}

	// Ignore SIGHUP
	signal(SIGHUP, SIG_IGN);

	// Fork and exit the parent process
	// This is to ensure that we have not reacquired a controlling terminal
	pid = fork();

	if(pid < 0) {
		return false;
	}

	if(pid > 0) {
		exit(0);
	}

	// Set file permissions, change working directory

	umask(0);

	if(working_directory != nullptr) {
		if(chdir(working_directory) < 0) {
			return false;
		}
	}

	// Redirect standard file descriptors to /dev/null

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	int dev_null = open("/dev/null", O_RDWR);

	if(dev_null < 0) {
		return false;
	}

	dup2(dev_null, STDIN_FILENO);
	dup2(dev_null, STDOUT_FILENO);
	dup2(dev_null, STDERR_FILENO);

	if(dev_null > STDERR_FILENO) {
		close(dev_null);
	}

	return true;
}
