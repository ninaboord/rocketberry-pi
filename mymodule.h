#ifndef _MY_MODULE_H

#define LEFT -1
#define RIGHT 1
#define SCALE 3

typedef struct {
	unsigned int width;
	unsigned int height;
	unsigned int bytes_per_pixel; //always 4
	unsigned char pixel_data[];
} img_t;

typedef struct {
	int x;
	int y;
    int width;
	int height;
} collider_t;

typedef struct {
    int velocity_x;
    int velocity_y;
	int x;
    int y;
	int status;
	int type;
	int anim_frame;
    collider_t *collider;
	const img_t *img;
} object_t;

#endif
