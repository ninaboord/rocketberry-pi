#include "timer.h"
#include "uart.h"
#include "i2c.h"
#include "accel.h"
#include "printf.h"

#include "LSM6DS33.h"

void accel_init(void) {

    timer_init();
	uart_init();

    i2c_init();
	lsm6ds33_init();
    //printf("lsm");

    //printf("whoami=%x\n", lsm6ds33_get_whoami());

}

short accel_vals(void){
    short x, y, z;
    lsm6ds33_read_accelerometer(&x, &y, &z);
    // 16384 is 1g (1g == 1000mg)
    //timer_delay_ms(150);
    //printf("accel=(%dmg,%dmg,%dmg)\n", x/16, y/16, z/16);
    return x/16;
}
