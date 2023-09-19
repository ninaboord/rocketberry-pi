# CS107e template
=======
## Project title
Rocketberry Pi

## Team members
Nina Boord and Ryan Dukes

## Project description
We built a video game called Rocketberry Pi. In the game, you use an accelerometer to tilt your rocket and a button to shoot lasers
from your rocket. In order to gain points, you shoot asteroids and "bugs". You get one point for shooting an asteroid. If you get hit by 
an asteroid, your ship explodes and the game is over. Bugs cannot kill you, but if you don't shoot one and it hits the bottom of the screen,
ten points are deducted. (So if you don't squash those bugs, you lose points!) If you shoot a bug, you gain two points. The purpose of the
bugs is to add complexity and prevent the user from "playing it too safe". As time goes on, the game increases in difficulty.

Cool features of our game:
The rocket speeds up when you tilt your accelerometer
High score and infinite restarts
Collision animations
Asteroids spawn randomly
Three different types of asteroid types/sizes, randomly distributed
"Bugs" move as they fall down the screen
Rocketship "explodes" when hit by an asteroid
The game increases in difficulty as you keep playing: asteroids spawn more often and move faster
A maximum of three lasers are allowed on the screen at one time (prevents spamming of lasers)
Bug "glitch" animation when the bug hits the bottom of the screen (even after the game over screen!)


## Member contribution
Ryan:
Contributed the graphics library (gl.c)
Astroid and bug movement
Drew the sprites and animations in a pixel art application
Coded animations
Made the initial object t and collision t structs
Multiple lasers functionality
Increase in difficulty over time (spawn rate and increasing speed of asteroids)
Border for score counter
Graphics optimizations

Nina:
Integreated the accelerometer with the rocket / rocket movement
All collision detection functionality
Laser trigger and movement
Interrupts and button
All hardware
Randomness and spawn locations of objects
End screen and restart
Point counter and high score
Start screen

## References
Pat's accelerometer code (accel.c)
Rand.c code from library
Pat, Julie, and TAs' advice on graphics optimizations 

## Self-evaluation

We accomplished everything that we set out to accomplish and more. We started early and were able to crunch out
the "minimum viable product" before Thursday, so we had all of Thursday to add more fun features, like "bugs", different asteroid types,
a start screen, and a high score.

The first heroic moment was getting just a white box to move across the screen using the accelerometer. The combo of hardware and software
was super satisfying. In addition, the graphics optimizations made the box move smoothly.

Another heroic moment we had was when the laser hit the asteroid and it exploded. The collision and animation combo was mind blowing and
made the game so much more addicting and satisfying. It was also super fun to add the bug "glitch" animation where the screen briefly
flashes green when a bug hits the bottom of the screen. We are super excited about the end product, and it was awesome to see our
peers playing the game and telling us that it was super addicting and fun.

For the process, we made sure we had the most basic functionality working first before adding anything fancy. We incrementally added
features. First the accelerometer, then the asteroids, then collisions, then the laser, and then animations. After that, we added "bugs",
more randomness, and increasing diffuclty, among other more "fun" features.

Finally, we learned SO much. We especially learned about graphics: how to optimimize graphics, use animations, use movement, and more.
We also learned about sensors and accelerometers, structs and typedefs, and how to style heavy code to make it more manageable. We practiced
breaking up a huge game into small parts nad and testing each feature thuroughly before moving on.


## Photos
Video attatched!
