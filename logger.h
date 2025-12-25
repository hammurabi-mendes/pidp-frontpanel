#ifndef LOGGER_H
#define LOGGER_H

#include <syslog.h>

#include <cstdio>
#include <cstdarg>

class Logger;

extern Logger *logger;

class Logger {
private:
	bool use_syslog;
	bool initialized;

public:
	Logger();
	~Logger();

	void init(bool use_syslog, const char *ident);
	void finish();

	void info(const char *format, ...) __attribute__((format(printf, 2, 3)));
	void error(const char *format, ...) __attribute__((format(printf, 2, 3)));
	void debug(const char *format, ...) __attribute__((format(printf, 2, 3)));

	bool is_syslog() const { return use_syslog; }
};

#endif /* LOGGER_H */
