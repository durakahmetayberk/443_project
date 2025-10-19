#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>

/* =========================
   System Parameters (tune later)
   ========================= */
#define RANDOM_WAIT_MIN_MS 1000 // PI1
#define RANDOM_WAIT_MAX_MS 3000 // PI1
#define VISUAL_WINDOW_MS 1200   // PI2: max allowed after LED turns green
#define TACTILE_WINDOW_MS 1500  // PI3: time allowed for tactile after visual
#define PRESSURE_THRESHOLD 400  // PI3: mock ADC threshold (0..1023)
#define UART_BAUD 115200        // PI3

/* =========================
   Global State (mocked)
   ========================= */
typedef enum
{
    ST_IDLE = 0,
    ST_ARMED,
    ST_STIM_ON,
    ST_VIS_DONE,
    ST_TACT_DONE,
    ST_TACT_TIMEOUT,
    ST_ABORT_RETRY,
    ST_REPORT,
    ST_FEEDBACK
} sys_state_t;

static sys_state_t g_state = ST_IDLE;
static uint16_t g_difficulty = 50;            // 0..100 via potentiometer (PI3)
static uint32_t g_random_wait_ms = 0;         // PI1
static uint32_t g_visual_ms = 0;              // PI2
static uint32_t g_tactile_ms = 0;             // PI3
static uint32_t g_best_total_ms = 0xFFFFFFFF; // PI3
static uint32_t g_round_ix = 0;               // for mock sequence
static uint32_t g_time = 0;                   // mock time tracker
static bool g_score_improved = false;         // track if best score improved this round

/* =========================
   Utilities (purely mock)
   ========================= */
static uint32_t clamp(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/* =========================
   PI1 — GPIO / TIMERS / BASIC UI
   ========================= */

// Mock: start-button press (EXTI in real hw)
int pi1_button_pressed(void)
{
    // For demo: press on every iteration start
    printf("[PI1] Start button pressed.\n");
    return 1;
}

// Mock: random wait 1–3 s, scaled by difficulty (harder → shorter)
uint32_t pi1_compute_random_wait_ms(uint16_t difficulty_0_100)
{
    uint32_t span = RANDOM_WAIT_MAX_MS - RANDOM_WAIT_MIN_MS;
    // Generate a real random number within the span
    uint32_t random_offset = rand() % (span + 1);
    uint32_t res = RANDOM_WAIT_MIN_MS + random_offset;
    printf("[PI1] Random wait chosen = %ums (diff=%u)\n", res, difficulty_0_100);
    return res;
}

// Mock: drive LED green + short vibration burst
void pi1_stim_on_led_and_vibe(void)
{
    printf("[PI1] STIM_ON: LED=GREEN, vibration=short buzz\n");
}

// Mock: 7-seg display show message (eg. "GO")
void pi1_7seg_show_msg(const char *label)
{
    printf("[PI1] 7SEG: %s\n", label);
}

// Mock: 7-seg display show number (ms)
void pi1_7seg_show_ms(const char *label, uint32_t ms)
{
    printf("[PI1] 7SEG: %s = %u ms\n", label, ms);
}
/* =========================
   PI2 — IC/OC: ULTRASONIC VISUAL MEASURE
   ========================= */

// Mock: visual sensor detection function
int visual_sensor_output(uint32_t mock_time)
{
    // Simulate sensor detection based on some condition
    // For now, return 0 (no detection) - you can implement actual logic here
    return 0;
}

// Mock: hand crosses sensor after LED→GREEN; returns measured ms or 0 if timeout
uint32_t pi2_capture_visual_ms(uint32_t window_ms)
{
    // Produce a plausible reaction 180..520ms; sometimes exceed window for abort
    uint32_t base = 180 + (37 * ((g_round_ix * 5) % 10)); // 180..550
    if (base > window_ms)
    {
        printf("[PI2] Visual timeout (> %u ms)\n", window_ms);
        return 0;
    }
    printf("[PI2] Visual reaction captured = %u ms\n", base);
    return base;
}

/* =========================
   PI3 — ADC + UART: PRESSURE / POT / REPORT
   ========================= */

// Mock: read potentiometer via ADC → difficulty
uint16_t pi3_read_pot_difficulty(void)
{
    // Wave between 30..80 to show variety
    uint16_t d = 30 + (g_round_ix * 7) % 51;
    printf("[PI3] Pot difficulty read = %u\n", d);
    return d;
}

// Mock: read pressure ADC value
uint16_t pi3_read_pressure_adc(void)
{
    // Generate values that cross threshold after some delay; if round %5==2, timeout
    uint16_t val = (g_round_ix % 5 == 2) ? 200 : (300 + (g_round_ix * 150) % 600);
    printf("[PI3] Pressure ADC = %u\n", val);
    return val;
}

// Mock: poll until window or threshold reached → returns tactile ms or 0 if timeout
uint32_t pi3_capture_tactile_ms(uint32_t window_ms, uint16_t threshold)
{
    // Synthesize a tactile time derived from round index
    if (g_round_ix % 5 == 2)
    {
        printf("[PI3] Tactile timeout (no press within %u ms)\n", window_ms);
        return 0;
    }
    uint32_t t = 140 + (23 * ((g_round_ix * 3) % 12)); // 140..~400
    // "Check" threshold once (mock)
    uint16_t adc = pi3_read_pressure_adc();
    if (adc < threshold)
    {
        // pretend user presses a bit later
        t = clamp(t + 80, 0, window_ms);
    }
    if (t > window_ms)
    {
        printf("[PI3] Tactile timeout (computed %u > %u)\n", t, window_ms);
        return 0;
    }
    printf("[PI3] Tactile reaction captured = %u ms\n", t);
    return t;
}

// Mock: UART TX of the round result
void pi3_uart_send_result(uint32_t rnd,
                          uint32_t wait_ms,
                          uint32_t vis_ms,
                          uint32_t tact_ms,
                          uint32_t total_ms,
                          uint32_t best_ms)
{
    printf("[PI3][UART %d bps] Rnd=%u, Wait=%u, Vis=%u, Tact=%u, Total=%u, Best=%u\n",
           UART_BAUD, rnd, wait_ms, vis_ms, tact_ms, total_ms, best_ms);
}

/* =========================
   State-machine helpers (composition)
   ========================= */

void state_to_idle(void)
{
    g_state = ST_IDLE;
    printf("[SYS] → IDLE\n");
}
void state_to_abort(void)
{
    g_state = ST_ABORT_RETRY;
    g_time = 0; // reset mock time
    printf("[SYS] → ABORT/RETRY\n");
}
void state_to_feedback(void)
{
    g_state = ST_FEEDBACK;
    printf("[SYS] → FEEDBACK\n");
}

/* =========================
   One game round (blocking mock)
   ========================= */
void run_one_round(void)
{
    // IDLE
    state_to_idle();
    if (!pi1_button_pressed())
        return;

    // Read difficulty (pot) — PI3
    g_difficulty = pi3_read_pot_difficulty();

    // ARMED
    g_state = ST_ARMED;
    g_random_wait_ms = pi1_compute_random_wait_ms(g_difficulty);

    // Early hand? (false trigger) — PI2
    for (int t = g_time; t < g_random_wait_ms; t += 1)
    {
        if (visual_sensor_output(t))
        {
            state_to_abort();
            return;
        }
        g_time = t;
    }

    // STIM_ON
    g_state = ST_STIM_ON;
    pi1_stim_on_led_and_vibe();

    // VISUAL measure — PI2
    uint32_t visual_start_time = g_time; // Capture start time to avoid issues if g_time changes
    for (int t = visual_start_time; t < visual_start_time + VISUAL_WINDOW_MS; t += 1)
    {
        if (visual_sensor_output(t))
        {
            g_visual_ms = t - visual_start_time;
            g_state = ST_VIS_DONE;
            g_time = t; // Update g_time to current position
            break;
        }
        g_time = t; // Update g_time as we progress through the loop
    }
    if (g_state != ST_VIS_DONE)
    {
        state_to_abort(); // treat visual timeout as abort/retry
        return;
    }
    pi1_7seg_show_ms("VIS", g_visual_ms);

    // TACTILE measure — PI3
    uint32_t tactile_start_time = g_time; // Capture start time
    for (int t = tactile_start_time; t < tactile_start_time + TACTILE_WINDOW_MS; t += 1)
    {
        uint16_t adc = pi3_read_pressure_adc();
        if (adc >= PRESSURE_THRESHOLD)
        {
            g_tactile_ms = t - tactile_start_time;
            g_state = ST_TACT_DONE;
            g_time = t; // Update g_time
            pi1_7seg_show_ms("TAC", g_tactile_ms);
            break;
        }
        g_time = t; // Update g_time
    }
    if (g_state != ST_TACT_DONE)
    {
        printf("[SYS] No tactile within window → N/A\n");
        state_to_abort(); // treat tactile timeout as abort/retry
        return;
    }

    // REPORT
    g_state = ST_REPORT;
    uint32_t total = g_visual_ms + (g_tactile_ms);
    if (total < g_best_total_ms)
        g_best_total_ms = total;
    g_score_improved = true;
    pi1_7seg_show_ms("TOT", total);
    pi3_uart_send_result(g_round_ix, g_random_wait_ms, g_visual_ms, g_tactile_ms, total, g_best_total_ms);

    // FEEDBACK if best improved — PI1 (LED) + optional buzzer later
    if (g_score_improved)
    {
        state_to_feedback();
        printf("[PI1] BEST improved → LED blink + buzzer (mock)\n");
    }
    return;
}

/* =========================
   main()
   ========================= */
int main(void)
{
    printf("=== Reflex Game Conceptual Design (Mock) ===\n");

    // Initialize random number generator
    srand(time(NULL));

    // Mock 6 rounds to demonstrate paths
    for (g_round_ix = 1; g_round_ix <= 6; ++g_round_ix)
    {
        printf("\n----- Round %u -----\n", g_round_ix);
        run_one_round();
    }

    printf("\nBest total so far = %u ms\n", g_best_total_ms);
    return 0;
}
