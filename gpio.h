#ifndef GPIO_H
#define GPIO_H

#include <string>
#include <vector>
#include <cstdint>

using std::string;
using std::vector;

struct gpiod_chip;
struct gpiod_line_request;

enum class PinMode {
	Input,
	Output,
	OpenDrain,
	OpenSource
};

enum class PullMode {
	None,
	PullUp,
	PullDown
};

// =============================================================
// GPIOChip: Manages a single GPIO chip
// =============================================================

class GPIOChip {
private:
	string chip_path;
	gpiod_chip *chip;

	bool initialized;

public:
	GPIOChip(const string &chip_path);
	~GPIOChip();

	bool init();
	void finish();

	bool is_initialized() const { return initialized; }

	gpiod_chip* get_chip() { return chip; }
};

// =============================================================
// GPIO: Single pin control
// =============================================================

class GPIO {
private:
	GPIOChip *chip;
	unsigned int pin_number;

	PinMode current_mode;
	PullMode current_pull;

	gpiod_line_request *request;
	bool initialized;

public:
	GPIO(GPIOChip *chip, unsigned int pin_number);
	~GPIO();

	bool init();
	void finish();

	bool pin_mode(PinMode mode, PullMode pull = PullMode::None);
	bool pin_set(bool flag);
	bool pin_get();

	bool is_initialized() const { return initialized; }
};

// =============================================================
// GPIOGroup: Multiple pins controlled together
// =============================================================

class GPIOGroup {
private:
	GPIOChip *chip;
	vector<unsigned int> pin_numbers;

	PinMode current_mode;
	PullMode current_pull;

	gpiod_line_request *request;
	bool initialized;

public:
	GPIOGroup(GPIOChip *chip, const vector<unsigned int> &pins);
	~GPIOGroup();

	bool init();
	void finish();

	bool pin_mode(PinMode mode, PullMode pull = PullMode::None);

	bool pin_set(int index, bool flag);
	bool pin_get(int index);

	bool pins_set_all(const bool *flags);
	bool pins_get_all(bool *flags);

	int get_pin_count() const { return pin_numbers.size(); }

	bool is_initialized() const { return initialized; }
};

#endif // GPIO_H
