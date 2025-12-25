#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include <string>
#include <vector>
#include <cstdint>

using std::string;
using std::vector;

struct ConfigurationEntry {
	uint32_t switch_code;
	string directory;
	string configuration_file;
	string boot_device;
};

class Configuration {
private:
	vector<ConfigurationEntry> entries;
	string configuration_path;

	bool initialized;

public:
	Configuration(const string &configuration_path);

	bool init();
	bool reload();

	const ConfigurationEntry* find_entry(uint32_t switch_code) const;

	bool is_initialized() const { return initialized; }
};

#endif /* CONFIGURATION_H */
