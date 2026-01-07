extern "C" {
	#include "sim_frontpanel.h"
}

#include "gpio.h"
#include "configuration.h"
#include "logger.h"
#include "daemon.h"

#include <unistd.h>
#include <time.h>
#include <getopt.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <chrono>

using std::vector;

// =============================================================
// Timing constants
// =============================================================

constexpr unsigned int WAIT_SIGNAL_LED_SETTLE_NS      = 1500000;
constexpr unsigned int WAIT_SIGNAL_LED_BLANKING_NS    = 100000;
constexpr unsigned int WAIT_SIGNAL_SWITCH_SETTLE_NS   = 50000;
constexpr unsigned int WAIT_MODE_CHANGE_US            = 10;
constexpr unsigned int WAIT_POLL_INTERVAL_MS          = 50;
constexpr unsigned int WAIT_LOOP_INTERVAL_NS          = 1000;
constexpr unsigned int WAIT_CONFIG_SELECTION_S        = 10;
 
// =============================================================
// Pin definitions
// =============================================================

static const unsigned LED_ROWS[6] = {20, 21, 22, 23, 24, 25};
static const unsigned SWITCH_ROWS[3]  = {16, 17, 18};
static const unsigned COLS[12]    = {26, 27, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};

// =============================================================
// Global state
// =============================================================

static volatile bool program_running = true;

static void signal_handler(int signal_number) {
	(void) signal_number;
	program_running = false;
}

// =============================================================
// Panel state
// =============================================================

struct PanelState {
	// Address LEDs (22 bits)
	uint32_t address;

	// Data LEDs (16 bits)
	uint16_t data;

	// Status flags
	bool flag_addr22;
	bool flag_addr18;
	bool flag_addr16;
	bool flag_data;
	bool flag_kernel;
	bool flag_super;
	bool flag_user;
	bool flag_master;
	bool flag_pause;
	bool flag_run;
	bool flag_addr_err;
	bool flag_par_err;
	bool flag_par_low;
	bool flag_par_high;

	// Rotary encoders (R1: 0-7, R2: 0-3)
	uint8_t r1_user_d;
	uint8_t r1_super_d;
	uint8_t r1_kernel_d;
	uint8_t r1_cons_phy;
	uint8_t r1_user_i;
	uint8_t r1_super_i;
	uint8_t r1_kernel_i;
	uint8_t r1_prog_phy;
	uint8_t r2_data_paths;
	uint8_t r2_bus_reg;
	uint8_t r2_mu_adr_fpp_cpu;
	uint8_t r2_display_register;

	// Switch register (22 bits)
	uint32_t switch_state;

	// Control switches
	bool flag_test;
	bool flag_load_addr;
	bool flag_exam;
	bool flag_dep;
	bool flag_cont;
	bool flag_enable_halt;
	bool flag_sinst_sbus_cycle;
	bool flag_start;

	// Rotary encoder push buttons
	bool r1_button;
	bool r2_button;

	// Internal state
	uint8_t r1_position;
	uint8_t r2_position;
};

static PanelState panel = {0};

// Register storage for simulator
static uint32_t reg_pc = 0;
static uint16_t reg_ir = 0;
static uint16_t reg_psw = 0;
static uint16_t reg_r[8] = {0};

// Bit sampling arrays for blinkenlights (accumulated bit activity)
static int bits_pc[22] = {0};

// Callback synchronization
static volatile bool registers_updated = false;

// =============================================================
// Edge detector
// =============================================================

struct Edge {
	bool previous;

	Edge(): previous{false} {}

	bool rising(bool current) {
		bool result = (current && !previous);

		previous = current;
		return result;
	}

	bool falling(bool current) {
		bool result = (!current && previous);

		previous = current;
		return result;
	}
};

// =============================================================
// Rotary encoder
// =============================================================

struct RotaryEncoder {
	static constexpr int SENSITIVITY = 4;

	uint8_t states;
	uint8_t last_state;
	int8_t accumulated_deltas;
	uint8_t position;

	RotaryEncoder(uint8_t states): states{states}, last_state{0}, accumulated_deltas{0}, position{0} {}

	void add_delta(bool a, bool b) {
		uint8_t state = (a ? 2 : 0) | (b ? 1 : 0);
		int delta = 0;

		// Clockwise transitions
		if(last_state == 0b00 && state == 0b01) delta = +1;
		else if(last_state == 0b01 && state == 0b11) delta = +1;
		else if(last_state == 0b11 && state == 0b10) delta = +1;
		else if(last_state == 0b10 && state == 0b00) delta = +1;

		// Counter-clockwise transitions
		else if(last_state == 0b00 && state == 0b10) delta = -1;
		else if(last_state == 0b10 && state == 0b11) delta = -1;
		else if(last_state == 0b11 && state == 0b01) delta = -1;
		else if(last_state == 0b01 && state == 0b00) delta = -1;

		accumulated_deltas += delta;

		last_state = state;

		if(accumulated_deltas > SENSITIVITY) {
			accumulated_deltas = 0;
			position++;
		}

		if(accumulated_deltas < -SENSITIVITY) {
			accumulated_deltas = 0;
			position--;
		}

		position %= states;
	}
};

// =============================================================
// GPIO objects
// =============================================================

static GPIOChip *chip = nullptr;
static GPIOGroup *led_rows = nullptr;
static GPIOGroup *switch_rows = nullptr;
static GPIOGroup *cols = nullptr;

// =============================================================
// GPIO initialization
// =============================================================

static void init_gpio() {
	chip = new GPIOChip("/dev/gpiochip0");
	chip->init();

    // Led pins off
	vector<unsigned int> led_row_pins;

	for(int i = 0; i < 6; i++) {
		led_row_pins.push_back(LED_ROWS[i]);
	}

	led_rows = new GPIOGroup(chip, led_row_pins);
	led_rows->init();
	led_rows->pin_mode(PinMode::Output);

	for(int i = 0; i < 6; i++) {
		led_rows->pin_set(i, false);
	}

    // Row pins as sink
	vector<unsigned int> switch_row_pins;

	for(int i = 0; i < 3; i++) {
		switch_row_pins.push_back(SWITCH_ROWS[i]);
	}

	switch_rows = new GPIOGroup(chip, switch_row_pins);
	switch_rows->init();
	switch_rows->pin_mode(PinMode::Output);

	for(int i = 0; i < 3; i++) {
		switch_rows->pin_set(i, true);
	}

    // Column pins off (high)
	vector<unsigned int> col_pins;

	for(int i = 0; i < 12; i++) {
		col_pins.push_back(COLS[i]);
	}

	cols = new GPIOGroup(chip, col_pins);
	cols->init();
	cols->pin_mode(PinMode::Output);

	for(int i = 0; i < 12; i++) {
		cols->pin_set(i, true);
	}
}

// =============================================================
// GPIO cleanup
// =============================================================

static void finish_gpio() {
	if(led_rows) {
		for(int i = 0; i < 6; i++) {
			led_rows->pin_set(i, false);
		}

		led_rows->finish();
		delete led_rows;
	}

	if(switch_rows) {
		for(int i = 0; i < 3; i++) {
			switch_rows->pin_set(i, true);
		}

		switch_rows->finish();
		delete switch_rows;
	}

	if(cols) {
		cols->finish();
		delete cols;
	}

	if(chip) {
		chip->finish();
		delete chip;
	}
}

// =============================================================
// Read switch state
// =============================================================

static void read_state_switches(bool switches[3][12]) {
	cols->pin_mode(PinMode::Input, PullMode::PullUp);

	bool sw_row_values[3];
	bool col_values[12];

	struct timespec time_specification = {0, WAIT_SIGNAL_SWITCH_SETTLE_NS};

	// Deactivate all switch rows (high)
	for(int i = 0; i < 3; i++) {
		sw_row_values[i] = true;
	}

	for(int switch_row = 0; switch_row < 3; switch_row++) {
		sw_row_values[switch_row] = false;
		switch_rows->pins_set_all(sw_row_values);

		// Wait for signals to settle
		nanosleep(&time_specification, nullptr);

		// Read all columns
		cols->pins_get_all(col_values);
		for(int col = 0; col < 12; col++) {
			// Switch pressed: column reads low
			switches[switch_row][col] = !col_values[col];
		}

		sw_row_values[switch_row] = true;
	}

	// Deactivate all switch rows (high)
	for(int i = 0; i < 3; i++) {
		sw_row_values[i] = true;
	}
	switch_rows->pins_set_all(sw_row_values);

	// Avoids changing pin modes too quickly
	usleep(WAIT_MODE_CHANGE_US);

	// Set all columns back to output mode (high)
	cols->pin_mode(PinMode::Output);

	for(int col = 0; col < 12; col++) {
		col_values[col] = true;
	}
	cols->pins_set_all(col_values);
}

// =============================================================
// Decode switch state
// =============================================================

static void decode_state_switches(const bool switches[3][12], PanelState &panel_state) {
	panel_state.switch_state = 0;

	// Row 0: SR0...SR11
	for(int col = 0; col < 12; col++) {
		if(switches[0][col]) {
			panel_state.switch_state |= (1u << col);
		}
	}

	// Row 1: SR12...SR21
	for(int col = 0; col < 10; col++) {
		if(switches[1][col]) {
			panel_state.switch_state |= (1u << (12 + col));
		}
	}

	// Row 2: Control switches
	panel_state.flag_test = !switches[2][0];
	panel_state.flag_load_addr = !switches[2][1];
	panel_state.flag_exam = !switches[2][2];
	panel_state.flag_dep = !switches[2][3];
	panel_state.flag_cont = !switches[2][4];
	panel_state.flag_enable_halt = !switches[2][5];
	panel_state.flag_sinst_sbus_cycle = !switches[2][6];
	panel_state.flag_start = !switches[2][7];
}

// =============================================================
// Decode rotary switch state
// =============================================================

static void decode_state_rotary_switches(const bool switches[3][12], PanelState &panel_state, RotaryEncoder &r1_encoder, RotaryEncoder &r2_encoder) {
	// R1 encoder: row1/col10 (button), row2/col8 and row2/col9 (rotation)
	panel_state.r1_button = switches[1][10];

	bool r1_a = switches[2][8];
	bool r1_b = switches[2][9];

	r1_encoder.add_delta(r1_a, r1_b);

	panel_state.r1_position = r1_encoder.position;

	// R2 encoder: row1/col11 (button), row2/col10 and row2/col11 (rotation)
	panel_state.r2_button = switches[1][11];

	bool r2_a = switches[2][10];
	bool r2_b = switches[2][11];

	r2_encoder.add_delta(r2_a, r2_b);

	panel_state.r2_position = r2_encoder.position;
}

// =============================================================
// Encode light state
// =============================================================

static void encode_state_lights(const PanelState &panel_state, bool leds[6][12], int *blinkenlight_array) {
	// Clear all LEDs
	for(int led_row = 0; led_row < 6; led_row++) {
		for(int col = 0; col < 12; col++) {
			leds[led_row][col] = false;
		}
	}

	// LED Row 0: A0...A11
	// Use blinkenlights (bit sampling) when array provided, otherwise direct address
	for(int col = 0; col < 12; col++) {
		if(blinkenlight_array != nullptr) {
			leds[0][col] = (blinkenlight_array[col] > 50);
		}
		else {
			leds[0][col] = (panel_state.address >> col) & 1;
		}
	}

	// LED Row 1: A12...A21
	for(int col = 0; col < 10; col++) {
		if(blinkenlight_array != nullptr) {
			leds[1][col] = (blinkenlight_array[12 + col] > 50);
		}
		else {
			leds[1][col] = (panel_state.address >> (12 + col)) & 1;
		}
	}

	// LED Row 2: Status indicators
	leds[2][0] = panel_state.flag_addr22;
	leds[2][1] = panel_state.flag_addr18;
	leds[2][2] = panel_state.flag_addr16;
	leds[2][3] = panel_state.flag_data;
	leds[2][4] = panel_state.flag_kernel;
	leds[2][5] = panel_state.flag_super;
	leds[2][6] = panel_state.flag_user;
	leds[2][7] = panel_state.flag_master;
	leds[2][8] = panel_state.flag_pause;
	leds[2][9] = panel_state.flag_run;
	leds[2][10] = panel_state.flag_addr_err;
	leds[2][11] = panel_state.flag_par_err;

	// LED Row 3: D0...D11
	for(int col = 0; col < 12; col++) {
		leds[3][col] = (panel_state.data >> col) & 1;
	}

	// LED Row 4: D12...D15, PAR_LOW, PAR_HIGH, R1 positions 0-3, R2 positions 0-1
	leds[4][0] = (panel_state.data >> 12) & 1;
	leds[4][1] = (panel_state.data >> 13) & 1;
	leds[4][2] = (panel_state.data >> 14) & 1;
	leds[4][3] = (panel_state.data >> 15) & 1;
	leds[4][4] = panel_state.flag_par_low;
	leds[4][5] = panel_state.flag_par_high;

	// R1 positions: USER_D, SUPER_D, KERNEL_D, CONS_PHY (row 4, cols 6-9)
	if(panel_state.r1_position == 0) leds[4][6] = true;  // USER_D
	if(panel_state.r1_position == 1) leds[4][7] = true;  // SUPER_D
	if(panel_state.r1_position == 2) leds[4][8] = true;  // KERNEL_D
	if(panel_state.r1_position == 3) leds[4][9] = true;  // CONS_PHY

	// LED Row 5: R1 positions 4-7, R2 positions 2-3
	// R1 positions: USER_I, SUPER_I, KERNEL_I, PROG_PHY (row 5, cols 6-9)
	if(panel_state.r1_position == 4) leds[5][6] = true;  // USER_I
	if(panel_state.r1_position == 5) leds[5][7] = true;  // SUPER_I
	if(panel_state.r1_position == 6) leds[5][8] = true;  // KERNEL_I
	if(panel_state.r1_position == 7) leds[5][9] = true;  // PROG_PHY

	// R2 positions: DATA_PATHS, BUS_REG (row 4, cols 10-11)
	if(panel_state.r2_position == 0) leds[4][10] = true;  // DATA_PATHS
	if(panel_state.r2_position == 1) leds[4][11] = true;  // BUS_REG

	// R2 positions: MU_ADR_FPP_CPU, DISPLAY_REGISTER (row 5, cols 10-11)
	if(panel_state.r2_position == 2) leds[5][10] = true;  // MU_ADR_FPP_CPU
	if(panel_state.r2_position == 3) leds[5][11] = true;  // DISPLAY_REGISTER
}

// =============================================================
// Write light state
// =============================================================

static void write_state_lights(const bool leds[6][12]) {
	bool row_values[6];
	bool col_values[12];

	struct timespec led_settle_time = {0, WAIT_SIGNAL_LED_SETTLE_NS};
	struct timespec blanking_time = {0, WAIT_SIGNAL_LED_BLANKING_NS};

	// Turn off all rows in the beginning
	for(int i = 0; i < 6; i++) {
		row_values[i] = false;
	}
	led_rows->pins_set_all(row_values);

	for(int led_row = 0; led_row < 6; led_row++) {
		// Set columns for this row
		for(int col = 0; col < 12; col++) {
			col_values[col] = !leds[led_row][col];
		}
		cols->pins_set_all(col_values);

		// Wait for signals to settle before turning on row
		nanosleep(&blanking_time, nullptr);

		// Turn on this row
		led_rows->pin_set(led_row, true);

		// Keep it on for visibility
		nanosleep(&led_settle_time, nullptr);

		// Turn off this row
		led_rows->pin_set(led_row, false);
	}

	// Turn off all columns at the end
	for(int col = 0; col < 12; col++) {
		col_values[col] = true;
	}
	cols->pins_set_all(col_values);
}

// =============================================================
// Simulator helpers
// =============================================================

static uint32_t pc_inc(uint32_t pc22) {
	return (pc22 + 1) & ((1u << 22) - 1);
}

static void compute_ksu_from_psw(PanelState &panel_state) {
	panel_state.flag_kernel = false;
	panel_state.flag_super = false;
	panel_state.flag_user = false;

	if(reg_psw == 0) {
		return;
	}

	uint32_t mode = (reg_psw >> 14) & 0x3;
	if(mode == 0) {
		panel_state.flag_kernel = true;
	}
	else if(mode == 1) {
		panel_state.flag_super = true;
	}
	else if(mode == 3) {
		panel_state.flag_user = true;
	}
}

static uint32_t select_display_address(uint8_t r1_pos, uint32_t pc, uint32_t console_address) {
	switch(r1_pos) {
		case 0: // USER_D
		case 1: // SUPER_D
		case 2: // KERNEL_D
		case 4: // USER_I
		case 5: // SUPER_I
		case 6: // KERNEL_I
			return pc;
		case 3: // CONS_PHY
			return console_address;
		case 7: // PROG_PHY
			return pc & ((1u << 22) - 1);
		default:
			return pc;
	}
}

static uint16_t select_display_register_data(uint32_t switch_state) {
	// Use switch register bits [2:0] to select R0-R7
	uint32_t index = switch_state & 0x7;

	if(index == 7) {
		// PC
		return (uint16_t) (reg_pc & 0xFFFF);
	}

	return reg_r[index];  // R0-R5 or R6 (SP)
}

// =============================================================
// Display callback for register updates
// =============================================================

static void display_callback(PANEL *panel, unsigned long long simulation_time, void *context) {
	(void) panel;
	(void) simulation_time;
	(void) context;

	// Registers are automatically updated in their buffers
	// Just signal that new data is available
	registers_updated = true;
}

// =============================================================
// Session
// =============================================================

enum class SessionResult {
	Exit,
	RestartSession,
	ReloadConfigRestartSession
};

static SessionResult run_session(const char *binary_path, const ConfigurationEntry *config_entry) {
	bool switches[3][12];

	read_state_switches(switches);
	decode_state_switches(switches, panel);

	uint32_t initial_low12 = panel.switch_state & 0xFFF;

	logger->info("Initial SR[11:0]: %o\n", initial_low12);

	logger->info("Starting OpenSIMH simulator: %s\n", binary_path);
	logger->info("Using config file: %s\n", config_entry->configuration_file.c_str());
	logger->info("Boot device: %s\n", config_entry->boot_device.c_str());

	PANEL* simh_panel = sim_panel_start_simulator(binary_path, config_entry->configuration_file.c_str(), 0);

	if(!simh_panel) {
		logger->error("ERROR: sim_panel_start_simulator() failed\n");
		logger->error("  %s\n", sim_panel_get_error());

		return SessionResult::Exit;
	}

	logger->info("Connected successfully\n\n");

	// Set up bit sampling for realistic blinkenlights
	// Sample every instruction, depth of 100 for smooth blinking
	sim_panel_set_sampling_parameters(simh_panel, 1, 100);

	// Register tracking with bit sampling for address/data buses
	sim_panel_add_register(simh_panel, "PC", nullptr, sizeof(reg_pc), &reg_pc);
	sim_panel_add_register_bits(simh_panel, "PC", nullptr, 22, bits_pc);
	sim_panel_add_register(simh_panel, "IR", nullptr, sizeof(reg_ir), &reg_ir);
	sim_panel_add_register(simh_panel, "PSW", nullptr, sizeof(reg_psw), &reg_psw);
	sim_panel_add_register(simh_panel, "R0", nullptr, sizeof(reg_r[0]), &reg_r[0]);
	sim_panel_add_register(simh_panel, "R1", nullptr, sizeof(reg_r[1]), &reg_r[1]);
	sim_panel_add_register(simh_panel, "R2", nullptr, sizeof(reg_r[2]), &reg_r[2]);
	sim_panel_add_register(simh_panel, "R3", nullptr, sizeof(reg_r[3]), &reg_r[3]);
	sim_panel_add_register(simh_panel, "R4", nullptr, sizeof(reg_r[4]), &reg_r[4]);
	sim_panel_add_register(simh_panel, "R5", nullptr, sizeof(reg_r[5]), &reg_r[5]);
	sim_panel_add_register(simh_panel, "SP", nullptr, sizeof(reg_r[6]), &reg_r[6]);

	// Set up callback for automatic register updates (10ms interval)
	sim_panel_set_display_callback_interval(simh_panel, display_callback, nullptr, 10000);

	Edge edge_load, edge_exam, edge_dep, edge_step, edge_cont, edge_enable_halt, edge_start;
	Edge edge_r1_button, edge_r2_button;
	Edge edge_test;
	RotaryEncoder r1_encoder(8), r2_encoder(4);

	logger->info("Starting main loop (Ctrl+C to exit)...\n");

	uint32_t console_address = 0;
	uint32_t prev_console_address = 0;

	bool use_data_latched = false;
	uint16_t data_latched = 0;
	uint16_t prev_data_latched = 0;

	logger->info("BOOT: Booting %s\n", config_entry->boot_device.c_str());
	sim_panel_exec_boot(simh_panel, config_entry->boot_device.c_str());

	// Fake register update in the beginning so we update the state right away
	registers_updated = true;

	if(!panel.flag_enable_halt) {
		logger->info("[HALT] Entering halt/step mode in the beginning\n");
		sim_panel_exec_halt(simh_panel);
	}

	SessionResult result = SessionResult::Exit;

	while(program_running) {
		// Scan switches every iteration for responsive rotary encoders
		bool switches[3][12];
		read_state_switches(switches);
		decode_state_switches(switches, panel);
		decode_state_rotary_switches(switches, panel, r1_encoder, r2_encoder);

		// Detect rotary button presses
		if(edge_r1_button.rising(panel.r1_button)) {
			logger->info("[R1 BUTTON] Reload configuration requested\n");
			result = SessionResult::ReloadConfigRestartSession;
			break;
		}
		if(edge_r2_button.rising(panel.r2_button)) {
			logger->info("[R2 BUTTON] Restart session requested\n");
			result = SessionResult::RestartSession;
			break;
		}

		// TEST switch: print debug state
		if(edge_test.rising(panel.flag_test)) {
			logger->info("\n========== DEBUG STATE DUMP (TEST) ==========\n");

			// Edge detector states
			logger->info("Edge Detectors (previous state):\n");
			logger->info("  edge_load:        %d\n", edge_load.previous);
			logger->info("  edge_exam:        %d\n", edge_exam.previous);
			logger->info("  edge_dep:         %d\n", edge_dep.previous);
			logger->info("  edge_cont:        %d\n", edge_cont.previous);
			logger->info("  edge_enable_halt: %d\n", edge_enable_halt.previous);
			logger->info("  edge_start:       %d\n", edge_start.previous);
			logger->info("  edge_r1_button:   %d\n", edge_r1_button.previous);
			logger->info("  edge_r2_button:   %d\n", edge_r2_button.previous);

			// Switch states
			logger->info("\nSwitch Register: %06o (octal) / %u (decimal)\n",
				panel.switch_state, panel.switch_state);

			logger->info("\nControl Switches:\n");
			logger->info("  LOAD_ADDR:   %d\n", panel.flag_load_addr);
			logger->info("  EXAM:        %d\n", panel.flag_exam);
			logger->info("  DEP:         %d\n", panel.flag_dep);
			logger->info("  CONT:        %d\n", panel.flag_cont);
			logger->info("  ENABLE/HALT: %d\n", panel.flag_enable_halt);
			logger->info("  S_INST/S_BC: %d\n", panel.flag_sinst_sbus_cycle);
			logger->info("  START:       %d\n", panel.flag_start);

			// Rotary encoders
			logger->info("\nRotary Encoders:\n");
			logger->info("  R1 position: %d\n", panel.r1_position);
			logger->info("  R2 position: %d\n", panel.r2_position);
			logger->info("  R1 button:   %d\n", panel.r1_button);
			logger->info("  R2 button:   %d\n", panel.r2_button);

			// Raw switch matrix
			logger->info("\nRaw Switch Matrix:\n");

			for(int row = 0; row < 3; row++) {
				logger->info("  Row %d: ", row);

				for(int col = 0; col < 12; col++) {
					logger->info("%d ", switches[row][col] ? 1 : 0);
				}

				logger->info("\n");
			}

			logger->info("=============================================\n\n");
		}

		// Use blinkkenlights only when the PC is displayed in the panel
		bool use_blinkenlights = false;

		// Process updates when callback signals new register data
		if(registers_updated) {
			registers_updated = false;

			uint32_t pc = reg_pc & ((1u << 22) - 1);

			OperationalState state = sim_panel_get_state(simh_panel);
			bool simulator_running = (state == Run);

			// LOAD ADDR: console_address <- switch_register
			if(!simulator_running && edge_load.falling(panel.flag_load_addr)) {
				prev_console_address = console_address;
				console_address = panel.switch_state & ((1u << 22) - 1);
				logger->debug("[LOAD] console_address: %06o -> %06o\n", prev_console_address, console_address);

				if(use_data_latched) {
					logger->debug("[LOAD] data latch OFF\n");
					use_data_latched = false;
				}
			}

			// EXAM: data <- memory[console_address]; console_address++
			if(!simulator_running && edge_exam.falling(panel.flag_exam)) {
				uint16_t value = 0;

				if(sim_panel_mem_examine(simh_panel, sizeof(console_address), &console_address, sizeof(value), &value) == 0) {
					prev_data_latched = data_latched;
					data_latched = value;
					logger->debug("[EXAM] data_latched: %06o -> %06o\n", prev_data_latched, data_latched);

					if(!use_data_latched) {
						logger->debug("[EXAM] data latch ON\n");
						use_data_latched = true;
					}

					prev_console_address = console_address;
					console_address = pc_inc(console_address);
					logger->debug("[EXAM] console_address: %06o -> %06o\n", prev_console_address, console_address);
				}
			}

			// DEP: memory[console_address] <- switch_register; console_address++
			// Note that the switch action of DEP is inverted, but the signal is still 1 on the default state
			if(!simulator_running && edge_dep.falling(panel.flag_dep)) {
				uint16_t value = (uint16_t)(panel.switch_state & 0xFFFF);

				if(sim_panel_mem_deposit(simh_panel, sizeof(console_address), &console_address, sizeof(value), &value) == 0) {
					prev_data_latched = data_latched;
					data_latched = value;
					logger->debug("[DEP] data_latched: %06o -> %06o\n", prev_data_latched, data_latched);

					if(!use_data_latched) {
						logger->debug("[DEP] data latch ON\n");
						use_data_latched = true;
					}

					prev_console_address = console_address;
					console_address = pc_inc(console_address);
					logger->debug("[DEP] console_address: %06o -> %06o\n", prev_console_address, console_address);
				}
			}

			// CONT: execute based on S_INST/S_BC switch state
			if(edge_cont.falling(panel.flag_cont)) {
				if(panel.flag_sinst_sbus_cycle) {
					// S_INST/S_BC active: single step
					logger->debug("[CONT (single step)]\n");
				}
				else {
					// S_INST/S_BC inactive: single step but give different message
					logger->debug("[CONT (single step)] - ignoring S_BC\n");
				}

				sim_panel_exec_step(simh_panel);
			}

			// ENABLE/HALT: edge-triggered control
			if(simulator_running) {
				if(edge_enable_halt.falling(panel.flag_enable_halt)) {
					// Transitioned to halt mode
					logger->info("[HALT] Entering halt (step) mode\n");
					sim_panel_exec_halt(simh_panel);
				}
			}
			else {
				if(edge_enable_halt.rising(panel.flag_enable_halt)) {
					// Transitioned to run mode
					logger->info("[ENABLE] Entering enable mode\n");
					sim_panel_exec_run(simh_panel);

					if(use_data_latched) {
						logger->debug("[HALT] data latch OFF\n");
						use_data_latched = false;
					}
				}
			}

			// START: PC <- console_address; RUN
			if(edge_start.falling(panel.flag_start)) {
				char buffer[32];
				snprintf(buffer, sizeof(buffer), "%u", console_address);

				logger->info("[START] Setting PC to console_address %06o and running\n", console_address);

				if(sim_panel_set_register_value(simh_panel, "PC", buffer) == 0) {
					pc = console_address;
					reg_pc = console_address;

					sim_panel_exec_run(simh_panel);
				}
			}

			// Update status lamps from simulator state
			compute_ksu_from_psw(panel);
			panel.flag_run = simulator_running;

			// ADDRESS LED priority:
			// 1. CPU not running: console_address
			// 2. CPU running: show address selected via R1
			if(!simulator_running) {
				panel.address = console_address;
				// Console address: no blinkenlights
				use_blinkenlights = false;
			}
			else {
				panel.address = select_display_address(panel.r1_position, pc, console_address);
				// Use blinkenlights only when showing PC (not CONS_PHY position 3)
				use_blinkenlights = (panel.r1_position != 3);
			}

			// DATA LED priority
			// 1. (R2 == DISPLAY REGISTER): show register selected via switch
			// 2. EXAM/DEP own the data latch: show examined/deposited memory
			// 3. CPU not running: show switch register
			// 4. Else: leave unchanged from last state
			if(panel.r2_position == 3) {
				panel.data = select_display_register_data(panel.switch_state);
			}
			else if(use_data_latched) {
				panel.data = data_latched;
			}
			else if(!simulator_running) {
				panel.data = (uint16_t) (panel.switch_state & 0xFFFF);
			}
			else {
				// Leave panel.data unchanged
			}

			panel.flag_addr16 = (pc < (1u << 16));
			panel.flag_addr18 = !panel.flag_addr16 && (pc < (1u << 18));
			panel.flag_addr22 = !panel.flag_addr18 && (pc >= (1u << 18));
			panel.flag_data = false;
			panel.flag_master = false;
			panel.flag_pause = false;
			panel.flag_addr_err = false;
			panel.flag_par_err = false;

			uint8_t parity_low = 0;
			uint8_t parity_high = 0;

			if(!use_blinkenlights) {
				uint16_t data_low = panel.data & 0xFF;
				uint16_t data_high = (panel.data >> 8) & 0xFF;

				for(int i = 0; i < 16; i++) {
					parity_low ^= (data_low >> i) & 1;
				}

				for(int i = 0; i < 6; i++) {
					parity_high ^= (data_high >> i) & 1;
				}
			}

			panel.flag_par_low = (parity_low == 1);
			panel.flag_par_high = (parity_high == 1);
		}
		else {
			struct timespec time_specification = {0, WAIT_LOOP_INTERVAL_NS};

			nanosleep(&time_specification, nullptr);
		}

		// Update and drive LED display
		bool leds[6][12];

		encode_state_lights(panel, leds, use_blinkenlights ? bits_pc : nullptr);
		write_state_lights(leds);
	}

	logger->info("\nShutting down session...\n");

	sim_panel_destroy(simh_panel);

	return result;
}

// =============================================================
// Main
// =============================================================

static void print_usage(const char *program_name) {
	fprintf(stderr, "Usage: %s [OPTIONS] <pdp11_binary> <config_file_full_path>\n", program_name);
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -d, --daemon     Run as daemon with syslog logging\n");
	fprintf(stderr, "  -h, --help       Show this help message\n");
	fprintf(stderr, "\n");
}

int main(int argc, char *argv[]) {
	bool run_as_daemon = false;

	// Parse command-line options
	static struct option long_options[] = {
		{"daemon", no_argument, 0, 'd'},
		{"help",   no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	int option_index = 0;
	int c;

	while((c = getopt_long(argc, argv, "dh", long_options, &option_index)) != -1) {
		switch(c) {
			case 'd':
				run_as_daemon = true;
				break;

			case 'h':
				print_usage(argv[0]);
				return 0;

			case '?':
				print_usage(argv[0]);
				return 1;

			default:
				break;
		}
	}

	// Check for required positional arguments
	if(optind + 2 > argc) {
		fprintf(stderr, "Error: Missing required arguments\n\n");
		print_usage(argv[0]);

		return 1;
	}

	const char *pdp11_binary = argv[optind];
	const char *config_file = argv[optind + 1];

	// Initialize logger
	logger = new Logger();
	logger->init(run_as_daemon, "frontpanel");

	// Daemonize if requested
	if(run_as_daemon) {
		logger->info("Daemonizing process\n");

		if(!daemonize(nullptr)) {
			logger->error("Failed to daemonize process\n");

			delete logger;
			return 1;
		}

		logger->info("Daemon started successfully\n");
	}

	std::signal(SIGINT, signal_handler);
	std::signal(SIGTERM, signal_handler);

	init_gpio();

	// Load configuration file
	Configuration config(config_file);

	if(!config.init()) {
		logger->error("ERROR: Failed to load configuration file\n");

		finish_gpio();
		logger->finish();
		delete logger;
		return 1;
	}

	while(program_running) {
		// Read switch register to determine configuration
		bool switches[3][12];
		read_state_switches(switches);
		decode_state_switches(switches, panel);

		uint32_t switch_code = panel.switch_state & ((1u << 22) - 1);

		logger->info("\n[CONFIG] Reading switch code: %06o\n", switch_code);

		const ConfigurationEntry *entry = config.find_entry(switch_code);

		if(!entry) {
			logger->error("[CONFIG] No matching configuration for switch code %06o\n", switch_code);
			logger->error("[CONFIG] Please set switches to a valid configuration\n");

			logger->info("[CONFIG] Sleeping for %ds\n", WAIT_CONFIG_SELECTION_S);
			sleep(WAIT_CONFIG_SELECTION_S);

			continue;
		}

		logger->info("[CONFIG] Matched entry:\n");
		logger->info("  Directory: %s\n", entry->directory.c_str());
		logger->info("  Config file: %s\n", entry->configuration_file.c_str());
		logger->info("  Boot device: %s\n", entry->boot_device.c_str());

		// Change to the specified directory
		if(chdir(entry->directory.c_str()) != 0) {
			logger->error("[CONFIG] Failed to change directory to: %s\n", entry->directory.c_str());

			program_running = false;
			break;
		}

		logger->info("[CONFIG] Changed to directory: %s\n", entry->directory.c_str());

		// Run session with this configuration
		SessionResult result = run_session(pdp11_binary, entry);

		switch(result) {
			case SessionResult::Exit:
				logger->info("[SESSION] Session completed; restarting\n");

				break;

			case SessionResult::ReloadConfigRestartSession:
				logger->info("[SESSION] Reloading configuration\n");

				if(!config.reload()) {
					logger->error("[CONFIG] Failed to reload configuration\n");
				}

				// no break, keep going

			case SessionResult::RestartSession:
				logger->info("[SESSION] Restarting session\n");
				break;
		}
	}

	finish_gpio();

	logger->info("\nClean exit\n");

	logger->finish();
	delete logger;

	return 0;
}