#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
// Our assembled program:
#include "pmemtest.pio.h"
#include "st7789.h"
#include "sserif13.h"
#include "gui.h"

PIO pio;
uint sm = 0;

#define GPIO_QUAD_A 22
#define GPIO_QUAD_B 26
#define GPIO_QUAD_BTN 27
#define GPIO_BACK_BTN 28

const char *main_menu_items[] = {"Item one", "Item two", "Item three", "Item four",
                                 "Item five", "Item six", "Item seven", "Item eight",
                                 "Item nine", "Item ten" };
gui_listbox_t main_menu = {7, 40, 220, 10, 4, 0, 0, main_menu_items};

// Routines for reading and writing memory.
int ram_read(int addr)
{
        pio_sm_put(pio, sm, 0 |                     // Fast page mode flag
                            0 << 1 |                // Write flag
                            (addr & 0xff) << 2 |    // Row address
                            (addr & 0xff00) << 2|   // Column address
                            ((0 & 1) << 18));       // Data bit
        while (pio_sm_is_rx_fifo_empty(pio, sm)) {} // Wait for data to arrive
        return pio_sm_get(pio, sm);                 // Return the data
}

void ram_write(int addr, int data)
{
        pio_sm_put(pio, sm, 0 |                     // Fast page mode flag
                            1 << 1 |                // Write flag
                            (addr & 0xff) << 2 |    // Row address
                            (addr & 0xff00) << 2|   // Column address
                            ((data & 1) << 18));    // Data bit
        while (pio_sm_is_rx_fifo_empty(pio, sm)) {} // Wait for dummy data
        pio_sm_get(pio, sm);                        // Discard the dummy data bit
}

int addr_map(int addr)
{
    return addr;
 //   return (addr >> 8) | ((addr & 0xFF) << 8);
}

// Fills the RAM with the provided data.
void ram_fill(int range, int data)
{
    int i;
    for (i = 0; i < range; i++) {
        ram_write(addr_map(i), data);
    }
}

static inline bool me_r0(int a)
{
    int bit = ram_read(a);
    return (bit == 0);
}

static inline bool me_r1(int a)
{
    int bit = ram_read(a);
    return (bit == 1);
}

static inline bool me_w0(int a)
{
    ram_write(a, 0);
    return true;
}

static inline bool me_w1(int a)
{
    ram_write(a, 1);
    return true;
}

static inline bool marchb_m0(int a)
{
    me_w0(a);
    return true;
}

static inline bool marchb_m1(int a)
{
    return me_r0(a) && me_w1(a) && me_r1(a) && me_w0(a) && me_r0(a) && me_w1(a);
}

static inline bool marchb_m2(int a)
{
    return me_r1(a) && me_w0(a) && me_w1(a);
}

static inline bool marchb_m3(int a)
{
    return me_r1(a) && me_w0(a) && me_w1(a) && me_w0(a);
}

static inline bool marchb_m4(int a)
{
    return me_r0(a) && me_w1(a) && me_w0(a);
}

static inline bool march_element(int addr_size, bool descending, int algorithm)
{
    int inc = descending ? -1 : 1;
    int start = descending ? (addr_size - 1) : 0;
    int end = descending ? -1 : addr_size;
    int a;
    bool ret;

    for (a = start; a != end; a += inc) {
        switch (algorithm) {
            case 0:
                ret = marchb_m0(a);
                break;
            case 1:
                ret = marchb_m1(a);
                break;
            case 2:
                ret = marchb_m2(a);
                break;
            case 3:
                ret = marchb_m3(a);
                break;
            case 4:
                marchb_m4(a);
                break;
            default:
                break;
        }
        if (!ret) return false;
    }
    return true;
}

bool marchb_test(int addr_size)
{
    bool ret = true;
    printf("M0 ");
    ret = march_element(addr_size, false, 0);
    if (!ret) return false;
    printf("M1 ");
    ret = march_element(addr_size, false, 1);
    if (!ret) return false;
    printf("M2 ");
    ret = march_element(addr_size, false, 2);
    if (!ret) return false;
    printf("M3 ");
    ret = march_element(addr_size, true, 3);
    if (!ret) return false;
    printf("M4 ");
    ret = march_element(addr_size, true, 4);
    if (!ret) return false;
    return ret;
}

int ram_toggle_check(int range, uint expected)
{
    int i, j;
    uint bit;
    int retval = -1;
    for (i = 0; i < range; i++) {
        bit = ram_read(addr_map(i));
        if (bit != expected) retval = i; //return i;
        //retval = (retval << 1) | (bit & 0x1);
        gpio_put(15, bit);
        gpio_put(15, bit);
//        for (j = 0; j < 100; j++) {
            gpio_put(15, 0);
//        }
        ram_write(addr_map(i), (~expected) & 1);
    }
    return retval;
}

int ramtest(int range)
{
    int retval1, retval2;
    ram_fill(range, 1);
    retval1 = ram_toggle_check(range, 1);
    retval2 = ram_toggle_check(range, 0);
    printf("1: %x. 2: %x\n", retval1, retval2);
    if (retval1 != -1) return retval1;
    if (retval2 != -1) return retval2;
    return -1;
}

// Also need some testing algorithms... :)

typedef struct {
    uint32_t pin;
    uint32_t hcount;
} pin_debounce_t;

#define DEBOUNCE_COUNT 1000

// Debounces a pin
uint8_t do_debounce(pin_debounce_t *d)
{
    if (gpio_get(d->pin)) {
        d->hcount++;
        if (d->hcount > DEBOUNCE_COUNT) d->hcount = DEBOUNCE_COUNT;
    } else {
        d->hcount = 0;
    }
    return (d->hcount >= DEBOUNCE_COUNT) ? 1 : 0;
}

// Returns true only *once* when a button is pushed. No key repeat.
bool is_button_pushed(pin_debounce_t *pin_b)
{
    if (!gpio_get(pin_b->pin)) {
        if (pin_b->hcount < DEBOUNCE_COUNT) {
            pin_b->hcount++;
            if (pin_b->hcount == DEBOUNCE_COUNT) {
                return true;
            }
        }
    } else {
        pin_b->hcount = 0;
    }
    return false;
}

// Called when user presses the action button
void button_action()
{
    font_string(9, 56, "ACT", 0x0000, 0xffff, &sserif13, false);
    sleep_ms(100);
    st7789_fill(9, 56, 50, 13, 0xffff);
}

// Called when the user presses the back button
void button_back()
{
    font_string(9, 69, "BACK", 0x0000, 0xffff, &sserif13, false);
    sleep_ms(100);
    st7789_fill(9, 69, 50, 12, 0xffff);
}

void do_buttons()
{
    static pin_debounce_t action_btn = {GPIO_QUAD_BTN, 0};
    static pin_debounce_t back_btn = {GPIO_BACK_BTN, 0};
    if (is_button_pushed(&action_btn)) button_action();
    if (is_button_pushed(&back_btn)) button_back();
}

static int wheel_val = 0;

void wheel_print()
{
    char a[] = "          ";
    sprintf(a, "%d  ", wheel_val);
    font_string(9, 43, a, 0x0000, 0xffff, &sserif13, false);
}

void wheel_increment()
{
    wheel_val++;
//    wheel_print();
    gui_listbox(&main_menu, LIST_ACTION_DOWN);
}

void wheel_decrement()
{
    wheel_val--;
//    wheel_print();
    gui_listbox(&main_menu, LIST_ACTION_UP);
}

void do_encoder()
{
    static pin_debounce_t pin_a = {GPIO_QUAD_A, 0};
    static pin_debounce_t pin_b = {GPIO_QUAD_B, 0};
    static uint8_t wheel_state_old = 0;
    uint8_t st;
    uint8_t wheel_state;

    wheel_state = do_debounce(&pin_a) | (do_debounce(&pin_b) << 1);
    st = wheel_state | (wheel_state_old << 4);
    if (wheel_state != wheel_state_old) {
        // Present state, next state
        // 00 -> 01 clockwise
        // 10 -> 11 counterclockwise
        // 11 -> 10 clockwise
        // 01 -> 00 counterclockwise
        if ((st == 0x01) || (st == 0x32)) {
            wheel_increment();
        }
        if ((st == 0x23) || (st == 0x10)) {
            wheel_decrement();
        }
        wheel_state_old = wheel_state;
    }
}

void init_buttons_encoder()
{
    gpio_init(GPIO_QUAD_A);
    gpio_init(GPIO_QUAD_B);
    gpio_init(GPIO_QUAD_BTN);
    gpio_init(GPIO_BACK_BTN);
    gpio_set_dir(GPIO_QUAD_A, GPIO_IN);
    gpio_set_dir(GPIO_QUAD_B, GPIO_IN);
    gpio_set_dir(GPIO_QUAD_BTN, GPIO_IN);
    gpio_set_dir(GPIO_BACK_BTN, GPIO_IN);

}

int main() {
    uint pin = 5; // Starting GPIO for memory interface
    uint offset;
    uint16_t addr;
    uint8_t db = 0;
    uint din = 0;
    int i, retval;

    // PLL->prim = 0x51000.

    //stdio_uart_init_full(uart0, 57600, 28, 29); // 28=tx, 29=rx actually runs at 115200 due to overclock
    //gpio_init(15);
    //gpio_set_dir(15, GPIO_OUT);

    //printf("Test.\n");

    // Init display
    st7789_init();
//    gui_demo();
    paint_dialog("Select Device");
    gui_listbox(&main_menu, LIST_ACTION_NONE);
    init_buttons_encoder();
    while(1) {
        do_encoder();
        do_buttons();
    }

    bool rc = pio_claim_free_sm_and_add_program_for_gpio_range(&pmemtest_program, &pio, &sm, &offset, pin, 2, true);
    pmemtest_program_init(pio, sm, offset, pin);
    pio_sm_set_enabled(pio, sm, true);

    while(1) {
        //retval = ramtest(65536);
//        printf("Begin march test.\n");
        retval = marchb_test(65536);
//        printf("Rv: %d\n", retval);
    }

    return 0;
}
