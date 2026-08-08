/*
  Milestone 1: Audio Loopback Test
  Board: ESP32 WROOM (single board, TEMPORARY test wiring)
  Purpose: Verify the INMP441 mic -> MAX98357A amp -> speaker signal
           chain works BEFORE adding ESP-NOW wireless, compression,
           and encryption. If you can hear your own voice come out
           of the speaker with acceptable latency and no garbage
           noise, the hardware and I2S config are good.

  TEMPORARY TEST WIRING (this is NOT the final split-node wiring --
  in the final build the mic lives on the ESP32-C3 node and this
  board only has the amp side, fed over ESP-NOW instead of a wire):

  INMP441 (mic)         ESP32 WROOM
    VDD  ----------------- 3.3V
    GND  ----------------- GND
    L/R  ----------------- GND      (selects left channel)
    WS   ----------------- GPIO15
    SCK  ----------------- GPIO14
    SD   ----------------- GPIO32

  MAX98357A (amp)       ESP32 WROOM
    VIN  ----------------- 5V
    GND  ----------------- GND
    SD   ----------------- GPIO33   (HIGH = enabled)
    GAIN ----------------- floating (default ~9dB)
    BCLK ----------------- GPIO27
    LRC  ----------------- GPIO26
    DIN  ----------------- GPIO25
    OUT+/OUT- ------------- Speaker terminals

  These pins were chosen to avoid conflicting with the OLED
  (SDA=21, SCL=22) so you can add the display to this same test
  sketch later if you want a "status: audio OK" message on screen.
*/

#include <driver/i2s.h>

// ---- Mic (I2S port 0, receive) ----
#define MIC_WS   15
#define MIC_SCK  14
#define MIC_SD   32

// ---- Amp (I2S port 1, transmit) ----
#define AMP_BCLK 27
#define AMP_LRC  26
#define AMP_DIN  25
#define AMP_SD   33

#define SAMPLE_RATE      16000
#define BUFFER_SAMPLES   256
#define MIC_GAIN         8      // raw INMP441 levels are quiet; tune this by ear

int32_t rawSamples[BUFFER_SAMPLES];  // INMP441 sends 32-bit frames (24-bit audio, left-justified)
int16_t audioOut[BUFFER_SAMPLES];    // MAX98357A wants 16-bit

void setupMicI2S() {
  i2s_config_t micConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t micPins = {
    .bck_io_num = MIC_SCK,
    .ws_io_num = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_SD
  };

  i2s_driver_install(I2S_NUM_0, &micConfig, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &micPins);
}

void setupAmpI2S() {
  i2s_config_t ampConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t ampPins = {
    .bck_io_num = AMP_BCLK,
    .ws_io_num = AMP_LRC,
    .data_out_num = AMP_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_1, &ampConfig, 0, NULL);
  i2s_set_pin(I2S_NUM_1, &ampPins);
}

void setup() {
  Serial.begin(115200);

  pinMode(AMP_SD, OUTPUT);
  digitalWrite(AMP_SD, HIGH);  // enable the amplifier (LOW would mute it)

  setupMicI2S();
  setupAmpI2S();

  Serial.println("Audio loopback test running. Speak near the mic.");
  Serial.println("If you hear nothing, check the troubleshooting notes at the bottom of this file.");
}

void loop() {
  size_t bytesRead = 0;
  size_t bytesWritten = 0;

  // Pull one buffer of raw 32-bit samples from the mic
  i2s_read(I2S_NUM_0, rawSamples, sizeof(rawSamples), &bytesRead, portMAX_DELAY);
  int samplesRead = bytesRead / sizeof(int32_t);

  // INMP441 gives 24-bit audio, left-justified inside a 32-bit word.
  // Shift down to a usable 16-bit sample and apply a gain multiplier,
  // since raw levels are usually much quieter than full scale.
  for (int i = 0; i < samplesRead; i++) {
    int32_t sample24 = rawSamples[i] >> 8;       // drop the bottom padding byte
    int32_t sample16 = (sample24 >> 8) * MIC_GAIN; // scale down to 16-bit range, then boost
    if (sample16 > 32767)  sample16 = 32767;
    if (sample16 < -32768) sample16 = -32768;
    audioOut[i] = (int16_t)sample16;
  }

  // Write straight out to the amp -- this is the loopback path.
  // In the real build, this buffer gets ADPCM-compressed and sent
  // over ESP-NOW instead of written locally.
  i2s_write(I2S_NUM_1, audioOut, samplesRead * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
}

/*
  TROUBLESHOOTING

  No sound at all:
   - Confirm AMP_SD (GPIO33) is reading HIGH -- probe it with a multimeter
     or just double check digitalWrite(AMP_SD, HIGH) ran.
   - Confirm speaker is actually connected to OUT+/OUT-, not VIN/GND.
   - Confirm INMP441 L/R pin is tied to GND, not floating -- floating
     L/R gives unreliable channel selection and can read as silence.

  Loud static / garbage noise:
   - Double-check BCLK/LRC/DIN aren't swapped between mic and amp wiring.
   - Try lowering MIC_GAIN -- if the mic is clipping, dropping gain to 2-4
     often cleans it up immediately.

  Feedback squeal:
   - Physically move the mic and speaker apart -- this is acoustic feedback
     from the loopback, not a wiring problem. It'll go away once the mic
     and amp are on separate boards in the real build.

  Noticeable lag between speaking and hearing yourself:
   - Expected to some degree with DMA buffering. Try lowering
     BUFFER_SAMPLES (e.g. to 128) for lower latency at the cost of more
     CPU overhead, or raising dma_buf_count for more headroom if you
     hear dropouts instead.
*/
