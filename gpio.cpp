#include "gpio.h"

#include <gpiod.h>
#include <cstring>

using std::string;
using std::vector;

// =============================================================
// GPIOChip
// =============================================================

GPIOChip::GPIOChip(const string &chip_path):
	chip_path(chip_path),
	chip(nullptr),
	initialized(false) {
}

GPIOChip::~GPIOChip() {
	finish();
}

bool GPIOChip::init() {
	if(initialized) {
		return true;
	}

	chip = gpiod_chip_open(chip_path.c_str());

	if(!chip) {
		return false;
	}

	initialized = true;

	return true;
}

void GPIOChip::finish() {
	if(chip) {
		gpiod_chip_close(chip);
		chip = nullptr;
	}

	initialized = false;
}

// =============================================================
// GPIO
// =============================================================

GPIO::GPIO(GPIOChip *chip, unsigned int pin_number):
	chip(chip),
	pin_number(pin_number),
	current_mode(PinMode::Input),
	current_pull(PullMode::None),
	request(nullptr),
	initialized(false) {
}

GPIO::~GPIO() {
	finish();
}

bool GPIO::init() {
	if(initialized) {
		return true;
	}

	if(!chip || !chip->is_initialized()) {
		return false;
	}

	initialized = true;

	return true;
}

void GPIO::finish() {
	if(request) {
		gpiod_line_request_release(request);
		request = nullptr;
	}

	initialized = false;
}

bool GPIO::pin_mode(PinMode mode, PullMode pull) {
	if(!initialized || !chip) {
		return false;
	}

	if(request) {
		gpiod_line_request_release(request);
		request = nullptr;
	}

	gpiod_line_settings *settings = gpiod_line_settings_new();

	if(!settings) {
		return false;
	}

	switch(mode) {
		case PinMode::Input:
			gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
			break;

		case PinMode::Output:
			gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
			gpiod_line_settings_set_drive(settings, GPIOD_LINE_DRIVE_PUSH_PULL);
			gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
			break;

		case PinMode::OpenDrain:
			gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
			gpiod_line_settings_set_drive(settings, GPIOD_LINE_DRIVE_OPEN_DRAIN);
			gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
			break;

		case PinMode::OpenSource:
			gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
			gpiod_line_settings_set_drive(settings, GPIOD_LINE_DRIVE_OPEN_SOURCE);
			gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
			break;
	}

	switch(pull) {
		case PullMode::None:
			gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_DISABLED);
			break;

		case PullMode::PullUp:
			gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
			break;

		case PullMode::PullDown:
			gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_DOWN);
			break;
	}

	gpiod_line_config *line_config = gpiod_line_config_new();

	if(!line_config) {
		gpiod_line_settings_free(settings);
		return false;
	}

	int return_value = gpiod_line_config_add_line_settings(line_config, &pin_number, 1, settings);

	if(return_value) {
		gpiod_line_config_free(line_config);
		gpiod_line_settings_free(settings);
		return false;
	}

	gpiod_request_config *request_config = gpiod_request_config_new();

	if(!request_config) {
		gpiod_line_config_free(line_config);
		gpiod_line_settings_free(settings);
		return false;
	}

	gpiod_request_config_set_consumer(request_config, "GPIO");

	request = gpiod_chip_request_lines(chip->get_chip(), request_config, line_config);

	gpiod_request_config_free(request_config);
	gpiod_line_config_free(line_config);
	gpiod_line_settings_free(settings);

	if(!request) {
		return false;
	}

	current_mode = mode;
	current_pull = pull;

	return true;
}

bool GPIO::pin_set(bool flag) {
	if(!initialized || !request) {
		return false;
	}

	if(current_mode == PinMode::Input) {
		return false;
	}

	gpiod_line_value value = flag ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;

	int return_value = gpiod_line_request_set_value(request, pin_number, value);

	return (return_value == 0);
}

bool GPIO::pin_get() {
	if(!initialized || !request) {
		return false;
	}

	gpiod_line_value value = gpiod_line_request_get_value(request, pin_number);

	return (value == GPIOD_LINE_VALUE_ACTIVE);
}

// =============================================================
// GPIOGroup
// =============================================================

GPIOGroup::GPIOGroup(GPIOChip *chip, const vector<unsigned int> &pins):
	chip(chip),
	pin_numbers(pins),
	current_mode(PinMode::Input),
	current_pull(PullMode::None),
	request(nullptr),
	initialized(false) {
}

GPIOGroup::~GPIOGroup() {
	finish();
}

bool GPIOGroup::init() {
	if(initialized) {
		return true;
	}

	if(!chip || !chip->is_initialized()) {
		return false;
	}

	initialized = true;

	return true;
}

void GPIOGroup::finish() {
	if(request) {
		gpiod_line_request_release(request);
		request = nullptr;
	}

	initialized = false;
}

bool GPIOGroup::pin_mode(PinMode mode, PullMode pull) {
	if(!initialized || !chip || pin_numbers.empty()) {
		return false;
	}

	if(request) {
		gpiod_line_request_release(request);
		request = nullptr;
	}

	gpiod_line_settings *settings = gpiod_line_settings_new();

	if(!settings) {
		return false;
	}

	switch(mode) {
		case PinMode::Input:
			gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
			break;

		case PinMode::Output:
			gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
			gpiod_line_settings_set_drive(settings, GPIOD_LINE_DRIVE_PUSH_PULL);
			gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
			break;

		case PinMode::OpenDrain:
			gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
			gpiod_line_settings_set_drive(settings, GPIOD_LINE_DRIVE_OPEN_DRAIN);
			gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
			break;

		case PinMode::OpenSource:
			gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
			gpiod_line_settings_set_drive(settings, GPIOD_LINE_DRIVE_OPEN_SOURCE);
			gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
			break;
	}

	switch(pull) {
		case PullMode::None:
			gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_DISABLED);
			break;

		case PullMode::PullUp:
			gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
			break;

		case PullMode::PullDown:
			gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_DOWN);
			break;
	}

	gpiod_line_config *line_config = gpiod_line_config_new();

	if(!line_config) {
		gpiod_line_settings_free(settings);
		return false;
	}

	int return_value = gpiod_line_config_add_line_settings(line_config, pin_numbers.data(), pin_numbers.size(), settings);

	if(return_value) {
		gpiod_line_config_free(line_config);
		gpiod_line_settings_free(settings);
		return false;
	}

	gpiod_request_config *request_config = gpiod_request_config_new();

	if(!request_config) {
		gpiod_line_config_free(line_config);
		gpiod_line_settings_free(settings);
		return false;
	}

	gpiod_request_config_set_consumer(request_config, "GPIOGroup");

	request = gpiod_chip_request_lines(chip->get_chip(), request_config, line_config);

	gpiod_request_config_free(request_config);
	gpiod_line_config_free(line_config);
	gpiod_line_settings_free(settings);

	if(!request) {
		return false;
	}

	current_mode = mode;
	current_pull = pull;

	return true;
}

bool GPIOGroup::pin_set(int index, bool flag) {
	if(!initialized || !request || index < 0 || index >= (int)pin_numbers.size()) {
		return false;
	}

	if(current_mode == PinMode::Input) {
		return false;
	}

	gpiod_line_value value = flag ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;

	int return_value = gpiod_line_request_set_value(request, pin_numbers[index], value);

	return (return_value == 0);
}

bool GPIOGroup::pin_get(int index) {
	if(!initialized || !request || index < 0 || index >= (int)pin_numbers.size()) {
		return false;
	}

	gpiod_line_value value = gpiod_line_request_get_value(request, pin_numbers[index]);

	return (value == GPIOD_LINE_VALUE_ACTIVE);
}

bool GPIOGroup::pins_set_all(const bool *flags) {
	if(!initialized || !request || !flags) {
		return false;
	}

	if(current_mode == PinMode::Input) {
		return false;
	}

	vector<gpiod_line_value> values(pin_numbers.size());

	for(size_t i = 0; i < pin_numbers.size(); i++) {
		values[i] = flags[i] ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE;
	}

	int return_value = gpiod_line_request_set_values(request, values.data());

	return (return_value == 0);
}

bool GPIOGroup::pins_get_all(bool *flags) {
	if(!initialized || !request || !flags) {
		return false;
	}

	vector<gpiod_line_value> values(pin_numbers.size());

	int return_value = gpiod_line_request_get_values(request, values.data());

	if(return_value != 0) {
		return false;
	}

	for(size_t i = 0; i < pin_numbers.size(); i++) {
		flags[i] = (values[i] == GPIOD_LINE_VALUE_ACTIVE);
	}

	return true;
}
