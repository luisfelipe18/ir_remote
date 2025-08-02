/*
 * ATTiny24 IR Decoder for NEC protocol
 * Maps 6 IR remote buttons to 6 output pins
 * LED indicates learning mode
 * Learning mode is triggered via a button on PA6
 * Outputs raw NEC codes over PB1 using software serial
 * Uses Timer1 for precise IR pulse timing
 *
 * Author: Luis Villavicencio
 * Date: August 2025
 */

#include <avr/io.h>
#include <util/delay.h>

#define F_CPU 8000000UL
#define IR_PIN     PB2
#define LED_PIN    PB0
#define TX_PIN     PB1
#define IR_DDR     DDRB
#define IR_PORT    PORTB
#define IR_PINREG  PINB

#define OUT_DDR    DDRA
#define OUT_PORT   PORTA
#define IN_PINREG  PINA

#define BTN_LEARN  PA6
#define BTN_DDR    DDRA
#define BTN_PORT   PORTA

#define PIN_POWER  PA0
#define PIN_UP     PA1
#define PIN_DOWN   PA2
#define PIN_MENU   PA3
#define PIN_RIGHT  PA4
#define PIN_LEFT   PA5

#define BIT_DELAY_US 104
#define LEAD_LOW_MIN   8000
#define LEAD_HIGH_MIN  4000
#define BIT_HIGH_TH    1000

uint32_t known_codes[6] = {
    0x61D648B7, 0x61D6D827, 0x61D658A7,
    0x61D6A05F, 0x61D6609F, 0x61D620DF
};

const uint8_t output_pins[6] = {
    PIN_POWER, PIN_UP, PIN_DOWN,
    PIN_MENU, PIN_RIGHT, PIN_LEFT
};

void uart_send_char(char c) {
    IR_PORT &= ~(1 << TX_PIN);
    _delay_us(BIT_DELAY_US);
    for (uint8_t i = 0; i < 8; i++) {
        if (c & (1 << i)) IR_PORT |= (1 << TX_PIN);
        else IR_PORT &= ~(1 << TX_PIN);
        _delay_us(BIT_DELAY_US);
    }
    IR_PORT |= (1 << TX_PIN);
    _delay_us(BIT_DELAY_US);
}

void uart_send_hex(uint32_t value) {
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xF;
        uart_send_char(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    uart_send_char('\r');
    uart_send_char('\n');
}

void blink_led(uint8_t times) {
    for (uint8_t i = 0; i < times; i++) {
        IR_PORT |= (1 << LED_PIN);
        _delay_ms(200);
        IR_PORT &= ~(1 << LED_PIN);
        _delay_ms(200);
    }
}

void setup() {
    IR_DDR &= ~(1 << IR_PIN);
    IR_PORT |= (1 << IR_PIN);
    IR_DDR |= (1 << LED_PIN);
    IR_PORT &= ~(1 << LED_PIN);
    IR_DDR |= (1 << TX_PIN);
    IR_PORT |= (1 << TX_PIN);

    OUT_DDR |= (1 << PIN_POWER) | (1 << PIN_UP) | (1 << PIN_DOWN) |
               (1 << PIN_MENU)  | (1 << PIN_RIGHT) | (1 << PIN_LEFT);
    OUT_PORT = 0x00;

    BTN_DDR &= ~(1 << BTN_LEARN);
    BTN_PORT |= (1 << BTN_LEARN);

    TCCR1B = (1 << CS11);

    blink_led(2);
}

uint32_t decode_nec(void) {
    uint16_t t;
    uint32_t data = 0;

    while (IR_PINREG & (1 << IR_PIN));
    TCNT1 = 0;
    while (!(IR_PINREG & (1 << IR_PIN)));
    t = TCNT1;
    if (t < LEAD_LOW_MIN) return 0;

    TCNT1 = 0;
    while (IR_PINREG & (1 << IR_PIN));
    t = TCNT1;
    if (t < LEAD_HIGH_MIN) return 0;

    for (uint8_t i = 0; i < 32; i++) {
        while (IR_PINREG & (1 << IR_PIN));
        while (!(IR_PINREG & (1 << IR_PIN)));
        TCNT1 = 0;
        while (IR_PINREG & (1 << IR_PIN));
        t = TCNT1;
        data <<= 1;
        if (t > BIT_HIGH_TH) data |= 1;
    }
    return data;
}

void trigger_output(uint8_t pin) {
    OUT_PORT |= (1 << pin);
    for (uint16_t i = 0; i < 400; i++) _delay_ms(1);
    OUT_PORT &= ~(1 << pin);
}

int main(void) {
    setup();
    uint32_t code;
    uint8_t learning_index = 0;

    while (1) {
        uint8_t learning = !(IN_PINREG & (1 << BTN_LEARN));
        code = decode_nec();
        if (code) {
            uart_send_hex(code);
            if (learning) {
                known_codes[learning_index] = code;
                trigger_output(output_pins[learning_index]);
                blink_led(1);
                learning_index = (learning_index + 1) % 6;
            } else {
                for (uint8_t i = 0; i < 6; i++) {
                    if (code == known_codes[i]) {
                        trigger_output(output_pins[i]);
                        break;
                    }
                }
            }
        }
    }
    return 0;
}
