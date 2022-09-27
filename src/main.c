#include <bcm2835.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* PWM output on RPi Plug P1 pin 12 (which is GPIO pin 18)
 *  in alt fun 5.
 * Note that this is the _only_ PWM pin available on the RPi IO headers
 */

// and it is controlled by PWM channel 0
#define PWM_CHANNEL 0

// This controls the max range of the PWM signal
#define RANGE 253
#define PWM_DUTY 40

// Signal timing parameters
static const long TIME_PREAMBLE_HIGH_US = 3050l;  //~3050us
static const long TIME_PREAMBLE_LOW_US  = 2900l; //~2900us
static const long TIME_MANCHESTER_TRANSITION_US  = 950l; //~900-1000us

// protocol constants
#define TEMP_MAX_C 30
#define TEMP_MIN_C 16

#define DEBUG_MODE


enum air_mode_t //bits 3 downto 1
{
    COOL    = 0x04, // 001 (1 upto 3)
    HEAT    = 0x02, // 010
    RECYCLE = 0x06, // 011
    WATER   = 0x01, // 100
    FAN     = 0x05  // 101
};

enum fan_speed_t //bits 5 down 4
{
    F_LOW     = 0x00, // 00 (4 upto 5)
    F_MED     = 0x02, // 01
    F_HI      = 0x01, // 10
    F_AUTO    = 0x03  // 11
};

enum function_t
{
    SETTING = 0x00, // 0
    POWER   = 0x01  // 1
};

struct infrared_frame_t {
    uint32_t function   : 1;
    uint32_t mode       : 3;
    uint32_t fan        : 2;
    uint32_t reserved1  : 3;
    uint32_t temp       : 8;
    uint32_t reserved2  : 15;
    // + extra 2 bits that are always 1,0
};

union infrared_frame_converter_t
{
    struct infrared_frame_t bitfield;
    uint32_t bits;
};

static inline void system_timer_delay(uint64_t microseconds)
{
    // more accurate sleep than using OS sleep functions
    const uint64_t start = bcm2835_st_read();

    while ((bcm2835_st_read() - start) < microseconds);
}

static inline void outPreamble(void)
{
    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 1);
    system_timer_delay(TIME_PREAMBLE_HIGH_US);
    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 0);
    system_timer_delay(TIME_PREAMBLE_LOW_US);
}

static inline void outBitLow(void)
{
    // A logic 1, encoded as high-low transition
    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 1);
    system_timer_delay(TIME_MANCHESTER_TRANSITION_US);
    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 0);
    system_timer_delay(TIME_MANCHESTER_TRANSITION_US);
}

static inline void outBitHigh(void)
{
    // A logic 0, encoded as low-high transition
    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 0);
    system_timer_delay(TIME_MANCHESTER_TRANSITION_US);
    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 1);
    system_timer_delay(TIME_MANCHESTER_TRANSITION_US);
}

static inline uint8_t encodeTemperature(int temp)
{
    return (uint8_t) (4*temp - 60);
}

static inline void modulateIR(const uint32_t frame)
{
    uint32_t bits = frame;

    outPreamble();

    // Modulate the first 32Bits
    for (int i = 0; i < 32; i++)
    {
        if (bits & 0x00000001)
        {
            outBitHigh();
        }
        else
        {
            outBitLow();
        }

        bits = bits >> 1;

    }

    // Last 2 bits
    outBitHigh();
    outBitLow();
}

static int usage(const char *arg)
{
    printf("usage: %s function mode temperature fan\n", arg);
    printf("\tfunction is either power or set - switch on/off or just update current config\n");
    printf("\tmode is either cool, heat, recycle, fan, droplet\n");
    printf("\ttemperature is an integer between 16 and 30\n");
    printf("\tfan is either low, med, hi, auto\n");
    printf("\t\tNote: fan is always low when using the water drop mode\n");

    return 1;
}

#ifdef DEBUG_MODE
static void printBits(uint32_t bits)
{
    uint32_t bits1 = bits;

    printf("Generated command bits:\n");

    // Modulate the first 32Bits
    for (int i = 0; i < 32; i++)
    {
        if (bits1 & 0x00000001)
        {
            printf("1");
        }
        else
        {
            printf("0");
        }

        bits1 = bits1 >> 1;

    }

    printf("\n");
}
#endif //DEBUG_MODE

int main(int argc, const char** argv)
{
    if (argc == 4)
    {
        if (strncmp(argv[2], "droplet", 7))
        {
            return usage(argv[0]);
        }
    }
    else if (argc != 5)
    {
        return usage(argv[0]);
    }

    enum function_t func;
    enum air_mode_t mode;
    uint8_t temp;
    enum fan_speed_t fan = LOW;

    if (!strncmp(argv[1], "power", 5))
    {
        func = POWER;
    }
    else if (!strncmp(argv[1], "set", 3))
    {
        func = SETTING;
    }
    else
    {
        return usage(argv[0]);
    }

    if (!strncmp(argv[2], "cool", 4))
    {
        mode = COOL;
    }
    else if (!strncmp(argv[2], "heat", 4))
    {
        mode = HEAT;
    }
    else if (!strncmp(argv[2], "fan", 3))
    {
        mode = FAN;
    }
    else if (!strncmp(argv[2], "recycle", 7))
    {
        mode = RECYCLE;
    }
    else if (!strncmp(argv[2], "droplet", 7))
    {
        mode = WATER;
    }
    else
    {
        return usage(argv[0]);
    }

    char *errs = NULL;
    temp = strtol(argv[3], &errs, 10);
    if (*errs != '\x00' || strlen(errs) > 0)
    {
        return usage(argv[0]);
    }

    if (temp < TEMP_MIN_C || temp > TEMP_MAX_C)
    {
        printf("Temperature must be between 16 and 30 degrees Celsius\n");
        return 1;
    }

    temp = encodeTemperature(temp);

    if (mode == WATER)
    {
        fan = F_LOW;
    }
    else
    {
        if (!strncmp(argv[4], "low", 3))
        {
            fan = F_LOW;
        }
        else if (!strncmp(argv[4], "med", 3))
        {
            fan = F_MED;
        }
        else if (!strncmp(argv[4], "hi", 2))
        {
            fan = F_HI;
        }
        else if (!strncmp(argv[4], "auto", 4))
        {
            fan = F_AUTO;
        }
        else
        {
            return usage(argv[0]);
        }
    }

    if (!bcm2835_init())
    {
        printf("Error starting bcm2835\n");
        return 1;
    }


    union infrared_frame_converter_t remoteParams;
    memset(&remoteParams.bitfield, 0, sizeof(struct infrared_frame_t));

    struct infrared_frame_t* frame = &remoteParams.bitfield;
    frame->fan = fan;
    frame->function = func;
    frame->mode = mode;
    frame->temp = temp;

    // Set the output pin to Alt Fun 5, to allow PWM channel 0 to be output there
    bcm2835_gpio_fsel(RPI_GPIO_P1_12, BCM2835_GPIO_FSEL_ALT5);
    bcm2835_pwm_set_clock(BCM2835_PWM_CLOCK_DIVIDER_2);
    bcm2835_pwm_set_range(PWM_CHANNEL, RANGE); //(19.2MHz / 2) / 253 = 37944KHz
    bcm2835_pwm_set_data(PWM_CHANNEL, (PWM_DUTY * RANGE)/100); // 40% duty

    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 0); //disabled by default

    // frame repeats 3 times

    const uint32_t bits = remoteParams.bits;

    #ifdef DEBUG_MODE
    printBits(bits);
    #endif // DEBUG_MODE

    modulateIR(bits);
    modulateIR(bits);
    modulateIR(bits);

    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 1);
    system_timer_delay(4000);

    // Switch off the IR LED when done
    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 0);

    return 0;
}
