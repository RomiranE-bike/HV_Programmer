/*
 * HV_Programmer_ver.4_1404.c
 *
 * Created: 5/11/2025 21:53:13
 * Author : me
 */ 

#include <avr/io.h>
#include <util/delay.h>

// ATtiny26 Signature Bytes
#define SIG1 0x1E
#define SIG2 0x91
#define SIG3 0x0F

// Fuse settings
#define HFUSE 0x64    // Default for ATtiny26
#define LFUSE 0xDF    // Default for ATtiny26
#define EFUSE 0xFF    // Extended fuse (unused in ATtiny26)

// Port Definitions
#define DATA_PORT PORTB
#define DATA_DDR  DDRB
#define CTRL_PORT PORTD
#define CTRL_DDR  DDRD

// Control Pins
enum {
	LED = PD0,
	BUZ = PD1,
	OE  = PD2,
	XTAL1 = PD3,
	WR  = PD4,
	VCC = PD5,
	BS2 = PD6,
	RESET12V = PD7
};

// Status Pins
enum {
	RDY = PC0,
	BS1 = PC1,
	XA0 = PC2,
	XA1 = PC3,
	BUTTON = PC4,
	PAG = PC5
};

// Timing Constants
#define STABLE_DELAY 1250  // ms
#define PULSE_DELAY 150    // ms
#define BEEP_DURATION 500 // cycles

// Global state
typedef struct {
	uint8_t sig[3];
	uint8_t sig_valid;
} ProgrammerState;

ProgrammerState state;

void init_ports() {
	// Set up data port
	DATA_DDR = 0xFF;
	DATA_PORT = 0x00;
	
	// Set up control port
	CTRL_DDR = 0xFF;
	CTRL_PORT = (1 << VCC); // VCC off initially
	
	// Status port configuration
	DDRC = 0xEE;  // PC0,PC4 inputs
	PORTC = 0x11; // Pull-ups on PC0,PC4
}

void enter_program_mode() {
	// Step 1: Set initial control state
	PORTC = 0x19;
	CTRL_PORT = 0x23; // ~RESET low, VCC off
	_delay_ms(STABLE_DELAY);
	
	// Step 2: Apply programming voltage sequence
	CTRL_PORT = 0x14; // ~RESET high
	_delay_ms(PULSE_DELAY);
	
	// Step 3: Activate target
	CTRL_PORT = 0x94; // VCC on, ~RESET high
	_delay_ms(PULSE_DELAY);
}

void send_command(uint8_t cmd) {
	PORTC = 0x19;
	CTRL_PORT = 0x96;
	DATA_PORT = cmd;
	generate_xtal_pulse();
}

void generate_xtal_pulse() {
	CTRL_PORT |= (1 << XTAL1);
	_delay_ms(PULSE_DELAY);
	CTRL_PORT &= ~(1 << XTAL1);
	_delay_ms(PULSE_DELAY);
}

void generate_write_pulse() {
	CTRL_PORT &= ~(1 << WR);
	_delay_ms(PULSE_DELAY);
	CTRL_PORT |= (1 << WR);
	_delay_ms(PULSE_DELAY);
}

uint8_t read_byte() {
	DDRB = 0x00;    // Set data port as input
	PORTC = 0x09;   // Prepare to read
	CTRL_PORT = 0x94;
	_delay_ms(PULSE_DELAY);
	
	uint8_t result = PINB;
	DDRB = 0xFF;    // Restore data port as output
	return result;
}

void read_signature() {
	// Read all three signature bytes
	send_command(0x08); // Read sig byte 0
	state.sig[0] = read_byte();
	
	send_command(0x08 + 0x01); // Read sig byte 1
	state.sig[1] = read_byte();
	
	send_command(0x08 + 0x02); // Read sig byte 2
	state.sig[2] = read_byte();
	
	// Verify signature
	state.sig_valid = (state.sig[0] == SIG1 &&
	state.sig[1] == SIG2 &&
	state.sig[2] == SIG3);
}

void repair_signature() {
	// Enter signature calibration mode
	send_command(0xAC);
	send_command(0x5E);
	
	// Write signature bytes
	for(uint8_t i = 0; i < 3; i++) {
		send_command(0x08 + i);
		DATA_PORT = (i == 0) ? SIG1 : (i == 1) ? SIG2 : SIG3;
		PORTC = 0x15;
		generate_write_pulse();
	}
	
	// Exit calibration mode
	send_command(0xAC);
	send_command(0x5F);
	PORTC = 0x15;
	generate_write_pulse();
}

void program_fuses() {
	// High fuse
	send_command(0x40);
	DATA_PORT = HFUSE;
	PORTC = 0x17;
	generate_write_pulse();
	
	// Low fuse
	send_command(0x40);
	DATA_PORT = LFUSE;
	PORTC = 0x15;
	generate_write_pulse();
}

void chip_erase() {
	send_command(0x80);
	PORTC = 0x15;
	generate_write_pulse();
}

void exit_program_mode() {
	PORTC = 0x11;
	CTRL_PORT = 0x23;
	DATA_PORT = 0x00;
}

void user_feedback(uint8_t success) {
	if(success) {
		for(uint8_t i = 0; i < 3; i++) {
			CTRL_PORT |= (1 << LED);
			_delay_ms(100);
			CTRL_PORT &= ~(1 << LED);
			_delay_ms(100);
		}
		} else {
		for(uint8_t i = 0; i < 3; i++) {
			for(uint8_t i = 0; i < 500; i++) {
			CTRL_PORT |= (1 << BUZ);
			_delay_us(50);
			CTRL_PORT &= ~(1 << BUZ);
			_delay_us(50);
			}
		}
	}
}

int main(void) {
	init_ports();
	
	while(1) {
		if(!(PINC & (1 << BUTTON))) {
			enter_program_mode();
			read_signature();
			
			if(!state.sig_valid) {
				repair_signature();
				read_signature(); // Verify repair
			}
			
			if(state.sig_valid) {
				program_fuses();
				chip_erase();
				user_feedback(1); // Success
				} else {
				user_feedback(0); // Failure
			}
			
			exit_program_mode();
		}
		_delay_ms(STABLE_DELAY);
	}
}