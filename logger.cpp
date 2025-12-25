#include "logger.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

Logger *logger = nullptr;

Logger::Logger():
	use_syslog{false},
	initialized{false} {
}

Logger::~Logger() {
	finish();
}

void Logger::init(bool use_syslog_flag, const char *ident) {
	if(initialized) {
		return;
	}

	use_syslog = use_syslog_flag;

	if(use_syslog) {
		openlog(ident, LOG_PID | LOG_NDELAY, LOG_DAEMON);
	}

	initialized = true;
}

void Logger::finish() {
	if(!initialized) {
		return;
	}

	if(use_syslog) {
		closelog();
	}

	initialized = false;
}

void Logger::info(const char *format, ...) {
	va_list arguments;

	va_start(arguments, format);

	if(use_syslog) {
		vsyslog(LOG_INFO, format, arguments);
	}
	else {
		vprintf(format, arguments);
		fflush(stdout);
	}

	va_end(arguments);
}

void Logger::error(const char *format, ...) {
	va_list arguments;

	va_start(arguments, format);

	if(use_syslog) {
		vsyslog(LOG_ERR, format, arguments);
	}
	else {
		vfprintf(stderr, format, arguments);
		fflush(stderr);
	}

	va_end(arguments);
}

void Logger::debug(const char *format, ...) {
	va_list arguments;

	va_start(arguments, format);

	if(use_syslog) {
		vsyslog(LOG_DEBUG, format, arguments);
	}
	else {
		vprintf(format, arguments);
		fflush(stdout);
	}

	va_end(arguments);
}
