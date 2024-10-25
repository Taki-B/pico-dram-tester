#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#include "st7789.h"

#include "rtc_image.h"
//#include "prop_font.h"
//#include "sserif13.h"
#include "sserif16.h"
#include "sserif20.h"
#include "widgets16.h"

#define PIN_SPI_CS 1
#define PIN_SPI_SCK 2
#define PIN_SPI_DO 3
#define PIN_SPI_DC 0

#define PIN_7789_RST 6 // FIXME no more reset

#define RESET_DELAY 140

#define CMD_NOP      0x00
#define CMD_SWRESET  0x01
#define CMD_SLPIN    0x10
#define CMD_SLPOUT   0x11
#define CMD_NORON    0x13
#define CMD_INVOFF   0x20
#define CMD_INVON    0x21
#define CMD_DISPOFF  0x28
#define CMD_DISPON   0x29
#define CMD_CASET    0x2A
#define CMD_RASET    0x2B
#define CMD_RAMWR    0x2C
#define CMD_MADCTL   0x36
#define CMD_COLMOD   0x3A

// A few colors
#define COLOR_BLACK 0x0000
#define COLOR_DKBLUE 0xa800
#define COLOR_DKGREEN 0x0540
#define COLOR_DKCYAN 0xad40
#define COLOR_DKRED 0x0015
#define COLOR_DKMAGENTA 0xa815
#define COLOR_DKYELLOW 0x0555
#define COLOR_LTGRAY 0xad55
#define COLOR_DKGRAY 0x52aa
#define COLOR_BLUE 0xfaaa
#define COLOR_GREEN 0x57ea
#define COLOR_CYAN 0xffea
#define COLOR_RED 0x52bf
#define COLOR_MAGENTA 0xfabf
#define COLOR_YELLOW 0x57ff
#define COLOR_WHITE 0xffff

// Display corner isn't always 0,0
static uint16_t _x_offset = 0;
static uint16_t _y_offset = 0;

// Mode for transmitting a command
static inline void mode_cmd()
{
    gpio_put(PIN_SPI_DC, 0);
}

// Mode for transmitting data
static inline void mode_data()
{
    gpio_put(PIN_SPI_DC, 1);
}

// Select the device
static inline void cs_low()
{
    asm volatile("nop \n nop \n nop");
    gpio_put (PIN_SPI_CS, 0);
    asm volatile("nop \n nop \n nop");
}

// Deselect the device
static inline void cs_high()
{
    asm volatile("nop \n nop \n nop");
    gpio_put (PIN_SPI_CS, 1);
    asm volatile("nop \n nop \n nop");
}

// Write a command byte
static void write_command(uint8_t cmd, uint8_t num_bytes, uint8_t buf[], bool cs)
{
    if (cs) cs_low();
    mode_cmd();
    // Write command byte
    spi_write_blocking(spi0, &cmd, 1);
    mode_data();
    // Write data bytes
    if (num_bytes > 0) {
        spi_write_blocking(spi0, buf, num_bytes);
    }
    if (cs) cs_high();
}

// Write a 16-bit value
static void write_data16(size_t num_words, uint16_t *buf)
{
    hw_write_masked(&spi_get_hw(spi0)->cr0, 15 << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);
    spi_write16_blocking(spi0, buf, num_words);
    hw_write_masked(&spi_get_hw(spi0)->cr0, 7 << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);
}

static void st7789_gpio_init()
{
    // Set up GPIO for SPI0
    spi_init(spi0, 70 * 1000 * 1000);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_DO, GPIO_FUNC_SPI);

    gpio_init(PIN_SPI_CS);
    gpio_set_dir(PIN_SPI_CS, GPIO_OUT);
    gpio_put(PIN_SPI_CS, 1);

    gpio_init(PIN_SPI_DC);
    gpio_set_dir(PIN_SPI_DC, GPIO_OUT);
    gpio_put(PIN_SPI_DC, 0);

    gpio_init(PIN_7789_RST);
    gpio_set_dir(PIN_7789_RST, GPIO_OUT);
    gpio_put(PIN_7789_RST, 0);
    sleep_ms(RESET_DELAY);
    gpio_put(PIN_7789_RST, 1);

}

// Set the window size for data writes
static inline void st7789_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height, bool cs)
{
    uint16_t sx = x + _x_offset;
    uint16_t sy = y + _y_offset;
    uint16_t ex = x + width - 1 + _x_offset;
    uint16_t ey = y + height - 1 + _y_offset;
    write_command(CMD_CASET, 4, (uint8_t []){sy >> 8, sy & 0xff, ey >> 8, ey & 0xff}, cs);
    write_command(CMD_RASET, 4, (uint8_t []){sx >> 8, sx & 0xff, ex >> 8, ex & 0xff}, cs);
}

// Low level display initialization.
void st7789_disp_init(uint16_t xoff, uint16_t yoff, uint16_t width, uint16_t height)
{
    _x_offset = xoff;
    _y_offset = yoff;
    write_command(CMD_SWRESET, 0, NULL, true);                   // Software reset
    sleep_ms(120);
    write_command(CMD_SLPOUT, 0, NULL, true);                    // Exit sleep mode
    sleep_ms(5);
    write_command(CMD_COLMOD, 1, (uint8_t []){ 0x55 }, true);    // Interface format: 16 bit color
    write_command(CMD_MADCTL, 1, (uint8_t []){ 0x48 }, true);    // Memory address control: BGR
    st7789_window(0, 0, width, height, true);                    // Set display size
    write_command(CMD_INVON, 0, NULL, true);                     // Display inversion on
    write_command(CMD_NORON, 0, NULL, true);                     // Partial off = normal
    write_command(CMD_DISPON, 0, NULL, true);                    // Display on
}

// Draw a filled rectangle.
void st7789_fill(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height, uint16_t col)
{
    int count;
    cs_low();
    st7789_window(sx, sy, width, height, false);
    write_command(CMD_RAMWR, 0, NULL, false);
    hw_write_masked(&spi_get_hw(spi0)->cr0, 15 << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);
    for (count = 0; count < width * height; count++) {
        spi_write16_blocking(spi0, &col, 1);
    }
    hw_write_masked(&spi_get_hw(spi0)->cr0, 7 << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);
    cs_high();
}

// Fill with 50% halftone
void st7789_halftone_fill(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height, uint16_t c1, uint16_t c2)
{
    int row, col;
    uint8_t rowstate = 0;
    uint8_t colstate = 0;
    for (row = 0; row < height; row++) {
        rowstate = ~rowstate;
        colstate = rowstate;
        cs_low();
        st7789_window(sx, sy + row, width, 1, false);
        write_command(CMD_RAMWR, 0, NULL, false);
        hw_write_masked(&spi_get_hw(spi0)->cr0, 15 << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);
        for (col = 0; col < width; col++) {
            colstate = ~colstate;
            spi_write16_blocking(spi0, (colstate) ? &c1 : &c2, 1);
        }
        hw_write_masked(&spi_get_hw(spi0)->cr0, 7 << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);
        cs_high();
    }
}

// Plots a bitmap. Must be 16bpp and match the display type (BGR 565)
void st7789_bitblt(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height, uint16_t *buf)
{
    cs_low();
    st7789_window(sx, sy, width, height, false);
    write_command(CMD_RAMWR, 0, NULL, false);
    write_data16(width * height, buf);
    cs_high();
}

// Rotates the bitmap using a trick. A single-line bitblt assumes the orientation of the line.
void st7789_bitblt_rot(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height, uint16_t *buf)
{
    for (int count = 0; count < height; count++) {
        st7789_bitblt(sx, sy + count, width, 1, &buf[width * count]);
    }
}

// Plot a single pixel. Inefficient!
static void pset(uint16_t x, uint16_t y, uint16_t col)
{
    uint8_t db[] = {0, 0, 0, 0};
    cs_low();
    st7789_window(x, y, 1, 1, false);
    write_command(CMD_RAMWR, 0, NULL, false);
    write_data16(1, &col);
    cs_high();
}

// Get the width of a string using a particular font
uint16_t font_string_width(char *text, const font_def_t *font, bool bold)
{
    uint16_t total_width = 0;

    while (*text) {
        if (*text >= font->count) {
            text++;
            continue;
        }
        total_width += font->widths[*(text++)] + (bold ? 1 : 0);
    }
    return total_width;
}

// Draws a string at the specific coordinates using the default font
void font_string(uint16_t x, uint16_t y, char *text,
                 uint16_t fg_color, uint16_t bg_color,
                 const font_def_t *font, bool bold)
{
    uint8_t row, col;
    uint16_t total_width, width;
    uint16_t offset;
    uint8_t bytes_column;
    uint8_t byte;
    uint8_t db;
    uint8_t col_count;
    char *text_buf = text;
    uint8_t prev_bit;

    // First, compute total width
    total_width = font_string_width(text, font, bold);

    for (row = 0; row < font->height; row++) {
        // Set the window
        cs_low();
        st7789_window(x, y + row, total_width, 1, false);
        write_command(CMD_RAMWR, 0, NULL, false);
        hw_write_masked(&spi_get_hw(spi0)->cr0, 15 << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);

        text_buf = text;
        while (*text_buf) {
            // Get information about this character
            if (*text_buf >= font->count) {
                text_buf++; // Skip if invalid
                continue;
            }
            width = font->widths[*text_buf];
            bytes_column = (width + 7) >> 3;
            offset = font->offsets[*text_buf];
            // Get exact byte offset into the character table
            offset += row * bytes_column;
            prev_bit = 0;
            for (byte = 0; byte < bytes_column; byte++) {
                db = font->data[offset + byte];
                col_count = (width > 8) ? 8 : width;
                for (col = 0; col < col_count; col++) {
                    if ((db & 0x1) || (bold && (prev_bit == 1))) {
                        // Emit foreground color
                        spi_write16_blocking(spi0, &fg_color, 1);
                    } else {
                        // Emit background color
                        spi_write16_blocking(spi0, &bg_color, 1);
                    }
                    prev_bit = db & 0x1;
                    db = db >> 1;
                }
               width -= col_count;
            }
            if (bold) {
                if (prev_bit) {
                    spi_write16_blocking(spi0, &fg_color, 1);
                } else {
                    spi_write16_blocking(spi0, &bg_color, 1);
                }
            }

            text_buf++;
        }
        hw_write_masked(&spi_get_hw(spi0)->cr0, 7 << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS);
        cs_high();
    }
}

// Colors for fancy rectangles
uint16_t color_fill = COLOR_LTGRAY;
uint16_t color_outline = COLOR_BLACK;
uint16_t color_shadow = COLOR_DKGRAY;
uint16_t color_highlight = COLOR_WHITE;
uint16_t color_field = COLOR_WHITE;

// Two-color rectangle
void shadow_rect(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height, uint16_t tl, uint16_t br)
{
    st7789_fill(sx,             sy,              width - 1, 1,          tl);
    st7789_fill(sx,             sy,              1,         height - 1, tl);
    st7789_fill(sx + width - 1, sy,              1,         height,     br);
    st7789_fill(sx            , sy + height - 1, width,     1,          br);
}

typedef enum {
    W_RAISED_OUTER,
    W_RAISED_INNER,
    W_SUNKEN_OUTER,
    W_SUNKEN_INNER,
    WINDOW,
    B_RAISED_OUTER,
    B_RAISED_INNER,
    B_SUNKEN_OUTER,
    B_SUNKEN_INNER,
    BUTTON,
    FIELD,
    STATUS,
    GROUPING
} e_style;

void fancy_rect(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height, e_style style)
{
    switch (style) {
    case W_RAISED_OUTER:
        shadow_rect(sx, sy, width, height, color_fill, color_outline);
        break;
    case W_RAISED_INNER:
        shadow_rect(sx, sy, width, height, color_highlight, color_shadow);
        break;
    case W_SUNKEN_OUTER:
        shadow_rect(sx, sy, width, height, color_shadow, color_highlight);
        break;
    case W_SUNKEN_INNER:
        shadow_rect(sx, sy, width, height, color_outline, color_fill);
        break;
    case B_RAISED_OUTER:
        shadow_rect(sx, sy, width, height, color_highlight, color_outline);
        break;
    case B_RAISED_INNER:
        shadow_rect(sx, sy, width, height, color_fill, color_shadow);
        break;
    case B_SUNKEN_OUTER:
        shadow_rect(sx, sy, width, height, color_outline, color_highlight);
        break;
    case B_SUNKEN_INNER:
        shadow_rect(sx, sy, width, height, color_shadow, color_fill);
        break;
    case WINDOW:
        fancy_rect(sx, sy, width, height, W_RAISED_OUTER);
        fancy_rect(sx + 1, sy + 1, width - 2, height - 2, W_RAISED_INNER);
        st7789_fill(sx + 2, sy + 2, width - 4, height - 4, color_fill);
    case BUTTON:
        fancy_rect(sx, sy, width, height, B_RAISED_OUTER);
        fancy_rect(sx + 1, sy + 1, width - 2, height - 2, B_RAISED_INNER);
        st7789_fill(sx + 2, sy + 2, width - 4, height - 4, color_fill);
        break;
    case FIELD:
        fancy_rect(sx, sy, width, height, B_SUNKEN_OUTER);
        fancy_rect(sx + 1, sy + 1, width - 2, height - 2, B_SUNKEN_INNER);
        st7789_fill(sx + 2, sy + 2, width - 4, height - 4, color_field);
        break;
    case STATUS:
        fancy_rect(sx, sy, width, height, B_SUNKEN_OUTER);
        st7789_fill(sx + 1, sy + 1, width - 1, height - 1, color_fill);
    case GROUPING:
        fancy_rect(sx, sy, width, height, W_SUNKEN_OUTER);
        fancy_rect(sx + 1, sy + 1, width - 2, height - 2, W_RAISED_INNER);
        st7789_fill(sx + 2, sy + 2, width - 4, height - 4, color_fill);
        break;

    default:
        break;
    }
}

// Draws a button
void paint_button(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height,
                  char *text, const font_def_t *font, bool bold)
{
    uint16_t twidth = font_string_width(text, font, bold);
    uint16_t theight = font->height;
    uint16_t tsx = sx + (width / 2) - (twidth / 2);
    uint16_t tsy = sy + (height / 2) - (theight / 2);

    fancy_rect(sx, sy, width, height, BUTTON);
    font_string(tsx, tsy, text, COLOR_BLACK, color_fill, font, bold);
}

// Draws a dialog box
void paint_dialog(char *title)
{
    fancy_rect(0, 0, 240, 135, WINDOW);
    st7789_fill(3, 3, 240 - 7, 26, COLOR_DKBLUE);
    paint_button(240 - 6 - 21, 6, 21, 21,"\x01", &widgets16, false);
    font_string(5, 5, title, COLOR_WHITE, COLOR_DKBLUE, &sserif20, true);
}

// Lines visible
// Lines available
// Starting line
#define MIN_SCROLL_HEIGHT 10
void paint_scrollbar(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height,
                     uint32_t vis, uint32_t tot, uint32_t pos)
{
    // Position of marker
    uint32_t avail = height - 21 * 2; // Pixels representing the "total" scrollbar area
    uint32_t sb_height = avail * vis / tot; // Compute scrollbar height
    uint32_t start_pos;

    if (vis >= tot) {
        pos = 0;
        sb_height = avail;
    } else {
        if (pos > tot - vis) { // Is position out of range?
            pos = tot - vis;
        }
    }
    start_pos = pos * (avail / tot);

    // Buttons
    paint_button(sx, sy, width, 21, "\x03", &widgets16, false);
    paint_button(sx, sy + height - 21, width, 21, "\x02", &widgets16, false);
    st7789_halftone_fill(sx, sy + 21, width, height - 21 - 21, COLOR_WHITE, color_fill);
    // Position marker. Only show if there is room.
    if (sb_height >= MIN_SCROLL_HEIGHT) {
        paint_button(sx, sy + 21 + start_pos, width, sb_height, "\x00", &widgets16, false);
    }
}

// Initialize the display
void st7789_init()
{
    uint16_t count;
    uint16_t col;
    st7789_gpio_init();
    st7789_disp_init(40, 53, 240, 135);

    st7789_fill(0, 0, 240, 135, 0x001F); // Clear screen

#if 0
    // Pixel format: BGR (15-0) 5:6:5.
    for (count = 0; count < 64; count++) {
        st7789_fill(0, count, 100, 1, count << 11); // horizontal line
    }
#endif

    for (count = 0; count < 60; count++) {
        pset(count, count, 0xFFFF);
    }

//    st7789_bitblt_rot(0, 0, 240, 135, (uint16_t *)image_data);

      paint_dialog("Dialog Box");
      fancy_rect(3, 30, 210, 72, GROUPING);
      fancy_rect(7, 40, 190, 52, FIELD);
      font_string(9, 43, "Hello World Text Box", 0x0000, 0xffff, &sserif20, false);
      paint_button(45, 105, 90, 25, "OK", &sserif20, false);
      paint_scrollbar(7+190-2-23, 40+2, 23, 52 - 4, 1, 2, 0);
//      fancy_rect(5, 5, 8, 8, FIELD);
#if 0
    while (1) {
        for (count = 0; count < 36; count++) {
            st7789_bitblt_rot(0, count, 240, 100, (uint16_t *)image_data);
 //           sleep_ms(0);
        }
        for (count = 35; count > 0; count--) {
            st7789_bitblt_rot(0, count, 240, 100, (uint16_t *)image_data);
//            sleep_ms(0);
        }
    }
#endif
}
