#include "configuration.h"

#include <fstream>
#include <sstream>
#include <cstdio>

using std::string;
using std::vector;
using std::ifstream;
using std::stringstream;

Configuration::Configuration(const string &configuration_path):
	configuration_path{configuration_path},
	initialized{false} {
}

bool Configuration::init() {
	if(initialized) {
		return true;
	}

	entries.clear();

	ifstream file(configuration_path);

	if(!file.is_open()) {
		std::fprintf(stderr, "[CONFIG] Failed to open configuration file: %s\n", configuration_path.c_str());
		return false;
	}

	string line;
	int line_number = 0;

	while(std::getline(file, line)) {
		line_number++;

		// Skip empty lines and comments
		if(line.empty() || line[0] == '#' || line[0] == ';') {
			continue;
		}

		stringstream stream(line);
		ConfigurationEntry entry;

		// Parse switch_code (octal)
		string switch_code_str;
		if(!std::getline(stream, switch_code_str, ',')) {
			std::fprintf(stderr, "[CONFIG] Line %d: missing switch_code\n", line_number);
			continue;
		}

		// Parse octal switch code
		if(std::sscanf(switch_code_str.c_str(), "%o", &entry.switch_code) != 1) {
			std::fprintf(stderr, "[CONFIG] Line %d: invalid octal switch_code: %s\n", line_number, switch_code_str.c_str());
			continue;
		}

		// Parse directory
		if(!std::getline(stream, entry.directory, ',')) {
			std::fprintf(stderr, "[CONFIG] Line %d: missing directory\n", line_number);
			continue;
		}

		// Parse configuration_file
		if(!std::getline(stream, entry.configuration_file, ',')) {
			std::fprintf(stderr, "[CONFIG] Line %d: missing configuration_file\n", line_number);
			continue;
		}

		// Parse boot_device
		if(!std::getline(stream, entry.boot_device, ',')) {
			std::fprintf(stderr, "[CONFIG] Line %d: missing boot_device\n", line_number);
			continue;
		}

		// Trim whitespace from strings
		auto trim = [](string &s) {
			size_t start = 0;

			while(start < s.length() && std::isspace(s[start])) {
				start++;
			}

			size_t end = s.length();

			while(end > start && std::isspace(s[end - 1])) {
				end--;
			}

			s = s.substr(start, end - start);
		};

		trim(entry.directory);
		trim(entry.configuration_file);
		trim(entry.boot_device);

		entries.push_back(entry);

		std::printf("[CONFIG] Loaded entry: switch=%06o, dir=%s, config=%s, boot=%s\n",
			entry.switch_code, entry.directory.c_str(),
			entry.configuration_file.c_str(), entry.boot_device.c_str());
	}

	file.close();

	if(entries.empty()) {
		std::fprintf(stderr, "[CONFIG] No valid entries found in configuration file\n");
		return false;
	}

	initialized = true;

	return true;
}

bool Configuration::reload() {
	initialized = false;
	return init();
}

const ConfigurationEntry* Configuration::find_entry(uint32_t switch_code) const {
	if(!initialized) {
		return nullptr;
	}

	for(const auto &entry : entries) {
		if(entry.switch_code == switch_code) {
			return &entry;
		}
	}

	return nullptr;
}
