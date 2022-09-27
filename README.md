# airwell-infrared-remote-protocol
Reverse engineered Infrared Remote control protocol for Airwell/Email Air ducted airconditioning systems

Includes protocol spec document and reference implementation for Raspberry Pi

Compile with arm-linux-gcc and link against libbcm2835

Drive IR LED from PWM pin, use a transistor as the current is too high for the IO pin to sink

