#include "gl.h"
#include "font.h"
#include "uart.h"
#include "printf.h"

void gl_init(unsigned int width, unsigned int height, gl_mode_t mode)
{
    fb_init(width, height, 4, mode);    // use 32-bit depth always for graphics library
}

void gl_swap_buffer(void)
{
    fb_swap_buffer();
}

unsigned int gl_get_width(void)
{
    return fb_get_width();
}

unsigned int gl_get_height(void)
{
    return fb_get_height();
}

color_t gl_color(unsigned char r, unsigned char g, unsigned char b)
{
    color_t color = 0xff000000; // alpha always set to 0xff
	color |= r << 16; // red
	color |= g << 8; // green
	color |= b; // blue
    return color;
}

void gl_clear(color_t c)
{
	// draw over whole screen
	int size = fb_get_pitch()/4 * fb_get_height();
	long long c_base = c;
	long long cc = (c_base << 32) | c_base;
	unsigned long long *pixel = fb_get_draw_buffer();
	for (int i = 0; i < size/2; i ++) {
		//pixel[i] = cc;
		unsigned long long *pixelset = &pixel[i];
	    pixelset[0] = cc;
		pixelset[1] = cc;
		pixelset[2] = cc;
		pixelset[3] = cc;
		pixelset[4] = cc;
		pixelset[5] = cc;
		pixelset[6] = cc;
		pixelset[7] = cc;
	}
}

void gl_draw_pixel(int x, int y, color_t c)
{
	if (x >= fb_get_width() || y >= fb_get_height())
		return; //don't draw if out of bounds
	
	unsigned int (*pixel)[fb_get_pitch() / 4] = fb_get_draw_buffer();
	pixel[y][x] = c;
}

color_t gl_read_pixel(int x, int y)
{
	if (x >= fb_get_width() || y >= fb_get_height())
		return 0; // return 0 if out of bounds
	
	unsigned int (*pixel)[fb_get_pitch() / 4] = fb_get_draw_buffer();
	return (color_t) pixel[y][x];

}

void gl_draw_rect(int x, int y, int w, int h, color_t c)
{
	// restrict drawing to bounds of screen
	if (x + w > fb_get_width())
		w = fb_get_width() - x;
	if (y + h > fb_get_height())
		h = fb_get_height() - y;
	int start_x = x;
	if (x < 0)
		start_x = 0;
	int start_y = y;
	if (y < 0)
		start_y = 0;

	unsigned int (*pixel)[fb_get_pitch() / 4] = fb_get_draw_buffer();
	for (int cur_y = start_y; cur_y < y + h; cur_y++)
		for (int cur_x = start_x; cur_x < x + w; cur_x++)
		    pixel[cur_y][cur_x] = c;
}

void gl_draw_img(int x, int y, const void *img, int scale){
    struct img_hdr {
		int width;
		int height;
		int bytes_per_pixel; // assume always four
	};
	struct img_hdr *hdr = (void *)img;
	int* image = (int *)(hdr + 1);
	
	int width_end = x + (hdr->width * scale);
	if (width_end > fb_get_width())
		width_end = fb_get_width();
	int height_end = y + (hdr->height * scale);
	if (height_end >fb_get_height())
		height_end = fb_get_height();
	int start_x = x;
	if (x < 0)
		start_x = 0;
	int start_y = y;
	if (y < 0)
		start_y = 0;
	int i = 0;
	for (int pixel_y = start_y; pixel_y < height_end; pixel_y += scale) {
		for (int pixel_x = start_x; pixel_x < width_end; pixel_x += scale) {
			if (image[i]) {
				//printf("%x ", image[i]);
			    gl_draw_rect(pixel_x, pixel_y, scale, scale, image[i]);
			}
			    
			i++;
		}
	}
}

void gl_draw_char(int x, int y, char ch, color_t c)
{
	unsigned char buf[font_get_glyph_size()];
    if (!font_get_glyph(ch, buf, sizeof(buf)))
		return; //do nothing on unsuccessful char

	// create 2d array for font template, pixels on screen
	char (* img)[font_get_glyph_width()] = (void *) buf; 
	unsigned int (*pixel)[fb_get_pitch() / 4] = fb_get_draw_buffer();

	// restrict bounds
	int width_end = x + font_get_glyph_width();
	if (width_end > fb_get_width())
		width_end = fb_get_width();
	int height_end = y + font_get_glyph_height();
	if (height_end > fb_get_height())
		height_end = fb_get_height();
	int start_x = x;
	if (x < 0)
		start_x = 0;
	int start_y = y;
	if (y < 0)
		start_y = 0;
	
	for (int pixel_y = start_y; pixel_y < height_end; pixel_y++) {
		for (int pixel_x = start_x; pixel_x < width_end; pixel_x++) {
			if (img[pixel_y - y][pixel_x - x]) { // if template pixel on
				pixel[pixel_y][pixel_x] = c; // draw to screen pixel
			}
		}
	}
	
}

void gl_draw_string(int x, int y, const char* str, color_t c)
{
    while (*str != '\0') {
		gl_draw_char(x, y, *str, c);
		x += font_get_glyph_width();
		if (x > fb_get_width())
			return;
		str++;
	}
}

unsigned int gl_get_char_height(void)
{
    return font_get_glyph_height();
}

unsigned int gl_get_char_width(void)
{
    return font_get_glyph_width();
}
