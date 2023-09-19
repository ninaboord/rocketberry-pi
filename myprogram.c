#include "uart.h"
#include "mymodule.h"
#include "gl.h"
#include "timer.h"
#include "accel.h"
#include "printf.h"
#include "malloc.h"
#include "rand.h"
#include "gpio.h"
#include "gpio_extra.h"
#include "gpio_interrupts.h"
#include "interrupts.h"
#include "ringbuffer.h"

#define LASER_SPEED 20
#define FRAMES 5
#define BUG_PENALTY 10
#define BANNER_HEIGHT 35
#define BANNER_COLOR 0xffaa8eed
#define TEXT_COLOR 0xffaa8eed
#define BACKGROUND_COLOR 0xff121015//0x0e200e

static const int BUTTON = GPIO_PIN23;
static int shootCount = 0;
static unsigned int last_click = 0;
static int points = 0;
static int MAX_LASERS = 4;
static int high_score = 0;
static unsigned int game_over = 0;
static unsigned int start_screen = 1;
static unsigned int num_asteroids = 2;
static unsigned int ultra = 30;
static unsigned int fast = 25;
static unsigned int medium = 20;
static unsigned int slow = 15;
#define INITIAL_SPAWNRATE 30;
static short a = 0;
static const img_t rocket_img;
static const img_t rocket_e1;
static const img_t rocket_e2;
static const img_t rocket_e3;
static const img_t rocket_e4;
static const img_t *rocket_anim[] = {&rocket_img, &rocket_e1, &rocket_e2, &rocket_e3, &rocket_e4};
static const img_t asteroid1_img;
static const img_t asteroid1_e1;
static const img_t asteroid1_e2;
static const img_t asteroid1_e3;
static const img_t asteroid1_e4;
static const img_t *asteroid1_anim[] = {&asteroid1_img, &asteroid1_e1, &asteroid1_e2, &asteroid1_e3, &asteroid1_e4};
static const img_t asteroid2_img;
static const img_t asteroid2_e1;
static const img_t asteroid2_e2;
static const img_t asteroid2_e3;
static const img_t asteroid2_e4;
static const img_t *asteroid2_anim[] = {&asteroid2_img, &asteroid2_e1, &asteroid2_e2, &asteroid2_e3, &asteroid2_e4};
static const img_t asteroid3_img;
static const img_t asteroid3_e1;
static const img_t asteroid3_e2;
static const img_t asteroid3_e3;
static const img_t asteroid3_e4;
static const img_t *asteroid3_anim[] = {&asteroid3_img, &asteroid3_e1, &asteroid3_e2, &asteroid3_e3, &asteroid3_e4};
static const img_t **asteroid_anims[] = {asteroid1_anim, asteroid2_anim, asteroid3_anim};
static const img_t bug_walk1;
static const img_t bug_walk2;
static const img_t bug_walk3;
static const img_t bug_walk4;
static const img_t *bug_walk[] = {&bug_walk1, &bug_walk2, &bug_walk3, &bug_walk4};
static const img_t bug_e1;
static const img_t bug_e2;
static const img_t bug_e3;
static const img_t bug_e4;
static const img_t *bug_explode[] = {&bug_walk1, &bug_e1, &bug_e2, &bug_e3, &bug_e4}; 
static const img_t laser_img;

// --- color palette ---
// light green: 0xff42c342
// other green: 0xff35f435
// light purple: 0xffaa8eed
// med purple: 0xff8459ea
// "black": 0xff121015

/* initialize basics  */
void init()
{
	accel_init();
	uart_init();
	gl_init(640, 480, GL_DOUBLEBUFFER);
}

/* handler function does the action we want to interrupt with */
void handle_click(unsigned int pc, void *aux_data)
{
	if (gpio_check_and_clear_event(BUTTON))
	{
		shootCount++;
		if (timer_get_ticks() - last_click > 150000) // debouncing
		{
			rb_enqueue((rb_t *)aux_data, shootCount);
		}
		last_click = timer_get_ticks();
	}
}

/* initialize interrupts */
void interrupts()
{
	gpio_init();
	interrupts_init();		// enable all inturrupts so can use gpio inturrupts
	gpio_set_input(BUTTON); // configure button
	gpio_set_pullup(BUTTON);
}


/* gets random number between 0 and 99 (we will call this in future random functions) */
unsigned int get_random(void)
{
	return rand() % 100;
}

/* gets an astroid spawn location; one of 10 locations */ 
unsigned int get_asteroid_spawn_loc(unsigned int left_border, unsigned int right_border, unsigned int asteroid_width)
{
	right_border += asteroid_width * SCALE;
	left_border += asteroid_width * SCALE;
	unsigned int interval = (right_border - left_border) / 10;
	return interval * (get_random() % 10 + 1);
}

unsigned int get_asteroid_type(unsigned int num_asteroids){
	return get_random() % num_asteroids;
}

/* returns the velocity of the rocket according to the accelerometer reading */
int get_rocket_velocity(int accel_val)
{
	int velocity = 0;
	if (a < -800)
		velocity = ultra * LEFT; // step tilt of accelerometer gives faster rocket velocity
	if (-600 >= a && a > -800)
		velocity = fast * LEFT;
	if (-300 >= a && a > -600)
		velocity = medium * LEFT;
	if (-125 >= a && a > -300) // less step tilt of accelerometer gives slower rocket velocity
		velocity = slow * LEFT;
	if (125 >= a && a > -125)
		velocity = 0; // range in which accelerometer will not move the rocket
	if (125 < a && a <= 300)
		velocity = slow * RIGHT; // same but going right instead of left (pos instead of neg)
	if (300 < a && a <= 600)
		velocity = medium * RIGHT;
	if (600 < a && a <= 800)
		velocity = fast * RIGHT;
	if (a > 800)
		velocity = ultra * RIGHT;
	return velocity;
}

/* find maximum of two ints */
int max(int a, int b)
{
	if (a >= b)
	{
		return a;
	}
	return b;
}

/* find minimum of two ints */
int min(int a, int b)
{
	if (a <= b)
	{
		return a;
	}
	return b;
}

/* checks if overlap between the ranges a-b and c-d. this function is used in collisions  */
bool overlap(unsigned int a, unsigned int b, unsigned int c, unsigned int d)
{ 
	if (max(a, c) <= min(b, d))
	{
		return true;
	}
	return false;
}

/* moves rocket using acceleromater value a */
int move_rocket(object_t rocket, unsigned int left_border, unsigned int right_border)
{
	// recalculate rocket position
	a = accel_vals();
	int velocity = get_rocket_velocity(a);
	if ((rocket.x <= left_border && velocity < 0) || (rocket.x >= right_border && velocity > 0))
		velocity = 0; // keep rocket from moving off the edge of the screen
	return velocity;
}

/* detects collision between object "object" and list of objects "objects". right now, the object is
the rocket and the list is the asteroids, but later the object will be the laser
we can also use this same code to detect collisions between bugs and any other objects */
int detect_collision(unsigned int num_objects, object_t *objects[], object_t object)
{
	for (int i = 0; i < num_objects; i++)
	{
		int x1Obj = objects[i]->collider->x; // get x values of colliders
		int x2Obj = objects[i]->collider->x + objects[i]->collider->width;
		int x1Main = object.collider->x;
		int x2Main = object.collider->x + object.collider->width;

		int y1Obj = objects[i]->collider->y; // get y values of colliders
		int y2Obj = objects[i]->collider->y + objects[i]->collider->height;
		int y1Main = object.collider->y;
		int y2Main = object.collider->y + object.collider->height;
		/*if there is an overlap betweem both the range of x values and the range of y values between
		two sprites, then the two sprites have collided*/
		bool res = overlap(x1Obj, x2Obj, x1Main, x2Main) && overlap(y1Obj, y2Obj, y1Main, y2Main);
		if (res && objects[i]->status == true)
		{
			return i;
		}
	}

	return -1;
}

void main(void)
{
	init();
	interrupts(); // init interrupts
	rb_t *rb = rb_new();
	gpio_enable_event_detection(BUTTON, GPIO_DETECT_FALLING_EDGE);
	gpio_interrupts_init();										
	gpio_interrupts_register_handler(BUTTON, handle_click, rb);
	gpio_interrupts_enable();									
	interrupts_global_enable();									

	unsigned int max_y = gl_get_height(); // highest y value that can be printed

	// implement borders so astroids only fall between valid rocket locations
	unsigned int left_border = rocket_img.width * SCALE;
	unsigned int right_border = gl_get_width() - (rocket_img.width * SCALE * 2); 

	// Set rocket at x = 300 and y = 400
	object_t rocket = {0, 0,			   // init x and y velocity
					   300, 400, true, 0, 0,  // x and y position, info on status and type
					   NULL, &rocket_img}; // collider info, image
	// set rocket collider to be a rectangle slightly smaller than rocket
	collider_t rocket_collider = { 
		rocket.x + 3 * SCALE, rocket.y + 3 * SCALE,
		(rocket.img->width - 6) * SCALE, (rocket.img->height - 6) * SCALE};
	rocket.collider = &rocket_collider;

	// spawn asteroid at one of 10 random x locations 
	int random = get_asteroid_spawn_loc(left_border, right_border, asteroid1_img.width);

	// Set asteroids and spawnrate
	object_t *asteroids[100];
	int num_asteroids = 0;
	int asteroid_spawnrate = INITIAL_SPAWNRATE;
	int cur_asteroid_spawn = 0;
	int asteroids_since_change = 0;
	int asteroid_speed = 10;

	// initialize lasers
	object_t *lasers[MAX_LASERS];
	unsigned int cur_lasers = 0;

	// initialize bugs
	object_t *bugs[100];
	unsigned int cur_bugs = 0;
	int glitch = 0;

	while(start_screen == 1){
		gl_clear(BACKGROUND_COLOR);
		gl_draw_rect(10, 15, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(78, 42, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(6, 400, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(210, 150, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(194, 50, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(179, 567, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(7, 282, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(263, 32, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(347, 330, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(198, 293, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(420, 420, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(617, 189, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(154, 345, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(506, 18, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(678, 651, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(431, 451, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(263, 87, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(626, 611, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(620, 742, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(612, 178, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(671, 34, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(361, 741, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(512, 398, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(19, 324, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_rect(580, 451, 1 * SCALE, 1 * SCALE, GL_WHITE);
		gl_draw_string(220, 210, "ROCKETBERRY PI", GL_WHITE);
		gl_draw_string(180, 260, "Press button to play", GL_WHITE);
		gl_swap_buffer();
		if(rb_dequeue(rb, &shootCount)){
			start_screen = 0;
		}
	}

	while (start_screen == 0)
	{
		// move rocket based on velocity from accelerometer 
		int velocity = move_rocket(rocket, left_border, right_border);
		rocket.velocity_x = velocity;
		rocket.x += velocity;
		rocket.collider->x += velocity;

		// bug spawning
		int bug_spawn = get_random();
		if (bug_spawn == 69 && cur_bugs < 3) {
			int random = get_asteroid_spawn_loc(left_border, right_border, asteroid1_img.width);
			object_t *bug = malloc(sizeof(object_t));
			*bug = (object_t) {0, 8,
							   random, 0, true, 0, 0,
							  NULL, &bug_walk1};
			collider_t *bug_collider = malloc(sizeof(collider_t));
			*bug_collider = (collider_t){
				bug->x + 2 * SCALE, bug->y + 2 * SCALE,
				(bug->img->width - 4) * SCALE, (bug->img->height - 4) * SCALE};
			bug->collider = bug_collider;
			bugs[cur_bugs] = bug;
			cur_bugs++;
		}

		// asteroid spawning
		if (cur_asteroid_spawn <= 0) {
			// spawn asteroid at one of 10 random x locations 
			int random = get_asteroid_spawn_loc(left_border, right_border, asteroid1_img.width);
			int type = get_asteroid_type(3); //choose from 3 asteroid types
			object_t *asteroid = malloc(sizeof(object_t));
			
			*asteroid = (object_t) {0, asteroid_speed, 
									random, 0, true, type, 0,
									NULL, asteroid_anims[type][0]};
			collider_t *asteroid_collider = malloc(sizeof(collider_t));
			*asteroid_collider = (collider_t){
				asteroid->x + 1 * SCALE, asteroid->y + 1 * SCALE,
				(asteroid->img->width - 2) * SCALE, (asteroid->img->height - 2) * SCALE};
			
			asteroid->collider = asteroid_collider;
			asteroids[num_asteroids] = asteroid;
			num_asteroids++;
			asteroids_since_change++;
			if (asteroids_since_change > 3) {
				if (asteroid_spawnrate > 4) {
					asteroid_spawnrate--;
					if (asteroid_spawnrate % 4 == 0)
						if(asteroid_speed < 20){
							asteroid_speed++;
						}
				}
				asteroids_since_change = 0;
			}
			cur_asteroid_spawn = asteroid_spawnrate + (get_random() % 10 - 5);
		}
		else
			cur_asteroid_spawn--;

		// recalculate asteroid positions
		for (int i = 0; i < num_asteroids; i++) {
			asteroids[i]->y += asteroids[i]->velocity_y;
			asteroids[i]->collider->y += asteroids[i]->velocity_y;
			if (asteroids[i]->y > max_y) {
				free(asteroids[i]);
				asteroids[i] = asteroids[num_asteroids - 1];
				num_asteroids--;
			}
		}

		// move bug
		for (int i = 0; i < cur_bugs; i++) {
			bugs[i]->y += bugs[i]->velocity_y;
			bugs[i]->collider->y += bugs[i]->velocity_y;
			bugs[i]->img = bug_walk[bugs[i]->anim_frame];
			if (bugs[i]->status == true) {
				if (bugs[i]->anim_frame < 3)
					bugs[i]->anim_frame++;
				else
					bugs[i]->anim_frame = 0;	
				if (bugs[i]->y > max_y) {
					if (game_over == 0) {
						points -= BUG_PENALTY;
					}
					glitch = 1;
					free(bugs[i]);
					bugs[i] = bugs[cur_bugs - 1];
					cur_bugs--;
				}
			}
		}
				

		// if button is clicked and game is not over, shoot a laser
		if (rb_dequeue(rb, &shootCount))
		{
			if (game_over == 0)
			{
				if (cur_lasers < MAX_LASERS) // can only have MAX_LASERS on the screen at one time to prevent laser spamming
				{
					// initialize a new laser object starting at middle of rocket
					object_t *laser = malloc(sizeof(object_t)); 
					*laser = (object_t){0, LASER_SPEED,
										rocket.x + (rocket.img->width / 2) * SCALE, rocket.y,
										true, 0, 0, NULL, &laser_img};

					// initialize a new laser collider object
					collider_t *laser_collider = malloc(sizeof(collider_t));
					*laser_collider = (collider_t){
						laser->x, laser->y,
						(laser->img->width) * SCALE, (laser->img->height) * SCALE};
					laser->collider = laser_collider;

					// the current laser has the index corresponding to its laser number in the lasers array
					lasers[cur_lasers] = laser;

					// since we added a new laser to the screen, add to current lasers
					cur_lasers += 1;
				}
			}
			else // reset these variables in order to play the game again
			{
				// reset rocket
				rocket.status = true;
				rocket.anim_frame = 0;
				rocket.img = rocket_anim[rocket.anim_frame];
				rocket.x = 300;
				rocket.y = 400;
				rocket.collider->x = rocket.x;
				rocket.collider->y = rocket.y;

				// change high score
				high_score = max(high_score, points);

				// reset asteroid locations
				for (int i = 0; i < num_asteroids; i++) {
					free(asteroids[i]);
				}
				num_asteroids = 0;
				asteroid_spawnrate = INITIAL_SPAWNRATE;
				asteroid_speed = 10;

				// reset bugs
				for (int i = 0; i < cur_bugs; i++) {
					free(bugs[i]);
				}
			    cur_bugs = 0;

				// reset points
				points = 0;

				game_over = 0;
				continue;
			}
		}

		for (int i = 0; i < cur_lasers; i++)
		{
			// move current laser along its path
			lasers[i]->y -= lasers[i]->velocity_y;
			lasers[i]->collider->y -= lasers[i]->velocity_y;

			// if a laser hits the top of the screen, put last laser in place of ended laser
			if (lasers[i]->y <= 0)
			{
				free(lasers[i]);
				lasers[i] = lasers[cur_lasers - 1];
				cur_lasers--;
			}
		}

		// if a collision is detected between an asteroid and a rocket
		if (detect_collision(num_asteroids, asteroids, rocket) != -1)
		{
			rocket.anim_frame = 1; // begin rocket animation
		}

		// iterate through the animation frames until reaching the last frame
		if (rocket.status == true && rocket.anim_frame > 0)
		{
			rocket.img = rocket_anim[rocket.anim_frame];
			rocket.anim_frame++;
			if (rocket.anim_frame == FRAMES)
			{
				rocket.status = false; // disable rocket
				game_over = 1; // trigger game over functionality
			}
		}

		
		for (int i = 0; i < cur_lasers; i++)
		{
			int collision = detect_collision(num_asteroids, asteroids, *lasers[i]); // get asteroid number that collided with laser
			if (collision != -1) // if a collision is detected between a laser and the rocket
			{
				asteroids[collision]->status = false;
				asteroids[collision]->anim_frame = 1; // begin animation
				points++;
			}
			collision = detect_collision(cur_bugs, bugs, *lasers[i]);
			if (collision != -1) {
				bugs[collision]->status = false;
				bugs[collision]->anim_frame = 1;
				points += 2;
			}
		}

		for (int i = 0; i < num_asteroids; i++)
		{
			if (asteroids[i]->anim_frame > 0) // if the asteroid collided with the laser
			{
				asteroids[i]->img = asteroid_anims[asteroids[i]->type][asteroids[i]->anim_frame]; // iterate through astroid collision frames
				asteroids[i]->anim_frame++;
				if (asteroids[i]->anim_frame == FRAMES)
				{
					free(asteroids[i]);
					asteroids[i] = asteroids[num_asteroids - 1];
					num_asteroids--;
				}
			}
		}

		for (int i = 0; i < cur_bugs; i++) {
			if (bugs[i]->status == false) {
				bugs[i]->img = bug_explode[bugs[i]->anim_frame];
				bugs[i]->anim_frame++;
				if (bugs[i]->anim_frame >= FRAMES) {
					free(bugs[i]);
					bugs[i] = bugs[cur_bugs - 1];
					cur_bugs--;
				}
			}
		}

		// draw objects to screen if status is true
		gl_clear(BACKGROUND_COLOR);
		if (rocket.status)
		{
			gl_draw_img(rocket.x, rocket.y, rocket.img, SCALE);
		}
		
		for (int i = 0; i < num_asteroids; i++) {
			gl_draw_img(asteroids[i]->x, asteroids[i]->y, asteroids[i]->img, SCALE);
		}

		for (int i = 0; i < cur_bugs; i++) {
			gl_draw_img(bugs[i]->x, bugs[i]->y, bugs[i]->img, SCALE);
		}

		for (int i = 0; i < cur_lasers; i++)
		{
			gl_draw_img(lasers[i]->x, lasers[i]->y, lasers[i]->img, SCALE);
			// gl_draw_rect(laser.collider->x, laser.collider->y, laser.collider->width,
			// laser.collider->height, GL_RED);
		}

		//glitch effect
		if (glitch == 1) {
			gl_clear(0xff35f435);
			glitch = 0;
		}

		// game over functionality
		if (game_over == 1)
		{
			gl_draw_string(250, 225, "GAME OVER", GL_WHITE);
			gl_draw_string(150, 275, "Press button to try again!", GL_WHITE);
		}

		// banner and score counters
		gl_draw_rect(0, 0, gl_get_width(), BANNER_HEIGHT, BANNER_COLOR);
		gl_draw_rect(SCALE, SCALE, gl_get_width() - 2 * SCALE, BANNER_HEIGHT - 2 * SCALE, BACKGROUND_COLOR);
		char *point_str = malloc(20);
		char *high_score_str = malloc(20);
		snprintf(point_str, 20, "POINTS: %d", points);
		snprintf(high_score_str, 20, "HIGH SCORE: %d", high_score);
		gl_draw_string(10, 10, point_str, GL_WHITE);
		gl_draw_string(425, 10, high_score_str, GL_WHITE);
		gl_swap_buffer();
	}

	uart_putchar(EOT);
}

static const img_t laser_img = {
	1,
	4,
	4,
	"\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377",
};

static const img_t rocket_img = {
	14,
	23,
	4,
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\025\020\022\377\352Y\204\377\352Y\204\377\025\020\022\377\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020"
	"\022\377\352Y\204\377\315#U\377\315#U\377\352Y\204\377\025\020\022\377\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\352Y\204"
	"\377\315#U\377\315#U\377\315#U\377\315#U\377\352Y\204\377\025\020\022\377\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\315"
	"#U\377\315#U\377\315#U\377\315#U\377\315#U\377\025\020\022\377\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\315#U\377\315#U\377\315"
	"#U\377\315#U\377\315#U\377\315#U\377\352Y\204\377\025\020\022\377\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\315#U\377\315#U\377\025\020\022"
	"\377\025\020\022\377\315#U\377\315#U\377\315#U\377\025\020\022\377\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\315#U\377\025\020\022\377B\303B"
	"\377\065\364\065\377\025\020\022\377\315#U\377\315#U\377\025\020\022\377\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\315#U\377\025\020\022\377B\303"
	"B\377\065\364\065\377\025\020\022\377\315#U\377\315#U\377\025\020\022\377\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\315#U\377\025\020\022\377B\303"
	"B\377B\303B\377\025\020\022\377\315#U\377\315#U\377\025\020\022\377\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\315#U\377\315#U\377\025\020\022"
	"\377\025\020\022\377\315#U\377\315#U\377\315#U\377\025\020\022\377\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\315#U\377\315#U\377\315#U\377"
	"\315#U\377\315#U\377\315#U\377\315#U\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\025\020\022\377\225\035@\377\315#U\377\315#U\377\315#U\377\315#"
	"U\377\315#U\377\315#U\377\315#U\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\025\020\022\377\225\035@\377\315#U\377\315#U\377\315#U\377\315#U\377"
	"\315#U\377\315#U\377\225\035@\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\025\020\022\377\225\035@\377\315#U\377\315#U\377\315#U\377\315#"
	"U\377\315#U\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\025\020\022\377\225\035@\377\225\035@\377\315#U\377\315#U\377\315#U\377\225"
	"\035@\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377"
	"\065\364\065\377\225\035@\377\225\035@\377\225\035@\377\315#U\377\315#U\377\225"
	"\035@\377\065\364\065\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377"
	"\065\364\065\377B\303B\377B\303B\377\225\035@\377\225\035@\377\225\035@\377\225"
	"\035@\377B\303B\377B\303B\377\065\364\065\377\025\020\022\377\000\000\000\000\000\000\000\000\025"
	"\020\022\377B\303B\377B\303B\377B\303B\377\025\020\022\377\225\035@\377\225\035"
	"@\377\025\020\022\377B\303B\377B\303B\377B\303B\377\025\020\022\377\000\000\000\000\025"
	"\020\022\377B\303B\377B\303B\377B\303B\377\022\212\022\377\025\020\022\377\025\020"
	"\022\377\025\020\022\377\025\020\022\377\022\212\022\377B\303B\377B\303B\377\065\364"
	"\065\377\025\020\022\377\025\020\022\377B\303B\377\022\212\022\377\022\212\022\377\025"
	"\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\022\212\022\377B\303"
	"B\377B\303B\377\025\020\022\377\025\020\022\377\022\212\022\377\025\020\022\377\025\020"
	"\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020"
	"\022\377\022\212\022\377\025\020\022\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377"
	"\000\000\000\000",
};

static const img_t asteroid1_img = {
	18,
	16,
	4,
	"\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\025\020\022\377\025\020\022"
	"\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377"
	"\334k\214\377\355\216\252\377\355\216\252\377\355\216\252\377\355\216\252"
	"\377\025\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\334"
	"k\214\377\334k\214\377\355\216\252\377\355\216\252\377\355\216\252\377\334"
	"k\214\377\277<b\377\277<b\377\355\216\252\377\025\020\022\377\025\020\022\377\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\277"
	"<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\355\216\252\377"
	"\334k\214\377\334k\214\377\355\216\252\377\355\216\252\377\355\216\252\377"
	"\025\020\022\377\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\355"
	"\216\252\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214"
	"\377\355\216\252\377\355\216\252\377\334k\214\377\334k\214\377\355\216\252"
	"\377\355\216\252\377\025\020\022\377\000\000\000\000\025\020\022\377\334k\214\377\355\216"
	"\252\377\355\216\252\377\355\216\252\377\334k\214\377\277<b\377\277<b\377"
	"\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\334k\214\377"
	"\334k\214\377\334k\214\377\025\020\022\377\000\000\000\000\025\020\022\377\334k\214\377"
	"\334k\214\377\334k\214\377\355\216\252\377\355\216\252\377\334k\214\377\334"
	"k\214\377\334k\214\377\334k\214\377\355\216\252\377\355\216\252\377\334k"
	"\214\377\277<b\377\277<b\377\334k\214\377\355\216\252\377\025\020\022\377\025"
	"\020\022\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
	"\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\355\216"
	"\252\377\355\216\252\377\334k\214\377\277<b\377\334k\214\377\355\216\252"
	"\377\025\020\022\377\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\334k\214"
	"\377\334k\214\377\334k\214\377\277<b\377\277<b\377\334k\214\377\334k\214"
	"\377\334k\214\377\334k\214\377\277<b\377\277<b\377\334k\214\377\355\216\252"
	"\377\025\020\022\377\000\000\000\000\025\020\022\377\277<b\377\334k\214\377\334k\214\377"
	"\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214"
	"\377\334k\214\377\277<b\377\334k\214\377\334k\214\377\355\216\252\377\025"
	"\020\022\377\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\334k\214\377\334k\214\377"
	"\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
	"\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\025\020\022"
	"\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\334k"
	"\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\277<b\377\334k"
	"\214\377\334k\214\377\334k\214\377\334k\214\377\025\020\022\377\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\334k\214\377\334k\214\377\334"
	"k\214\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377"
	"\277<b\377\277<b\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\025\020\022\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377"
	"\277<b\377\334k\214\377\334k\214\377\277<b\377\025\020\022\377\025\020\022\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\277"
	"<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277"
	"<b\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377\025"
	"\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000",
};

static const img_t asteroid2_img = {
  19, 16, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025"
  "\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\355\216\252\377\355\216\252\377"
  "\355\216\252\377\334k\214\377\334k\214\377\355\216\252\377\025\020\022\377\025"
  "\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\025\020\022\377\277<b\377\277<b\377\334k\214\377\355\216\252\377\355\216"
  "\252\377\355\216\252\377\334k\214\377\355\216\252\377\355\216\252\377\355"
  "\216\252\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
  "\377\025\020\022\377\355\216\252\377\355\216\252\377\355\216\252\377\277<b\377"
  "\334k\214\377\355\216\252\377\334k\214\377\334k\214\377\334k\214\377\334"
  "k\214\377\355\216\252\377\355\216\252\377\025\020\022\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\025\020\022\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214"
  "\377\355\216\252\377\355\216\252\377\355\216\252\377\334k\214\377\277<b\377"
  "\277<b\377\334k\214\377\334k\214\377\355\216\252\377\025\020\022\377\000\000\000\000"
  "\000\000\000\000\025\020\022\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377"
  "\334k\214\377\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\355"
  "\216\252\377\277<b\377\277<b\377\334k\214\377\334k\214\377\025\020\022\377\000"
  "\000\000\000\025\020\022\377\334k\214\377\334k\214\377\277<b\377\277<b\377\277<b\377"
  "\334k\214\377\355\216\252\377\355\216\252\377\277<b\377\277<b\377\334k\214"
  "\377\334k\214\377\355\216\252\377\355\216\252\377\277<b\377\334k\214\377"
  "\355\216\252\377\025\020\022\377\025\020\022\377\334k\214\377\334k\214\377\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\334k\214"
  "\377\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377"
  "\355\216\252\377\334k\214\377\334k\214\377\025\020\022\377\025\020\022\377\277"
  "<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214"
  "\377\334k\214\377\334k\214\377\334k\214\377\025\020\022\377\025\020\022\377\277"
  "<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\277<b\377\277<b\377"
  "\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377"
  "\277<b\377\334k\214\377\334k\214\377\334k\214\377\025\020\022\377\025\020\022\377"
  "\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\277"
  "<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\025\020\022\377\000\000"
  "\000\000\025\020\022\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\277<b"
  "\377\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\334k\214\377\277<b\377\025\020\022\377\000\000\000\000\000"
  "\000\000\000\025\020\022\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
  "\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\025\020\022\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\025\020\022\377\277<b\377\277<b\377\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\277<b\377"
  "\277<b\377\277<b\377\277<b\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\025\020\022\377\025\020\022\377\277<b\377\277<b\377\277<b\377\334k\214"
  "\377\334k\214\377\334k\214\377\025\020\022\377\025\020\022\377\025\020\022\377\025"
  "\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377\025\020"
  "\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000",
};

static const img_t asteroid3_img = {
  27, 24, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\025\020\022\377\025"
  "\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377"
  "\355\216\252\377\355\216\252\377\355\216\252\377\355\216\252\377\355\216"
  "\252\377\355\216\252\377\355\216\252\377\025\020\022\377\025\020\022\377\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377\355"
  "\216\252\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\355\216\252\377"
  "\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\025\020\022\377\025\020\022\377\334k\214\377\355\216\252\377\355\216"
  "\252\377\355\216\252\377\334k\214\377\334k\214\377\355\216\252\377\355\216"
  "\252\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377"
  "\334k\214\377\355\216\252\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\355\216\252\377\334k\214\377\334k\214\377\334"
  "k\214\377\355\216\252\377\355\216\252\377\355\216\252\377\355\216\252\377"
  "\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\025\020\022"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377"
  "\277<b\377\277<b\377\355\216\252\377\355\216\252\377\355\216\252\377\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
  "\334k\214\377\355\216\252\377\355\216\252\377\355\216\252\377\334k\214\377"
  "\355\216\252\377\334k\214\377\355\216\252\377\025\020\022\377\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\355\216\252\377"
  "\355\216\252\377\355\216\252\377\334k\214\377\334k\214\377\334k\214\377\277"
  "<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\355\216"
  "\252\377\355\216\252\377\355\216\252\377\355\216\252\377\355\216\252\377"
  "\334k\214\377\334k\214\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
  "\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334"
  "k\214\377\334k\214\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377"
  "\277<b\377\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\355\216"
  "\252\377\355\216\252\377\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\025\020\022\377\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\355\216"
  "\252\377\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\334"
  "k\214\377\334k\214\377\355\216\252\377\334k\214\377\334k\214\377\334k\214"
  "\377\277<b\377\334k\214\377\334k\214\377\025\020\022\377\000\000\000\000\025\020\022\377"
  "\334k\214\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214"
  "\377\334k\214\377\334k\214\377\355\216\252\377\355\216\252\377\355\216\252"
  "\377\277<b\377\277<b\377\277<b\377\334k\214\377\355\216\252\377\355\216\252"
  "\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\025"
  "\020\022\377\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\277<b\377\277<"
  "b\377\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\334k\214\377\334"
  "k\214\377\334k\214\377\355\216\252\377\277<b\377\277<b\377\277<b\377\277"
  "<b\377\277<b\377\334k\214\377\334k\214\377\025\020\022\377\025\020\022\377\334"
  "k\214\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377"
  "\334k\214\377\334k\214\377\277<b\377\277<b\377\334k\214\377\334k\214\377"
  "\277<b\377\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\355\216"
  "\252\377\355\216\252\377\277<b\377\355\216\252\377\355\216\252\377\334k\214"
  "\377\334k\214\377\025\020\022\377\025\020\022\377\334k\214\377\334k\214\377\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
  "\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\355\216"
  "\252\377\355\216\252\377\355\216\252\377\355\216\252\377\334k\214\377\025"
  "\020\022\377\025\020\022\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377"
  "\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214"
  "\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334"
  "k\214\377\334k\214\377\334k\214\377\025\020\022\377\025\020\022\377\334k\214\377"
  "\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\277<b\377"
  "\277<b\377\334k\214\377\334k\214\377\277<b\377\277<b\377\025\020\022\377\000\000"
  "\000\000\025\020\022\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377"
  "\277<b\377\277<b\377\277<b\377\334k\214\377\277<b\377\025\020\022\377\025\020"
  "\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\277<b\377\277<"
  "b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\025\020\022\377\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377"
  "\334k\214\377\334k\214\377\277<b\377\334k\214\377\334k\214\377\277<b\377"
  "\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377"
  "\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\025\020\022\377\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377"
  "\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377"
  "\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\277<b\377"
  "\277<b\377\277<b\377\277<b\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\277<b\377"
  "\277<b\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\277<b\377\277"
  "<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\025\020\022\377\277"
  "<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\025\020\022\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\277"
  "<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\277<b\377\277<b\377"
  "\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\025\020\022\377\277<b\377\277<"
  "b\377\277<b\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\025\020\022\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};


static const img_t rocket_e1 = {
	14,
	23,
	4,
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\352Y\204\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\352"
	"Y\204\377\315#U\377\315#U\377\352Y\204\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\352Y\204\377\315#U\377\315"
	"#U\377\315#U\377\315#U\377\352Y\204\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\315#U\377\315#U\377\315#U"
	"\377\315#U\377\315#U\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\025\020\022\377\315#U\377\315#U\377\315#U\377\315#U\377\315#U\377\315#"
	"U\377\315#U\377\352Y\204\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\025\020\022\377\315#U\377\315#U\377\315#U\377\025\020\022\377\025\020\022\377\315"
	"#U\377\000\000\000\000\315#U\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025"
	"\020\022\377\000\000\000\000\000\000\000\000\025\020\022\377B\303B\377\065\364\065\377\025\020\022"
	"\377\315#U\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020"
	"\022\377\315#U\377\000\000\000\000\025\020\022\377B\303B\377\000\000\000\000\025\020\022\377\315"
	"#U\377\315#U\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377"
	"\315#U\377\315#U\377\000\000\000\000B\303B\377B\303B\377\025\020\022\377\315#U\377\315"
	"#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\315#U\377"
	"\315#U\377\025\020\022\377\025\020\022\377\315#U\377\315#U\377\315#U\377\025\020"
	"\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\315#U\377"
	"\315#U\377\315#U\377\315#U\377\315#U\377\315#U\377\315#U\377\025\020\022\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\225\035@\377\315#U\377\000\000\000"
	"\000\315#U\377\315#U\377\315#U\377\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\025\020\022\377\225\035@\377\315#U\377\315#U\377\315#U\377"
	"\315#U\377\315#U\377\315#U\377\225\035@\377\025\020\022\377\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\225\035@\377\315#U\377\315#U\377\000\000\000"
	"\000\000\000\000\000\315#U\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\025\020\022\377\225\035@\377\225\035@\377\000\000\000\000\000\000\000\000\000\000\000\000\225"
	"\035@\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\065"
	"\364\065\377\000\000\000\000\225\035@\377\000\000\000\000\315#U\377\315#U\377\225\035@\377\065"
	"\364\065\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\065\364\065\377"
	"B\303B\377B\303B\377\225\035@\377\000\000\000\000\225\035@\377\225\035@\377B\303B\377"
	"B\303B\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\025\020\022\377B\303B\377B\303"
	"B\377B\303B\377\025\020\022\377\000\000\000\000\225\035@\377\025\020\022\377B\303B\377\000"
	"\000\000\000B\303B\377\025\020\022\377\000\000\000\000\000\000\000\000B\303B\377B\303B\377B\303B\377"
	"\022\212\022\377\025\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377\022\212"
	"\022\377B\303B\377B\303B\377\065\364\065\377\000\000\000\000\025\020\022\377B\303B\377"
	"\022\212\022\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000B\303B\377B\303B\377\025\020\022\377\025\020\022\377\022\212\022\377\025"
	"\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000",
};

static const img_t rocket_e2 = {
	14,
	23,
	4,
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\352Y\204\377\000\000\000\000\000\000"
	"\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\025\020\022\377\000\000\000\000\352Y\204\377\000\000\000\000\315#U\377\000\000\000\000\352Y\204\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\352Y\204\377\000\000"
	"\000\000\000\000\000\000\315#U\377\315#U\377\000\000\000\000\000\000\000\000\352Y\204\377\025\020\022\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\000\000\000\000\315#U\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\025\020\022\377\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\315#U\377\352Y\204\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000"
	"\000\000\315#U\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377B\303B\377"
	"\065\364\065\377\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000"
	"\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000"
	"\025\020\022\377\315#U\377\315#U\377\025\020\022\377\000\000\000\000\000\000\000\000\025\020\022\377"
	"\315#U\377\315#U\377\000\000\000\000\000\000\000\000B\303B\377B\303B\377\000\000\000\000\025\020\022"
	"\377\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\315#U\377"
	"\000\000\000\000\315#U\377\025\020\022\377\025\020\022\377\000\000\000\000\315#U\377\000\000\000\000\000"
	"\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\315#U\377\000\000"
	"\000\000\000\000\000\000\315#U\377\315#U\377\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\315"
	"#U\377\000\000\000\000\315#U\377\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\225\035@\377\315#U\377\000\000\000\000\315#U\377\000\000\000\000\315#U\377\000\000\000\000\000\000"
	"\000\000\315#U\377\225\035@\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
	"\377\225\035@\377\000\000\000\000\315#U\377\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\315"
	"#U\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\225\035@"
	"\377\000\000\000\000\225\035@\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\225\035@\377\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\225\035"
	"@\377\000\000\000\000\315#U\377\000\000\000\000\315#U\377\225\035@\377\065\364\065\377\025\020"
	"\022\377\000\000\000\000\025\020\022\377\000\000\000\000B\303B\377B\303B\377\000\000\000\000\225\035@"
	"\377\000\000\000\000\225\035@\377\000\000\000\000\225\035@\377B\303B\377B\303B\377\000\000\000\000"
	"\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000B\303B\377\000\000\000\000\025\020\022\377\000\000\000\000"
	"\000\000\000\000\000\000\000\000\025\020\022\377B\303B\377\000\000\000\000B\303B\377\025\020\022\377\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377B\303B\377B\303B"
	"\377\022\212\022\377\025\020\022\377\000\000\000\000\000\000\000\000\025\020\022\377\022\212\022\377"
	"B\303B\377B\303B\377\065\364\065\377\000\000\000\000\025\020\022\377B\303B\377\022\212"
	"\022\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\022\212\022\377\025\020\022\377\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\025\020\022\377",
};

static const img_t rocket_e3 = {
	14,
	23,
	4,
	"\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000"
	"\000\000\000\352Y\204\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\352Y\204\377\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\315#U\377\000\000\000\000\315"
	"#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\352Y\204\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315"
	"#U\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\315#"
	"U\377\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\315#U\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\315"
	"#U\377\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
	"\377\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\315#U\377"
	"\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025"
	"\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\315#U\377\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\225\035@\377\315#U\377\000\000\000\000\315"
	"#U\377\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\225"
	"\035@\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\225\035@\377\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\225\035@\377\065\364\065"
	"\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"B\303B\377\000\000\000\000\225\035@\377\000\000\000\000\000\000\000\000\225\035@\377\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\025\020"
	"\022\377\000\000\000\000\225\035@\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000"
	"\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303"
	"B\377\000\000\000\000\022\212\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\025\020\022\377\022\212\022\377B\303B\377\000\000\000\000\065\364\065\377\000\000\000\000\022\212"
	"\022\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377",
};

static const img_t rocket_e4 = {
	14,
	23,
	4,
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\352Y\204\377\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\225\035@\377\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\225\035@\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377"
	"\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\225\035@\377\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};

static const img_t asteroid1_e1 = {
	18,
	16,
	4,
	"\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\000\000\000\000\025\020\022\377\025"
	"\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377"
	"\355\216\252\377\355\216\252\377\355\216\252\377\355\216\252\377\000\000\000\000"
	"\025\020\022\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\000\000\000\000\334k\214\377\334k\214\377"
	"\355\216\252\377\355\216\252\377\000\000\000\000\334k\214\377\277<b\377\277<b\377"
	"\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\025"
	"\020\022\377\334k\214\377\334k\214\377\025\020\022\377\025\020\022\377\334k\214\377"
	"\334k\214\377\334k\214\377\355\216\252\377\334k\214\377\334k\214\377\355"
	"\216\252\377\000\000\000\000\355\216\252\377\025\020\022\377\000\000\000\000\334k\214\377\000"
	"\000\000\000\334k\214\377\334k\214\377\355\216\252\377\334k\214\377\000\000\000\000\277"
	"<b\377\277<b\377\277<b\377\334k\214\377\000\000\000\000\355\216\252\377\334k\214"
	"\377\000\000\000\000\355\216\252\377\355\216\252\377\025\020\022\377\000\000\000\000\000\000\000\000"
	"\334k\214\377\355\216\252\377\355\216\252\377\000\000\000\000\000\000\000\000\277<b\377\277"
	"<b\377\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334"
	"k\214\377\000\000\000\000\025\020\022\377\000\000\000\000\025\020\022\377\334k\214\377\334k\214"
	"\377\000\000\000\000\000\000\000\000\355\216\252\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000"
	"\355\216\252\377\355\216\252\377\000\000\000\000\277<b\377\277<b\377\000\000\000\000\000\000"
	"\000\000\025\020\022\377\025\020\022\377\277<b\377\334k\214\377\334k\214\377\334k\214"
	"\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\355\216\252"
	"\377\355\216\252\377\334k\214\377\277<b\377\334k\214\377\355\216\252\377"
	"\025\020\022\377\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\334k\214\377"
	"\334k\214\377\334k\214\377\277<b\377\277<b\377\334k\214\377\334k\214\377"
	"\334k\214\377\334k\214\377\277<b\377\277<b\377\000\000\000\000\000\000\000\000\025\020\022\377"
	"\000\000\000\000\025\020\022\377\277<b\377\334k\214\377\000\000\000\000\277<b\377\277<b\377"
	"\277<b\377\277<b\377\334k\214\377\000\000\000\000\334k\214\377\025\020\022\377\000\000\000"
	"\000\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\277<b\377\334k\214\377\000\000\000\000\000"
	"\000\000\000\000\000\000\000\334k\214\377\025\020\022\377\334k\214\377\334k\214\377\334k\214"
	"\377\000\000\000\000\334k\214\377\000\000\000\000\355\216\252\377\025\020\022\377\334k\214\377"
	"\334k\214\377\025\020\022\377\334k\214\377\334k\214\377\334k\214\377\277<b\377"
	"\277<b\377\334k\214\377\334k\214\377\000\000\000\000\334k\214\377\025\020\022\377\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\334k\214\377\000\000\000"
	"\000\334k\214\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\334"
	"k\214\377\000\000\000\000\277<b\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
	"\277<b\377\277<b\377\334k\214\377\334k\214\377\277<b\377\000\000\000\000\025\020\022"
	"\377\334k\214\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\277<b\377\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\277<b"
	"\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\000\000\000\000\025\020\022\377\025\020\022"
	"\377\025\020\022\377\000\000\000\000\025\020\022\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000"
	"\000\000\000\000\000",
};

static const img_t asteroid1_e2 = {
	18,
	16,
	4,
	"\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\000\000\000\000\025\020\022\377\025"
	"\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000"
	"\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334k\214"
	"\377\355\216\252\377\355\216\252\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\025\020\022\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
	"\377\025\020\022\377\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377\355\216\252\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000"
	"\000\000\000\000\025\020\022\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020"
	"\022\377\025\020\022\377\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\025\020\022\377\000\000\000\000\334k\214\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\277<b\377\000\000\000\000\000"
	"\000\000\000\334k\214\377\000\000\000\000\355\216\252\377\334k\214\377\000\000\000\000\355\216"
	"\252\377\355\216\252\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\334k\214\377\334k\214\377\000\000\000\000"
	"\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377\000\000\000\000\025\020\022\377\000\000\000\000\025"
	"\020\022\377\277<b\377\334k\214\377\000\000\000\000\000\000\000\000\355\216\252\377\334k\214"
	"\377\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\334k\214\377\355\216\252\377\025\020\022\377\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214"
	"\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\334k\214\377\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\277"
	"<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000"
	"\000\277<b\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214"
	"\377\025\020\022\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\355\216\252\377\000\000"
	"\000\000\025\020\022\377\277<b\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000"
	"\000\000\000\025\020\022\377\025\020\022\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000"
	"\000\000\334k\214\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020"
	"\022\377\334k\214\377\025\020\022\377\000\000\000\000\025\020\022\377\334k\214\377\000\000\000"
	"\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\025\020"
	"\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000",
};

static const img_t asteroid1_e3 = {
	18,
	16,
	4,
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020"
	"\022\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\277<b\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\334k\214\377"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000"
	"\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
	"\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000",
};

static const img_t asteroid1_e4 = {
	18,
	16,
	4,
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277"
	"<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025"
	"\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000"
	"\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};

static const img_t asteroid2_e1 = {
  19, 16, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025"
  "\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000"
  "\000\000\000\025\020\022\377\334k\214\377\355\216\252\377\000\000\000\000\000\000\000\000\334k\214"
  "\377\334k\214\377\355\216\252\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\277"
  "<b\377\334k\214\377\355\216\252\377\355\216\252\377\355\216\252\377\334k"
  "\214\377\355\216\252\377\355\216\252\377\355\216\252\377\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\355\216\252\377\355"
  "\216\252\377\355\216\252\377\277<b\377\000\000\000\000\355\216\252\377\334k\214\377"
  "\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\355\216\252\377\025"
  "\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\277<b\377\334k"
  "\214\377\000\000\000\000\334k\214\377\355\216\252\377\355\216\252\377\000\000\000\000\334"
  "k\214\377\277<b\377\277<b\377\000\000\000\000\334k\214\377\355\216\252\377\000\000\000"
  "\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\277<b\377\277<b\377\277<b\377\277"
  "<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\355\216\252\377"
  "\000\000\000\000\277<b\377\277<b\377\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\025"
  "\020\022\377\334k\214\377\000\000\000\000\000\000\000\000\277<b\377\277<b\377\334k\214\377"
  "\000\000\000\000\355\216\252\377\277<b\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\277<b\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334k\214"
  "\377\000\000\000\000\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\334k"
  "\214\377\277<b\377\277<b\377\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000\355"
  "\216\252\377\334k\214\377\334k\214\377\025\020\022\377\025\020\022\377\277<b\377"
  "\277<b\377\277<b\377\277<b\377\334k\214\377\000\000\000\000\334k\214\377\334k\214"
  "\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\334"
  "k\214\377\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\277<b\377\277<b\377\277"
  "<b\377\334k\214\377\334k\214\377\277<b\377\277<b\377\334k\214\377\334k\214"
  "\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\277<b\377\000\000\000\000\334"
  "k\214\377\334k\214\377\025\020\022\377\025\020\022\377\000\000\000\000\277<b\377\334k\214"
  "\377\334k\214\377\000\000\000\000\277<b\377\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
  "\334k\214\377\025\020\022\377\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377"
  "\334k\214\377\277<b\377\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000\334k\214"
  "\377\334k\214\377\000\000\000\000\334k\214\377\334k\214\377\334k\214\377\000\000\000\000"
  "\025\020\022\377\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\334k\214\377\334"
  "k\214\377\000\000\000\000\334k\214\377\000\000\000\000\334k\214\377\334k\214\377\334k\214"
  "\377\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\025\020\022\377\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\334k\214\377\334k\214\377\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\334k\214\377\000\000"
  "\000\000\025\020\022\377\025\020\022\377\000\000\000\000\277<b\377\277<b\377\334k\214\377\334"
  "k\214\377\334k\214\377\025\020\022\377\025\020\022\377\000\000\000\000\025\020\022\377\025"
  "\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\025\020\022\377\025\020\022\377\025\020\022\377\000\000\000\000\025\020\022\377\025\020\022\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};

static const img_t asteroid2_e2 = {
  19, 16, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000"
  "\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\355"
  "\216\252\377\355\216\252\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\355\216\252\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000"
  "\000\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\355\216\252\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
  "\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\355\216\252\377\000\000\000\000\277<b\377\334k\214\377\355\216\252\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\277<b\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000"
  "\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377"
  "\000\000\000\000\334k\214\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\277<b\377\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\334k\214\377\000\000\000"
  "\000\025\020\022\377\025\020\022\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334"
  "k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\277"
  "<b\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377"
  "\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\000\000\000\000\277<b\377\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000"
  "\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377"
  "\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\025\020\022\377\025\020\022\377\000\000\000"
  "\000\000\000\000\000\277<b\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\334k\214\377\025\020\022\377\025\020\022\377\000\000\000\000\025\020\022\377"
  "\025\020\022\377\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000",
};

static const img_t asteroid2_e3 = {
  19, 16, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\334k\214\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334"
  "k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\277<b\377\000\000\000\000"
  "\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\277<b\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000"
  "\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000",
};

static const img_t asteroid2_e4 = {
  19, 16, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000",
};

static const img_t asteroid3_e1 = {
  27, 24, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377"
  "\000\000\000\000\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\355\216\252"
  "\377\000\000\000\000\355\216\252\377\355\216\252\377\355\216\252\377\355\216\252"
  "\377\355\216\252\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\277<"
  "b\377\025\020\022\377\025\020\022\377\025\020\022\377\025\020\022\377\000\000\000\000\334k\214"
  "\377\334k\214\377\000\000\000\000\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\334k\214\377\000\000\000\000\355\216\252\377\025\020\022\377\334k\214\377\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\025\020\022\377"
  "\000\000\000\000\334k\214\377\355\216\252\377\000\000\000\000\355\216\252\377\334k\214\377"
  "\334k\214\377\355\216\252\377\000\000\000\000\277<b\377\277<b\377\277<b\377\277<"
  "b\377\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\000\000\000\000\000\000"
  "\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\277"
  "<b\377\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377\355\216\252\377\334k\214"
  "\377\334k\214\377\334k\214\377\355\216\252\377\355\216\252\377\355\216\252"
  "\377\355\216\252\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
  "\334k\214\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\025\020\022\377\277<b\377\277<b\377\277<b\377\000\000\000\000\355\216\252\377\355"
  "\216\252\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\334k\214\377\334k\214\377\355\216\252\377\355\216\252\377\355\216\252"
  "\377\334k\214\377\355\216\252\377\334k\214\377\355\216\252\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\334k\214\377\355\216"
  "\252\377\355\216\252\377\355\216\252\377\000\000\000\000\334k\214\377\334k\214\377"
  "\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\000\000\000\000\355\216"
  "\252\377\355\216\252\377\355\216\252\377\355\216\252\377\355\216\252\377"
  "\334k\214\377\334k\214\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
  "\377\334k\214\377\000\000\000\000\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\000\000\000\000\000\000\000\000\277<b\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214"
  "\377\334k\214\377\334k\214\377\355\216\252\377\355\216\252\377\355\216\252"
  "\377\334k\214\377\334k\214\377\334k\214\377\000\000\000\000\025\020\022\377\000\000\000\000"
  "\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\277<b\377\277<b\377"
  "\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\355\216"
  "\252\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\334k\214\377\334"
  "k\214\377\025\020\022\377\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\277"
  "<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377\334k\214\377"
  "\355\216\252\377\355\216\252\377\355\216\252\377\277<b\377\277<b\377\277"
  "<b\377\334k\214\377\355\216\252\377\355\216\252\377\334k\214\377\277<b\377"
  "\277<b\377\277<b\377\277<b\377\334k\214\377\025\020\022\377\000\000\000\000\000\000\000\000"
  "\334k\214\377\334k\214\377\277<b\377\277<b\377\000\000\000\000\277<b\377\277<b\377"
  "\334k\214\377\334k\214\377\000\000\000\000\334k\214\377\334k\214\377\334k\214\377"
  "\355\216\252\377\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\277"
  "<b\377\277<b\377\277<b\377\277<b\377\277<b\377\334k\214\377\334k\214\377"
  "\025\020\022\377\025\020\022\377\334k\214\377\334k\214\377\277<b\377\277<b\377"
  "\000\000\000\000\277<b\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\277<"
  "b\377\334k\214\377\334k\214\377\277<b\377\000\000\000\000\000\000\000\000\334k\214\377\355"
  "\216\252\377\355\216\252\377\355\216\252\377\277<b\377\355\216\252\377\355"
  "\216\252\377\334k\214\377\334k\214\377\025\020\022\377\025\020\022\377\334k\214"
  "\377\334k\214\377\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\334k\214\377"
  "\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377"
  "\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377\334k\214\377\355\216\252\377\355"
  "\216\252\377\000\000\000\000\355\216\252\377\355\216\252\377\000\000\000\000\025\020\022\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377\355\216\252\377\000\000\000"
  "\000\334k\214\377\334k\214\377\334k\214\377\277<b\377\277<b\377\277<b\377\277"
  "<b\377\277<b\377\000\000\000\000\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334"
  "k\214\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\277<b\377\277<b\377\334"
  "k\214\377\000\000\000\000\334k\214\377\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000"
  "\334k\214\377\334k\214\377\000\000\000\000\334k\214\377\334k\214\377\000\000\000\000\277"
  "<b\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\277<b\377\000\000\000\000\025\020\022\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\277<b\377\277<b\377\277<b\377\334k\214"
  "\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334"
  "k\214\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377\277"
  "<b\377\277<b\377\277<b\377\334k\214\377\277<b\377\025\020\022\377\025\020\022\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\277<b\377\277<b\377\277<b\377"
  "\277<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377"
  "\277<b\377\334k\214\377\334k\214\377\334k\214\377\334k\214\377\277<b\377"
  "\277<b\377\277<b\377\277<b\377\334k\214\377\025\020\022\377\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\334k\214\377\334k\214\377\334"
  "k\214\377\277<b\377\334k\214\377\000\000\000\000\277<b\377\277<b\377\277<b\377\000"
  "\000\000\000\277<b\377\277<b\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\277"
  "<b\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000"
  "\000\000\000\000\000\000\025\020\022\377\277<b\377\334k\214\377\000\000\000\000\334k\214\377\334"
  "k\214\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\277<b\377"
  "\277<b\377\334k\214\377\277<b\377\277<b\377\277<b\377\277<b\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025"
  "\020\022\377\000\000\000\000\000\000\000\000\277<b\377\334k\214\377\334k\214\377\334k\214\377"
  "\277<b\377\277<b\377\277<b\377\277<b\377\000\000\000\000\334k\214\377\334k\214\377"
  "\334k\214\377\334k\214\377\025\020\022\377\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\025\020\022\377"
  "\025\020\022\377\000\000\000\000\277<b\377\334k\214\377\334k\214\377\334k\214\377\334"
  "k\214\377\000\000\000\000\334k\214\377\334k\214\377\334k\214\377\334k\214\377\334"
  "k\214\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025"
  "\020\022\377\277<b\377\277<b\377\277<b\377\000\000\000\000\000\000\000\000\334k\214\377\277"
  "<b\377\277<b\377\000\000\000\000\025\020\022\377\355\216\252\377\000\000\000\000\000\000\000\000\277"
  "<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\277<b\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000",
};

static const img_t asteroid3_e2 = {
  27, 24, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\025\020\022\377\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\355"
  "\216\252\377\355\216\252\377\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000"
  "\000\000\000\000\277<b\377\025\020\022\377\025\020\022\377\000\000\000\000\025\020\022\377\000\000\000\000"
  "\000\000\000\000\334k\214\377\000\000\000\000\334k\214\377\000\000\000\000\334k\214\377\000\000\000\000\334"
  "k\214\377\334k\214\377\334k\214\377\000\000\000\000\355\216\252\377\025\020\022\377"
  "\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\025\020\022"
  "\377\000\000\000\000\334k\214\377\355\216\252\377\000\000\000\000\355\216\252\377\334k\214"
  "\377\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334"
  "k\214\377\277<b\377\334k\214\377\334k\214\377\000\000\000\000\355\216\252\377\000\000"
  "\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000"
  "\000\000\000\000\000\334k\214\377\334k\214\377\355\216\252\377\334k\214\377\000\000\000\000"
  "\334k\214\377\355\216\252\377\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000"
  "\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\277<b\377\277<b\377\000\000\000\000\355\216"
  "\252\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\334k\214\377\334k\214"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\355\216\252\377\334"
  "k\214\377\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
  "\377\000\000\000\000\334k\214\377\355\216\252\377\000\000\000\000\355\216\252\377\000\000\000\000"
  "\334k\214\377\000\000\000\000\000\000\000\000\277<b\377\277<b\377\334k\214\377\000\000\000\000\000"
  "\000\000\000\334k\214\377\000\000\000\000\355\216\252\377\000\000\000\000\355\216\252\377\000\000\000"
  "\000\355\216\252\377\000\000\000\000\334k\214\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\334k\214\377\334k\214\377\334k\214\377\334k\214\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\334"
  "k\214\377\334k\214\377\000\000\000\000\355\216\252\377\355\216\252\377\000\000\000\000\334"
  "k\214\377\000\000\000\000\334k\214\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000"
  "\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377"
  "\000\000\000\000\277<b\377\277<b\377\000\000\000\000\000\000\000\000\277<b\377\334k\214\377\334k"
  "\214\377\355\216\252\377\334k\214\377\334k\214\377\334k\214\377\000\000\000\000\334"
  "k\214\377\334k\214\377\000\000\000\000\000\000\000\000\334k\214\377\277<b\377\277<b\377\277"
  "<b\377\000\000\000\000\334k\214\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\355\216"
  "\252\377\277<b\377\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\334k\214\377\277<b\377\000\000\000\000\277<b\377\000\000\000\000\334k\214\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334"
  "k\214\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334"
  "k\214\377\334k\214\377\000\000\000\000\355\216\252\377\277<b\377\000\000\000\000\277<b\377"
  "\000\000\000\000\277<b\377\334k\214\377\000\000\000\000\334k\214\377\334k\214\377\277<b\377"
  "\277<b\377\000\000\000\000\277<b\377\334k\214\377\334k\214\377\000\000\000\000\277<b\377"
  "\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\355\216\252\377\000\000\000\000\355\216\252\377\277<b\377\000\000\000\000\000\000\000\000"
  "\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334"
  "k\214\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377"
  "\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\277<b\377\277"
  "<b\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\355\216"
  "\252\377\355\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377"
  "\355\216\252\377\000\000\000\000\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\277<b"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377\334k\214\377\334k\214"
  "\377\334k\214\377\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\334k\214\377\000\000\000\000"
  "\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000"
  "\000\334k\214\377\000\000\000\000\334k\214\377\334k\214\377\000\000\000\000\277<b\377\000\000\000"
  "\000\000\000\000\000\000\000\000\000\334k\214\377\277<b\377\000\000\000\000\025\020\022\377\000\000\000\000\277"
  "<b\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000"
  "\334k\214\377\000\000\000\000\334k\214\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\277<b\377\277<b\377\000\000\000\000\277<b\377"
  "\000\000\000\000\025\020\022\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\277<b\377\000\000"
  "\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\277<b\377\000\000"
  "\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\277<b\377\277<b\377"
  "\000\000\000\000\277<b\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025"
  "\020\022\377\334k\214\377\334k\214\377\000\000\000\000\277<b\377\334k\214\377\000\000\000"
  "\000\277<b\377\277<b\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\025\020\022\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\334k\214\377\000"
  "\000\000\000\334k\214\377\000\000\000\000\334k\214\377\277<b\377\277<b\377\277<b\377\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\277<b\377\277<b"
  "\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\277<b\377\334k\214\377\000\000\000\000\334k\214"
  "\377\000\000\000\000\277<b\377\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\334k\214\377\334k\214\377\025\020\022\377\000\000\000\000\000\000\000\000\277<b\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\025\020\022\377\025\020"
  "\022\377\000\000\000\000\277<b\377\000\000\000\000\334k\214\377\334k\214\377\334k\214\377"
  "\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\334k\214\377\000"
  "\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\277"
  "<b\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334k\214\377\000\000\000\000\277<b\377"
  "\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000"
  "\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};

static const img_t asteroid3_e3 = {
  27, 24, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\025\020\022\377\000\000\000\000\000\000\000\000\025"
  "\020\022\377\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020"
  "\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000"
  "\000\000\277<b\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\355\216\252\377\000\000\000\000"
  "\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\334"
  "k\214\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\334k"
  "\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\334k\214"
  "\377\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000"
  "\000\277<b\377\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\334k\214\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000"
  "\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\277<b\377\277<b\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334"
  "k\214\377\334k\214\377\000\000\000\000\000\000\000\000\355\216\252\377\000\000\000\000\000\000\000\000\355"
  "\216\252\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\334k"
  "\214\377\000\000\000\000\334k\214\377\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000"
  "\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\277<b\377\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\334k\214\377\000\000\000\000\025\020\022\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334"
  "k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\025\020\022\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000",
};

static const img_t asteroid3_e4 = {
  27, 24, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214"
  "\377\000\000\000\000\334k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334"
  "k\214\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\334k\214\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\277<b\377\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\277<b\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};

static const img_t bug_walk1 = {
  19, 11, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\065\364\065\377\000\000\000\000\000\000\000\000\000\000"
  "\000\000\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\065\364\065\377\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\065\364\065\377\065\364\065\377\065\364"
  "\065\377\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065"
  "\377\065\364\065\377B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\025\020\022\377\025\020\022\377\065\364"
  "\065\377\065\364\065\377\065\364\065\377\025\020\022\377\025\020\022\377\065\364\065\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000B\303B\377B\303B\377B\303B\377\025\020\022\377B\303B\377\025\020\022\377"
  "B\303B\377B\303B\377\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377B\303B\377B\303B\377\352Y\204\377\315"
  "#U\377B\303B\377B\303B\377B\303B\377\315#U\377\352Y\204\377B\303B\377B\303"
  "B\377B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000"
  "\000\000\000\000\000\000B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377"
  "B\303B\377B\303B\377B\303B\377\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000B\303"
  "B\377B\303B\377B\303B\377B\303B\377B\303B\377\000\000\000\000\000\000\000\000B\303B\377\000"
  "\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303"
  "B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000",
};

static const img_t bug_walk2 = {
  19, 11, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\065\364\065\377\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364"
  "\065\377\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303"
  "B\377\000\000\000\000\000\000\000\000\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065"
  "\377\065\364\065\377\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\065\364\065\377"
  "\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377\065"
  "\364\065\377\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\065\364\065\377B\303B\377\000\000\000\000\065\364\065\377\025\020\022\377\025\020"
  "\022\377\065\364\065\377\065\364\065\377\065\364\065\377\025\020\022\377\025\020\022\377"
  "\065\364\065\377B\303B\377B\303B\377\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\065\364\065\377\000\000\000\000\000\000\000\000B\303B\377B\303B\377B\303B\377B\303B\377\025"
  "\020\022\377B\303B\377\025\020\022\377B\303B\377B\303B\377\065\364\065\377\000\000\000"
  "\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000B\303B\377\352Y\204\377\315#U\377B\303B\377B\303B\377B\303B\377\315"
  "#U\377\352Y\204\377B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377B\303B\377B\303B\377B\303B\377B\303"
  "B\377B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000"
  "\000\000\000\000\000B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065"
  "\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};

static const img_t bug_walk3 = {
  19, 11, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "B\303B\377\000\000\000\000\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377"
  "\065\364\065\377\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\065\364\065\377B\303B\377\000\000\000\000\065\364\065\377\065\364\065"
  "\377\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377"
  "\065\364\065\377\065\364\065\377\000\000\000\000B\303B\377\065\364\065\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000B\303B\377\065\364\065\377\025\020"
  "\022\377\025\020\022\377\065\364\065\377\065\364\065\377\065\364\065\377\025\020\022\377"
  "\025\020\022\377\065\364\065\377B\303B\377\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377B\303B\377B\303B\377"
  "\025\020\022\377B\303B\377\025\020\022\377B\303B\377B\303B\377\065\364\065\377\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000B\303B\377\352Y\204\377\315#U\377B\303B\377B\303B\377B\303B\377\315"
  "#U\377\352Y\204\377B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377B\303"
  "B\377B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};

static const img_t bug_walk4 = {
  19, 11, 4,
  "\000\000\000\000\000\000\000\000\065\364\065\377\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377"
  "\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377"
  "\000\000\000\000\000\000\000\000\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377"
  "\065\364\065\377\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\065\364\065\377\065\364"
  "\065\377\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065"
  "\377\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\065\364\065\377B\303B\377B\303B\377\065\364\065\377\025\020\022\377\025\020\022"
  "\377\065\364\065\377\065\364\065\377\065\364\065\377\025\020\022\377\025\020\022\377"
  "\065\364\065\377B\303B\377B\303B\377\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377B\303B\377B\303B\377\025\020"
  "\022\377B\303B\377\025\020\022\377B\303B\377B\303B\377\065\364\065\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000B\303B\377\352Y\204\377\315#U\377B\303B\377B\303B\377B\303B\377\315"
  "#U\377\352Y\204\377B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377B\303B\377B\303B\377B\303B"
  "\377B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000"
  "\000\000\000\000B\303B\377B\303B\377B\303B\377B\303B\377B\303B\377\000\000\000\000\000\000\000"
  "\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364"
  "\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000",
};

static const img_t bug_e1 = {
  19, 11, 4,
  "\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364"
  "\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000"
  "\000\000\000\000\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065\377\065\364\065"
  "\377\000\000\000\000\065\364\065\377B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\065\364\065\377\025\020\022"
  "\377\065\364\065\377\065\364\065\377\377\377\377\377\377\377\377\377\025\020\022"
  "\377\065\364\065\377\000\000\000\000\352Y\204\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\065\364\065\377\377\377\377\377\377\377\377\377\377\377\377\377\377"
  "\377\377\377\025\020\022\377\065\364\065\377\065\364\065\377\377\377\377\377\377"
  "\377\377\377\025\020\022\377\352Y\204\377\352Y\204\377\352Y\204\377\065\364\065"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\377\377\377\377\377\377"
  "\377\377\377\377\377\377\377\377\377\377\315#U\377\315#U\377B\303B\377\025"
  "\020\022\377\343\377\000\377\343\377\000\377\352Y\204\377\352Y\204\377\352Y\204"
  "\377\343\377\000\377\065\364\065\377\000\000\000\000\035\003\371\377\035\003\371\377\035\003\371"
  "\377\035\003\371\377\035\003\371\377B\303B\377\352Y\204\377\315#U\377\315#U\377"
  "B\303B\377B\303B\377\315#U\377\352Y\204\377\352Y\204\377\352Y\204\377\352"
  "Y\204\377\000\000\000\000\000\000\000\000\000\000\000\000\035\003\371\377\035\003\371\377\035\003\371\377"
  "\035\003\371\377\035\003\371\377\000\000\000\000B\303B\377\315#U\377\315#U\377B\303B\377"
  "B\303B\377B\303B\377\377\377\377\377\377\377\377\377\377\377\377\377\377"
  "\377\377\377\377\377\377\377\377\377\377\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\315#U\377\315#U\377B\303B\377B\303"
  "B\377B\303B\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377"
  "\377\377\377\377\377\377\377\377\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000",
};

static const img_t bug_e2 = {
  19, 11, 4,
  "\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000"
  "\000\000\000\000\000\000\065\364\065\377\377\377\377\377\377\377\377\377\377\377\377\377"
  "\377\377\377\377\025\020\022\377\000\000\000\000\065\364\065\377B\303B\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\377\377\377\377\377\377\377\377\377\377\377\377\377"
  "\377\377\377\065\364\065\377\000\000\000\000\315#U\377\377\377\377\377\377\377\377"
  "\377\377\377\377\377\377\377\377\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\352Y\204\377\000\000\000\000\000\000\000\000\000\000\000\000\377\377\377\377\377\377\377\377\377"
  "\377\377\377\377\377\377\377\000\000\000\000\000\000\000\000\315#U\377\065\364\065\377\000\000"
  "\000\000\343\377\000\377\343\377\000\377\025\020\022\377\343\377\000\377\343\377\000\377"
  "\343\377\000\377\343\377\000\377\065\364\065\377\000\000\000\000\000\000\000\000\065\364\065\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\343"
  "\377\000\377\343\377\000\377\025\020\022\377\343\377\000\377\343\377\000\377\343\377"
  "\000\377\343\377\000\377\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\352Y\204\377\352Y\204\377\352Y\204\377\352Y\204\377\352Y\204\377\352Y"
  "\204\377B\303B\377\315#U\377\025\020\022\377\000\000\000\000\352Y\204\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\035\003\371\377\035\003\371\377"
  "\035\003\371\377\035\003\371\377\315#U\377B\303B\377B\303B\377\000\000\000\000B\303B\377"
  "\025\020\022\377\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\035\003\371\377\035\003\371\377\035\003\371\377\035\003\371\377\315#U"
  "\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377B\303B\377"
  "\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000B\303B\377B\303B\377\377\377\377\377\377"
  "\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\377\377\377\377\377\377\377\377\377\377\377"
  "\377\377\377\377\377\377\377\377\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};

static const img_t bug_e3 = {
  19, 11, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\065\364"
  "\065\377\000\000\000\000\065\364\065\377\000\000\000\000\025\020\022\377\025\020\022\377\025\020\022"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\065\364"
  "\065\377\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022"
  "\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\377\377\377\377\377\377\377\377\377\377\377\377\377\377"
  "\377\377\377\377\377\377\000\000\000\000\000\000\000\000\000\000\000\000\035\003\371\377\035\003\371\377"
  "\035\003\371\377\035\003\371\377\035\003\371\377\035\003\371\377\065\364\065\377\065\364"
  "\065\377\000\000\000\000\000\000\000\000\000\000\000\000\377\377\377\377\377\377\377\377\377\377\377"
  "\377\377\377\377\377\377\377\377\377\000\000\000\000\000\000\000\000B\303B\377\035\003\371\377"
  "\035\003\371\377\035\003\371\377\035\003\371\377\035\003\371\377\035\003\371\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\352Y\204\377\352Y\204\377\000\000\000\000\352"
  "Y\204\377\343\377\000\377\343\377\000\377\343\377\000\377\025\020\022\377\025\020\022"
  "\377\352Y\204\377\377\377\377\377\377\377\377\377\377\377\377\377\377\377"
  "\377\377\000\000\000\000\000\000\000\000\000\000\000\000\352Y\204\377\352Y\204\377\352Y\204\377\352"
  "Y\204\377\343\377\000\377\343\377\000\377\343\377\000\377\343\377\000\377\343\377"
  "\000\377\025\020\022\377\025\020\022\377\000\000\000\000\377\377\377\377\377\377\377\377"
  "\377\377\377\377\377\377\377\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\343\377\000\377\343\377\000\377\343\377\000\377\343\377\000\377\343\377"
  "\000\377\343\377\000\377\025\020\022\377\025\020\022\377\000\000\000\000\025\020\022\377\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000"
  "\025\020\022\377\025\020\022\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\000\000\000\000\000\000\000\000"
  "\315#U\377\000\000\000\000\000\000\000\000\025\020\022\377\025\020\022\377\025\020\022\377\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};

static const img_t bug_e4 = {
  19, 11, 4,
  "\000\000\000\000\000\000\000\000\000\000\000\000\065\364\065\377\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\377\377\377\377\377\377\377\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\343\377\000\377\343\377\000\377\343\377\000\377\343\377\000\377\343\377\000"
  "\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\352Y\204\377\352Y\204\377\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\343\377\000\377\343"
  "\377\000\377\343\377\000\377\343\377\000\377\343\377\000\377\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\035\003\371\377\035\003\371\377\000\000\000\000\000"
  "\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000B\303B\377\000\000"
  "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\315#U\377\000\000\000\000\000\000\000\000\000\000\000\000B\303"
  "B\377\025\020\022\377\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000",
};

