/*
 * Author: LC Wang <zaq14760@gmail.com>
 * Date: 2026-06-26
 *
 * Clip-region image display for the 2.13" mono PixPaper (250x122 landscape).
 * The visible content is confined to a shape (circle or rectangle) centred on
 * the panel; only that region's bounding rectangle is clocked to the panel on
 * each update, so the rest of the screen is never disturbed.
 *
 * Self-contained: SPI, EPD command sequence, partial-refresh LUT, framebuffer
 * and the clip logic all live here. The GPIO backend is board-specific and
 * must, before including this header, provide:
 *
 *   epd_dc_set(v) / epd_rst_set(v) / epd_busy_get()
 *   int  epd_gpio_init(void)     acquire DC/RST/BUSY; return <0 on error
 *   void epd_gpio_release(void)  release the lines and close the chip
 *
 * Note: SSD1680 partial-refresh windows are rectangular. A circular region is
 * refreshed via its bounding box with the corners written as background; the
 * circular shape itself is enforced in software by fb_set().
 */
#ifndef CLIP_APP_H
#define CLIP_APP_H

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include "demo_image.h"
#include "demo_image_b.h"

#define EPD_SPI_DEVICE "/dev/spidev0.0"
#define SPI_SPEED 5000000

#define DISP_W 250
#define DISP_H 128
#define DISP_VIS 122
#define DISP_STRIDE (DISP_H / 8)
#define DISP_BUF_SIZE (DISP_W * DISP_STRIDE)
#define DISP_GATE_MAX (DISP_W - 1)

static int g_portrait;
static int scr_w = DISP_W;
static int scr_h = DISP_VIS;

int spi_fd;

void sleep_ms(unsigned int milliseconds) {
	struct timespec ts;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1000000;
	nanosleep(&ts, NULL);
}

void sleep_us(unsigned int microseconds) {
	struct timespec ts;
	ts.tv_sec = microseconds / 1000000;
	ts.tv_nsec = (microseconds % 1000000) * 1000;
	nanosleep(&ts, NULL);
}

void spi_write(uint8_t *data, int len) {
	struct spi_ioc_transfer tr = {
	    .tx_buf = (unsigned long)data,
	    .len = len,
	    .speed_hz = SPI_SPEED,
	    .bits_per_word = 8,
	};
	ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
}

void epd_writeCommand(uint8_t command) {
	epd_dc_set(0);
	sleep_us(1);
	spi_write(&command, 1);
}

void epd_writeData(uint8_t data) {
	epd_dc_set(1);
	sleep_us(1);
	spi_write(&data, 1);
}

void epd_writeData_bulk(const uint8_t *data, int len) {
	epd_dc_set(1);
	sleep_us(1);
	while (len > 0) {
		int chunk = len > 4096 ? 4096 : len;
		spi_write((uint8_t *)data, chunk);
		data += chunk;
		len -= chunk;
	}
}

void epd_waitUntilIdle() {
	sleep_ms(2);
	while (epd_busy_get() != 0)
		;
}

void epd_HWreset() {
	sleep_ms(50);
	epd_rst_set(0);
	sleep_ms(50);
	epd_rst_set(1);
	sleep_ms(50);
}

void epd_reg_init(void) {
	epd_waitUntilIdle();
	epd_writeCommand(0x12);
	epd_waitUntilIdle();

	epd_writeCommand(0x01);
	epd_writeData(0xF9);
	epd_writeData(0x00);
	epd_writeData(0x00);

	epd_writeCommand(0x11);
	epd_writeData(0x01);

	epd_writeCommand(0x44);
	epd_writeData(0x00);
	epd_writeData(0x0F);

	epd_writeCommand(0x45);
	epd_writeData(0xF9);
	epd_writeData(0x00);
	epd_writeData(0x00);
	epd_writeData(0x00);

	epd_writeCommand(0x3C);
	epd_writeData(0x05);

	epd_writeCommand(0x21);
	epd_writeData(0x00);
	epd_writeData(0x80);

	epd_writeCommand(0x18);
	epd_writeData(0x80);

	epd_writeCommand(0x4E);
	epd_writeData(0x00);
	epd_writeCommand(0x4F);
	epd_writeData(0xF9);
	epd_writeData(0x00);

	epd_waitUntilIdle();
}

void epd_init(void) {
	spi_fd = open(EPD_SPI_DEVICE, O_RDWR);
	if (spi_fd < 0) {
		perror("Error opening SPI device");
		exit(1);
	}

	uint8_t spi_mode = SPI_MODE_0;
	ioctl(spi_fd, SPI_IOC_WR_MODE, &spi_mode);
	uint32_t speed = SPI_SPEED;
	ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

	if (epd_gpio_init() < 0)
		exit(1);

	epd_HWreset();
	sleep_ms(1000);
	epd_waitUntilIdle();
	epd_reg_init();
}

static void epd_set_window(int xb_start, int xb_end, int g_start, int g_end) {
	epd_writeCommand(0x44);
	epd_writeData(xb_start & 0xFF);
	epd_writeData(xb_end & 0xFF);

	epd_writeCommand(0x45);
	epd_writeData(g_start & 0xFF);
	epd_writeData((g_start >> 8) & 0xFF);
	epd_writeData(g_end & 0xFF);
	epd_writeData((g_end >> 8) & 0xFF);
}

static void epd_set_cursor(int xb, int g) {
	epd_writeCommand(0x4E);
	epd_writeData(xb & 0xFF);

	epd_writeCommand(0x4F);
	epd_writeData(g & 0xFF);
	epd_writeData((g >> 8) & 0xFF);
}

static void epd_set_full_window(void) {
	epd_set_window(0x00, DISP_STRIDE - 1, DISP_GATE_MAX, 0x00);
}

static void epd_set_full_cursor(void) {
	epd_set_cursor(0x00, DISP_GATE_MAX);
}

void epd_set_base_map(const uint8_t *buf) {
	epd_set_full_window();

	epd_set_full_cursor();
	epd_writeCommand(0x24);
	epd_writeData_bulk(buf, DISP_BUF_SIZE);

	epd_set_full_cursor();
	epd_writeCommand(0x26);
	epd_writeData_bulk(buf, DISP_BUF_SIZE);

	epd_writeCommand(0x22);
	epd_writeData(0xF7);
	epd_writeCommand(0x20);
	epd_waitUntilIdle();
}

static const uint8_t WF_PARTIAL[159] = {
	0x0, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x80, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x40, 0x40, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x80, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x14, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x1, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
	0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x0, 0x0, 0x0,
	0x22, 0x17, 0x41, 0x0, 0x32, 0x36,
};

static const int g_phase0_tp = 0x10;
static const int g_fr = 3;

void epd_load_partial_lut(void) {
	uint8_t fr_byte = ((g_fr & 7) << 4) | (g_fr & 7);
	epd_writeCommand(0x32);
	for (int i = 0; i < 153; i++) {
		uint8_t b = WF_PARTIAL[i];
		if (i == 60)
			b = (uint8_t)g_phase0_tp;
		else if (i >= 144 && i <= 149)
			b = fr_byte;
		epd_writeData(b);
	}
	epd_waitUntilIdle();

	epd_writeCommand(0x3F);
	epd_writeData(WF_PARTIAL[153]);
	epd_writeCommand(0x03);
	epd_writeData(WF_PARTIAL[154]);
	epd_writeCommand(0x04);
	epd_writeData(WF_PARTIAL[155]);
	epd_writeData(WF_PARTIAL[156]);
	epd_writeData(WF_PARTIAL[157]);
	epd_writeCommand(0x2C);
	epd_writeData(WF_PARTIAL[158]);

	epd_writeCommand(0x37);
	epd_writeData(0x00);
	epd_writeData(0x00);
	epd_writeData(0x00);
	epd_writeData(0x00);
	epd_writeData(0x00);
	epd_writeData(0x40);
	epd_writeData(0x00);
	epd_writeData(0x00);
	epd_writeData(0x00);
	epd_writeData(0x00);

	epd_writeCommand(0x3C);
	epd_writeData(0x80);
}

static void epd_partial_regs(void) {
	epd_writeCommand(0x01);
	epd_writeData(0xF9);
	epd_writeData(0x00);
	epd_writeData(0x00);

	epd_writeCommand(0x11);
	epd_writeData(0x01);

	epd_writeCommand(0x21);
	epd_writeData(0x00);
	epd_writeData(0x80);

	epd_writeCommand(0x18);
	epd_writeData(0x80);
}

#define DISPLAY_PART_KEEP_ON 0x0C

void epd_partial_begin(void) {
	epd_rst_set(0);
	sleep_ms(2);
	epd_rst_set(1);
	sleep_ms(2);

	epd_partial_regs();
	epd_load_partial_lut();

	epd_writeCommand(0x22);
	epd_writeData(0xC0);
	epd_writeCommand(0x20);
	epd_waitUntilIdle();
}

void epd_partial_end(void) {
	epd_writeCommand(0x22);
	epd_writeData(0x03);
	epd_writeCommand(0x20);
	epd_waitUntilIdle();
}

static uint8_t frame_buf[DISP_BUF_SIZE];

enum clip_shape { CLIP_RECT, CLIP_CIRCLE };

static struct {
	enum clip_shape shape;
	int cx, cy;
	int w, h;
} g_clip = { CLIP_CIRCLE, DISP_W / 2, DISP_VIS / 2, 112, 112 };

static int clip_inside(int x, int y) {
	int dx = x - g_clip.cx, dy = y - g_clip.cy;
	if (g_clip.shape == CLIP_CIRCLE) {
		int r = (g_clip.w < g_clip.h ? g_clip.w : g_clip.h) / 2;
		return dx * dx + dy * dy <= r * r;
	}
	return 2 * (dx < 0 ? -dx : dx) <= g_clip.w &&
	       2 * (dy < 0 ? -dy : dy) <= g_clip.h;
}

/* Set one pixel to ink (black), ignoring the clip region. */
static inline void fb_put(uint8_t *buf, int x, int y) {
	int gate, bit;

	if (x < 0 || x >= scr_w || y < 0 || y >= scr_h)
		return;

	if (g_portrait) {
		gate = y;
		bit  = DISP_VIS - 1 - x;
	} else {
		gate = x;
		bit  = y;
	}
	buf[gate * DISP_STRIDE + (bit >> 3)] &= ~(0x80 >> (bit & 7));
}

static inline void fb_set(uint8_t *buf, int x, int y) {
	if (clip_inside(x, y))
		fb_put(buf, x, y);
}

static void fb_clear(uint8_t *buf) {
	memset(buf, 0xFF, DISP_BUF_SIZE);
}

static void clip_bbox(int *x0, int *y0, int *x1, int *y1) {
	int hw, hh;
	if (g_clip.shape == CLIP_CIRCLE) {
		hw = hh = (g_clip.w < g_clip.h ? g_clip.w : g_clip.h) / 2;
	} else {
		hw = g_clip.w / 2;
		hh = g_clip.h / 2;
	}
	*x0 = g_clip.cx - hw; *x1 = g_clip.cx + hw;
	*y0 = g_clip.cy - hh; *y1 = g_clip.cy + hh;
	if (*x0 < 0) *x0 = 0;
	if (*y0 < 0) *y0 = 0;
	if (*x1 > scr_w - 1) *x1 = scr_w - 1;
	if (*y1 > scr_h - 1) *y1 = scr_h - 1;
}

/*
 * Push only the clip region's bounding box to the panel. The logical box maps
 * to a contiguous (gate, byte) rectangle in RAM; the window/cursor convention
 * and traversal order mirror the full-frame path (epd_set_base_map) restricted
 * to that box, so the result matches a full refresh over the same pixels.
 */
static void epd_refresh_region(void) {
	int x0, y0, x1, y1;
	clip_bbox(&x0, &y0, &x1, &y1);

	int gA, gB, bitlo, bithi;
	if (g_portrait) {
		gA = y0; gB = y1;
		bitlo = DISP_VIS - 1 - x1;
		bithi = DISP_VIS - 1 - x0;
	} else {
		gA = x0; gB = x1;
		bitlo = y0;
		bithi = y1;
	}
	int bA = bitlo >> 3, bB = bithi >> 3;

	epd_set_window(bA, bB, gB, gA);
	epd_set_cursor(bA, gB);

	epd_writeCommand(0x24);
	for (int g = gA; g <= gB; g++)
		epd_writeData_bulk(&frame_buf[g * DISP_STRIDE + bA], bB - bA + 1);

	epd_writeCommand(0x22);
	epd_writeData(DISPLAY_PART_KEEP_ON);
	epd_writeCommand(0x20);
	epd_waitUntilIdle();
}

/* Paint everything outside the clip region black; the region keeps its
 * content. This black surround is established once by the full base map and is
 * never touched again, so a region-only refresh leaves it perfectly still. */
static void fill_outside_black(uint8_t *buf) {
	for (int y = 0; y < scr_h; y++)
		for (int x = 0; x < scr_w; x++)
			if (!clip_inside(x, y))
				fb_put(buf, x, y);
}

struct image {
	int w, h, stride;
	const unsigned char *bits;
};

static const struct image g_images[] = {
	{ IMG_W,  IMG_H,  IMG_STRIDE,  img_bits },
	{ IMGB_W, IMGB_H, IMGB_STRIDE, imgb_bits },
};
#define NUM_IMAGES ((int)(sizeof(g_images) / sizeof(g_images[0])))

static void draw_image_centered(uint8_t *buf, const struct image *im) {
	int ox = g_clip.cx - im->w / 2;
	int oy = g_clip.cy - im->h / 2;
	for (int row = 0; row < im->h; row++)
		for (int col = 0; col < im->w; col++)
			if (im->bits[row * im->stride + (col >> 3)] & (0x80 >> (col & 7)))
				fb_set(buf, ox + col, oy + row);
}

static void render(int idx) {
	fb_clear(frame_buf);
	draw_image_centered(frame_buf, &g_images[idx]);
	fill_outside_black(frame_buf);
}

int main(int argc, char **argv) {
	int hold_ms = 1500;
	int cycles = 0;                 /* 0 = loop forever */

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "circle"))
			g_clip.shape = CLIP_CIRCLE;
		else if (!strcmp(argv[i], "rect"))
			g_clip.shape = CLIP_RECT;
		else if (!strcmp(argv[i], "portrait"))
			g_portrait = 1;
		else if (!strcmp(argv[i], "landscape"))
			g_portrait = 0;
		else if (!strncmp(argv[i], "size=", 5))
			g_clip.w = g_clip.h = atoi(argv[i] + 5);
		else if (!strncmp(argv[i], "w=", 2))
			g_clip.w = atoi(argv[i] + 2);
		else if (!strncmp(argv[i], "h=", 2))
			g_clip.h = atoi(argv[i] + 2);
		else if (!strncmp(argv[i], "hold=", 5))
			hold_ms = atoi(argv[i] + 5);
		else if (!strncmp(argv[i], "n=", 2))
			cycles = atoi(argv[i] + 2);
	}

	if (g_portrait) {
		scr_w = DISP_VIS;
		scr_h = DISP_W;
	} else {
		scr_w = DISP_W;
		scr_h = DISP_VIS;
	}

	g_clip.cx = scr_w / 2;
	g_clip.cy = scr_h / 2;
	if (g_clip.w > scr_w) g_clip.w = scr_w;
	if (g_clip.h > scr_h) g_clip.h = scr_h;

	epd_init();

	/* One full refresh establishes the black surround + first image. */
	render(0);
	epd_set_base_map(frame_buf);
	sleep_ms(500);
	epd_partial_begin();

	/* From here only the region's bounding box is pushed: the black
	 * surround never moves, proving the refresh is region-local. */
	int idx = 0;
	for (int c = 0; cycles == 0 || c < cycles; c++) {
		sleep_ms(hold_ms);
		idx = (idx + 1) % NUM_IMAGES;
		render(idx);
		epd_refresh_region();
	}

	epd_partial_end();
	close(spi_fd);
	epd_gpio_release();
	return 0;
}

#endif /* CLIP_APP_H */
