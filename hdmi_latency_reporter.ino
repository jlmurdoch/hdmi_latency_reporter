#include "hardware/vreg.h"
#include "src/libdvi/dvi.h"
#include "src/libdvi/dvi_serialiser.h"
#include "font_8x8.h"

#define VIDEO_INPUT_PIN 21 // Phototransistor
#define AUDIO_INPUT_PIN 20 // Clap Detector
#define FLASH_INTERVAL 10000 // Flash screen every 10 secs for 17ms

// Using an Olimex Pico PC
static const struct dvi_serialiser_cfg Olimex_RP2040_PICO_PC_cfg = {
        .pio = pio0,
        .sm_tmds = {0, 1, 2},
        .pins_tmds = {14, 18, 16},
        .pins_clk = 12,
        .invert_diffpairs = true
};
#define DVI_DEFAULT_SERIAL_CONFIG Olimex_RP2040_PICO_PC_cfg

// Video setup
#define FRAME_WIDTH 320
#define FRAME_HEIGHT 240
#define VREG_VSEL VREG_VOLTAGE_1_20
#define DVI_TIMING dvi_timing_640x480p_60hz

// For filling the DVI buffer prior to start
#define N_SCANLINE_BUFFERS 6
#define N_SCANLINE_OFFSET 225
uint16_t __attribute__((aligned(4))) static_scanbuf[N_SCANLINE_BUFFERS][FRAME_WIDTH];

// Audio setup
#define AUDIO_BUFFER_SIZE   256
audio_sample_t audio_buffer[AUDIO_BUFFER_SIZE];
struct repeating_timer audio_timer;

// Sine wave - quiet
const int16_t sine[32] = {
    0,  12,  24,  35,  45,  53,  59,  62,  63,  62,  59,  53,  45,  35,  24,  12,
    0, -12, -24, -35, -45, -53, -59, -62, -63, -62, -59, -53, -45, -35, -24, -12
};

// Global variables
uint16_t *scanbuf;
struct dvi_inst dvi0;
uint frame_counter = 0;
uint line_counter = 0;
unsigned long v_rendered, v_sensed, v_delay = 0;
unsigned long a_playback, a_sensed, a_delay = 0;
volatile bool wait_begin = true;

// Loops constantly the audio sine
bool audio_timer_callback(struct repeating_timer *t) {
    int size = get_write_size(&dvi0.audio_ring, false);
    audio_sample_t *audio_ptr = get_write_pointer(&dvi0.audio_ring);
    audio_sample_t sample;
    static uint sample_count = 0;
    // Quiet (small amplitude)
    int volume = 1;
    
    // Loud (high amplitude) when the first frame appears
    if (frame_counter == 0) {
      volume = 512;
      a_playback = millis();
      a_delay = 0;
      a_sensed = 0;
    }

    // Load sample
    for (int cnt = 0; cnt < size; cnt++) {
        sample.channels[0] = sine[sample_count % 32] * volume;
        sample.channels[1] = sine[sample_count % 32] * volume;
        *audio_ptr++ = sample;
        sample_count++;
    }
    increase_write_pointer(&dvi0.audio_ring, size);
 
    return true;
}

// Render the characters from the 720 x 8 (95 char) array
void draw_string(uint x, uint y, char *text, uint length) {
  int block = x/8;
  if (length > block) {
    scanbuf[x] = ((font_8x8[(text[block] - 32) + (y % 8) * 95] >> (x % 8)) & 0x1) * 0xFFFF;
  } else {
    scanbuf[x] = 0x0000;
  }
}

// Print the results...
void draw_result_screen(uint y) {
  char video[12];
  sprintf(video, "Video: %d", v_delay);
  char audio[12];
  sprintf(audio, "Audio: %d", a_delay);

  while (queue_try_remove_u32(&dvi0.q_colour_free, &scanbuf))
		  ;
  
  // Print video result
  if ( y < 8 ) {
    uint length = strlen(video);
    for (uint16_t x = 0; x < FRAME_WIDTH; ++x)
      draw_string(x, y, video, length);
  // Print audio result
  } else if ( y < 16 ) {
    uint length = strlen(audio);
    for (uint16_t x = 0; x < FRAME_WIDTH; ++x)
      draw_string(x, y, audio, length);
  // Print black
  } else
    for (uint16_t x = 0; x < FRAME_WIDTH; ++x)
      scanbuf[x] = 0x0000;
  
  queue_add_blocking_u32(&dvi0.q_colour_valid, &scanbuf);
}

// Render the screen flash
void draw_white_screen(void) {
  while (queue_try_remove_u32(&dvi0.q_colour_free, &scanbuf))
		  ;
  for (uint16_t x = 0; x < FRAME_WIDTH; ++x)
      scanbuf[x] = 0xFFFF;

  queue_add_blocking_u32(&dvi0.q_colour_valid, &scanbuf);
}

// Core 1 setup for DVI rendering
void setup1() {
  while (wait_begin)
    ;
  dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
  dvi_start(&dvi0);

  // Indicator that setup is good
  digitalWrite(LED_BUILTIN, HIGH);
}

// Core 1 loop - DVI signaling when the buffer is ready
void loop1() {
  dvi_scanbuf_main_16bpp(&dvi0);

  // Should never get here
  while (true) {}
	__builtin_unreachable(); 
}

void setup() {
  // Light sensor - needs pulldown
  pinMode(VIDEO_INPUT_PIN, INPUT_PULLDOWN);

  // Clap sensor - needs pullup
  pinMode(AUDIO_INPUT_PIN, INPUT_PULLUP);

  // Set up voltage and timing
  vreg_set_voltage(VREG_VSEL);
  delay(10);
  set_sys_clock_khz(DVI_TIMING.bit_clk_khz, true);
  
  // Set up DVI for video
  dvi0.timing = &DVI_TIMING;
	dvi0.ser_cfg = DVI_DEFAULT_SERIAL_CONFIG;
	dvi_init(&dvi0, next_striped_spin_lock_num(), next_striped_spin_lock_num());

  // May need to tweak these for different screens (e.g. 8 for a monitor)
  dvi_get_blank_settings(&dvi0)->top = 0;
  dvi_get_blank_settings(&dvi0)->bottom = 0;
  
  // Set up DVI for audio
  dvi_audio_sample_buffer_set(&dvi0, audio_buffer, AUDIO_BUFFER_SIZE);
  dvi_set_audio_freq(&dvi0, 44100, 28000, 6272);
  add_repeating_timer_ms(2, audio_timer_callback, NULL, &audio_timer);

  // Initialises the scanbuf
  memset(scanbuf, 0xFF, FRAME_WIDTH * 2);

  // Prep / fill the video buffer
  for (int i = 0; i < N_SCANLINE_BUFFERS; ++i) {
	  void *bufptr = &static_scanbuf[i];
	  queue_add_blocking((queue_t *)&dvi0.q_colour_free, &bufptr);
	}

  // Correct the display due to the buffer-filling above
  line_counter = N_SCANLINE_BUFFERS + N_SCANLINE_OFFSET;

  // Inform core1 to start rendering
  wait_begin = false; 
}

void loop() {
  if (frame_counter == 0) {
    // Flash the screen, perform reset of vars
    draw_white_screen();
    v_rendered = millis();
    v_delay = 0;
    v_sensed = 0;
  } else {
    // Read the phototransistor for change
    if (v_sensed == 0 && digitalRead(VIDEO_INPUT_PIN) == HIGH) {
      v_sensed = millis();
      v_delay = v_sensed - v_rendered;
    }
    // Read the clap detector for change
    if (a_sensed == 0 && digitalRead(AUDIO_INPUT_PIN) == LOW) {
      a_sensed = millis();
      a_delay = a_sensed - a_playback;
    }
    // Render the results on screen
    draw_result_screen(line_counter);
  }

  // Reset the line counter 
  if (++line_counter == FRAME_HEIGHT) {
    line_counter = 0;
    
    // Reset / perform flash every X milliseconds (17ms == 60Hz)
    if (millis() % FLASH_INTERVAL < 17)
      frame_counter = 0;
    else
      frame_counter++;
  }
}