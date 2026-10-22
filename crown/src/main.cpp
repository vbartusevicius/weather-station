// Crown v2 firmware (STM32G431RBT6): ultrasonic wind, piezo rain and AS3935 lightning as an I2C peripheral of main.
//
// Hardware (hardware/circuits/blocks/crown_sensors.py):
//   TIM8 CH1 / CH1N (PC6 / PC10) -> UCC27524 INA / INB of all four drivers; XD_EN_N/E/S/W (PC7, PC8, PC9, PC11)
//   select the transmitting transducer. MUX_S0/S1 (PB6, PB7) select the listening one on the 74HC4052.
//   PA1 -> OPAMP1 PGA (x2..x32) -> ADC1 (internal VOPAMP1), triggered at exactly 2 MSPS by TIM6, DMA1 CH1.
//   PA4 -> ADC2_IN17 rain charge amplifier, 50 kSPS by TIM7, circular DMA1 CH2.
//   SPI1 (PB3 SCK, PB4 MISO, PB5 MOSI, PA15 CS) + PB9 IRQ -> AS3935.
//   I2C2 (PC4 SCL, PA8 SDA) peripheral 0x2A; PC12 = CROWN_IRQ to main. HSE 8 MHz -> 170 MHz.
// The DSP / protocol logic lives in lib/crown_core (host-tested: pio test -d crown -e native).

#include <cstring>
#include <initializer_list>

#include "EchoDsp.hpp"
#include "RainDetector.hpp"
#include "RegisterBank.hpp"
#include "crown_protocol.hpp"
#include "stm32g4xx_hal.h"

namespace {

constexpr crown::FirmwareVersion kVersion{0, 2, 0};   // 0.2: protocol v2 (status CRC, burst pause)
constexpr std::size_t kEchoN = 1400;          // 0.7 ms at 2 MSPS (latest arrival: 60 m/s head wind at -40 C)
constexpr float kFs = 2.0e6F;
constexpr std::uint32_t kTimerHz = 170000000U;
constexpr std::uint32_t kBootMagic = 0xB007C0DEU;

ADC_HandleTypeDef hadc1, hadc2;
DMA_HandleTypeDef hdma_adc1, hdma_adc2;
OPAMP_HandleTypeDef hopamp1;
TIM_HandleTypeDef htim6, htim7, htim8;
SPI_HandleTypeDef hspi1;
I2C_HandleTypeDef hi2c2;
IWDG_HandleTypeDef hiwdg;

crown::RegisterBank g_bank(kVersion);
crown::RainDetector g_rain;
std::uint16_t g_echo_raw[kEchoN];
std::int16_t g_echo[2][kEchoN];   // one reciprocal pair at a time (32 KB SRAM)
float g_scratch[2 * kEchoN];
std::uint16_t g_rain_buf[256];
volatile bool g_echo_done = false;
volatile bool g_burst_active = false;
volatile bool g_as_irq = false;
volatile std::uint32_t g_as_irq_ms = 0;
volatile std::uint32_t g_lco_edges = 0;
volatile bool g_lco_counting = false;
std::uint32_t g_rain_us = 0;
std::uint8_t g_gain[4] = {1, 1, 1, 1};   // x4: ~1.4 Vpp for the expected ~50 mV echo (hardware/sim)
__attribute__((section(".noinit"))) volatile std::uint32_t g_boot_flag;

// I2C peripheral state
std::uint8_t g_i2c_rx[1 + sizeof(crown::ConfigBlock)];
std::uint8_t g_i2c_tx[40];
std::uint8_t g_i2c_reg = 0;

// Transducer index on the mux / driver: 0 N, 1 E, 2 S, 3 W. Paths: NS, SN, EW, WE (crown::Path order).
constexpr std::uint8_t kTx[4] = {0, 2, 1, 3};
constexpr std::uint8_t kRx[4] = {2, 0, 3, 1};
constexpr std::uint16_t kEnPin[4] = {GPIO_PIN_7, GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_11};   // GPIOC

void error_loop() {
    __disable_irq();
    for (;;) {
    }
}

void jump_to_bootloader() {
    // STM32G4 system memory (ROM bootloader) at 0x1FFF0000: stack pointer at +0, reset vector at +4.
    constexpr std::uint32_t kSysMem = 0x1FFF0000U;
    HAL_RCC_DeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    __disable_irq();
    for (auto& icer : NVIC->ICER) icer = 0xFFFFFFFFU;
    for (auto& icpr : NVIC->ICPR) icpr = 0xFFFFFFFFU;
    __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
    __set_MSP(*reinterpret_cast<volatile std::uint32_t*>(kSysMem));
    reinterpret_cast<void (*)()>(*reinterpret_cast<volatile std::uint32_t*>(kSysMem + 4))();
}

void clock_init() {
    RCC_OscInitTypeDef osc{};
    RCC_ClkInitTypeDef clk{};
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    osc.HSEState = RCC_HSE_ON;
    osc.LSIState = RCC_LSI_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = RCC_PLLM_DIV2;    // 4 MHz
    osc.PLL.PLLN = 85;               // 340 MHz VCO
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = RCC_PLLQ_DIV2;
    osc.PLL.PLLR = RCC_PLLR_DIV2;    // 170 MHz SYSCLK
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) error_loop();
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) error_loop();
}

void gpio_init() {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitTypeDef g{};
    // driver enables (all off), CROWN_IRQ, mux select, AS3935 CS (idle high)
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11 | GPIO_PIN_12, GPIO_PIN_RESET);
    g.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11 | GPIO_PIN_12;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
    g.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOA, &g);
    // AS3935 IRQ (active high)
    g.Pin = GPIO_PIN_9;
    g.Mode = GPIO_MODE_IT_RISING;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void tim8_init() {
    // 40 kHz complementary PWM, one-pulse mode with the repetition counter = burst length, ~120 ns dead time.
    __HAL_RCC_TIM8_CLK_ENABLE();
    GPIO_InitTypeDef g{};
    g.Pin = GPIO_PIN_6 | GPIO_PIN_10;
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF4_TIM8;
    HAL_GPIO_Init(GPIOC, &g);
    htim8.Instance = TIM8;
    htim8.Init.Prescaler = 0;
    htim8.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim8.Init.Period = kTimerHz / 40000U - 1U;
    htim8.Init.RepetitionCounter = 7;
    htim8.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    if (HAL_TIM_PWM_Init(&htim8) != HAL_OK) error_loop();
    if (HAL_TIM_OnePulse_Init(&htim8, TIM_OPMODE_SINGLE) != HAL_OK) error_loop();
    TIM_OC_InitTypeDef oc{};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = (kTimerHz / 40000U) / 2U;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;     // both driver inputs low between bursts
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    if (HAL_TIM_PWM_ConfigChannel(&htim8, &oc, TIM_CHANNEL_1) != HAL_OK) error_loop();
    TIM_BreakDeadTimeConfigTypeDef bdt{};
    bdt.OffStateRunMode = TIM_OSSR_ENABLE;
    bdt.OffStateIDLEMode = TIM_OSSI_ENABLE;
    bdt.DeadTime = 20;                           // 20 / 170 MHz = 118 ns
    bdt.BreakState = TIM_BREAK_DISABLE;
    bdt.Break2State = TIM_BREAK2_DISABLE;
    bdt.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
    if (HAL_TIMEx_ConfigBreakDeadTime(&htim8, &bdt) != HAL_OK) error_loop();
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_1);
    __HAL_TIM_DISABLE(&htim8);
}

void basic_timer(TIM_HandleTypeDef& h, TIM_TypeDef* inst, std::uint32_t hz) {
    h.Instance = inst;
    h.Init.Prescaler = 0;
    h.Init.CounterMode = TIM_COUNTERMODE_UP;
    h.Init.Period = kTimerHz / hz - 1U;
    h.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&h) != HAL_OK) error_loop();
    TIM_MasterConfigTypeDef m{};
    m.MasterOutputTrigger = TIM_TRGO_UPDATE;
    m.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&h, &m);
}

void adc_init() {
    RCC_PeriphCLKInitTypeDef pclk{};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_ADC12;
    pclk.Adc12ClockSelection = RCC_ADC12CLKSOURCE_SYSCLK;
    HAL_RCCEx_PeriphCLKConfig(&pclk);
    __HAL_RCC_ADC12_CLK_ENABLE();
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_TIM6_CLK_ENABLE();
    __HAL_RCC_TIM7_CLK_ENABLE();
    basic_timer(htim6, TIM6, 2000000U);
    basic_timer(htim7, TIM7, 50000U);

    // OPAMP1 PGA on PA1, output internal to ADC1 (VOPAMP1)
    GPIO_InitTypeDef g{};
    g.Pin = GPIO_PIN_1 | GPIO_PIN_4;
    g.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &g);
    hopamp1.Instance = OPAMP1;
    hopamp1.Init.PowerMode = OPAMP_POWERMODE_HIGHSPEED;
    hopamp1.Init.Mode = OPAMP_PGA_MODE;
    hopamp1.Init.NonInvertingInput = OPAMP_NONINVERTINGINPUT_IO0;   // PA1
    hopamp1.Init.InternalOutput = ENABLE;
    hopamp1.Init.TimerControlledMuxmode = OPAMP_TIMERCONTROLLEDMUXMODE_DISABLE;
    hopamp1.Init.PgaConnect = OPAMP_PGA_CONNECT_INVERTINGINPUT_NO;
    hopamp1.Init.PgaGain = OPAMP_PGA_GAIN_4_OR_MINUS_3;
    hopamp1.Init.UserTrimming = OPAMP_TRIMMING_FACTORY;
    if (HAL_OPAMP_Init(&hopamp1) != HAL_OK) error_loop();
    HAL_OPAMP_Start(&hopamp1);

    ADC_ChannelConfTypeDef ch{};
    for (auto* pair : {&hadc1, &hadc2}) {
        ADC_HandleTypeDef& h = *pair;
        const bool echo = (pair == &hadc1);
        h.Instance = echo ? ADC1 : ADC2;
        h.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;   // 42.5 MHz
        h.Init.Resolution = ADC_RESOLUTION_12B;
        h.Init.DataAlign = ADC_DATAALIGN_RIGHT;
        h.Init.ScanConvMode = ADC_SCAN_DISABLE;
        h.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
        h.Init.ContinuousConvMode = DISABLE;
        h.Init.NbrOfConversion = 1;
        h.Init.ExternalTrigConv = echo ? ADC_EXTERNALTRIG_T6_TRGO : ADC_EXTERNALTRIG_T7_TRGO;
        h.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
        h.Init.DMAContinuousRequests = ENABLE;
        h.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
        if (HAL_ADC_Init(&h) != HAL_OK) error_loop();
        HAL_ADCEx_Calibration_Start(&h, ADC_SINGLE_ENDED);
        ch.Channel = echo ? ADC_CHANNEL_VOPAMP1 : ADC_CHANNEL_17;
        ch.Rank = ADC_REGULAR_RANK_1;
        ch.SamplingTime = ADC_SAMPLETIME_6CYCLES_5;
        ch.SingleDiff = ADC_SINGLE_ENDED;
        ch.OffsetNumber = ADC_OFFSET_NONE;
        if (HAL_ADC_ConfigChannel(&h, &ch) != HAL_OK) error_loop();
    }
    for (auto* d : {&hdma_adc1, &hdma_adc2}) {
        const bool echo = (d == &hdma_adc1);
        d->Instance = echo ? DMA1_Channel1 : DMA1_Channel2;
        d->Init.Request = echo ? DMA_REQUEST_ADC1 : DMA_REQUEST_ADC2;
        d->Init.Direction = DMA_PERIPH_TO_MEMORY;
        d->Init.PeriphInc = DMA_PINC_DISABLE;
        d->Init.MemInc = DMA_MINC_ENABLE;
        d->Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        d->Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        d->Init.Mode = echo ? DMA_NORMAL : DMA_CIRCULAR;
        d->Init.Priority = echo ? DMA_PRIORITY_VERY_HIGH : DMA_PRIORITY_MEDIUM;
        if (HAL_DMA_Init(d) != HAL_OK) error_loop();
    }
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
    __HAL_LINKDMA(&hadc2, DMA_Handle, hdma_adc2);
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);

    HAL_ADC_Start_DMA(&hadc2, reinterpret_cast<std::uint32_t*>(g_rain_buf), 256);
    HAL_TIM_Base_Start(&htim7);
}

void spi_init() {
    __HAL_RCC_SPI1_CLK_ENABLE();
    GPIO_InitTypeDef g{};
    g.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5;
    g.Mode = GPIO_MODE_AF_PP;
    g.Speed = GPIO_SPEED_FREQ_MEDIUM;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOB, &g);
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;          // AS3935: data sampled on the falling edge
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;   // 1.3 MHz (AS3935 <= 2 MHz)
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) error_loop();
}

void i2c_init() {
    __HAL_RCC_I2C2_CLK_ENABLE();
    GPIO_InitTypeDef g{};
    g.Mode = GPIO_MODE_AF_OD;
    g.Pull = GPIO_NOPULL;            // 2.2k pull-ups on main
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Alternate = GPIO_AF4_I2C2;
    g.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_8;
    HAL_GPIO_Init(GPIOA, &g);
    hi2c2.Instance = I2C2;
    hi2c2.Init.Timing = 0x40B285C2;  // 100 kHz at 170 MHz (CubeMX); the controller may run up to 400 kHz
    hi2c2.Init.OwnAddress1 = static_cast<std::uint32_t>(crown::kI2cAddress) << 1;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c2) != HAL_OK) error_loop();
    HAL_I2CEx_ConfigAnalogFilter(&hi2c2, I2C_ANALOGFILTER_ENABLE);
    HAL_NVIC_SetPriority(I2C2_EV_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C2_EV_IRQn);
    HAL_NVIC_SetPriority(I2C2_ER_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(I2C2_ER_IRQn);
    HAL_I2C_EnableListen_IT(&hi2c2);
}

void watchdog_init() {
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;   // 32 kHz / 32 = 1 kHz
    hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
    hiwdg.Init.Reload = 2000;                   // 2 s
    HAL_IWDG_Init(&hiwdg);
}

// ------------------------------------------------------------------ AS3935 (SPI mode)
std::uint8_t as_read(std::uint8_t reg) {
    std::uint8_t tx[2] = {static_cast<std::uint8_t>(0x40U | (reg & 0x3FU)), 0}, rx[2] = {0, 0};
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 2, 5);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
    return rx[1];
}

void as_write(std::uint8_t reg, std::uint8_t v) {
    std::uint8_t tx[2] = {static_cast<std::uint8_t>(reg & 0x3FU), v};
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, tx, 2, 5);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);
}

// Tune the 500 kHz antenna tank: LCO / 16 on IRQ, count edges for 100 ms per TUN_CAP step, keep the closest.
std::uint8_t as_tune(std::uint16_t& lco_hz10) {
    g_bank.set_status_bit(crown::kStTuning, true);
    std::uint8_t best = 0;
    std::uint32_t best_err = 0xFFFFFFFFU;
    for (std::uint8_t cap = 0; cap < 16; ++cap) {
        as_write(0x08, static_cast<std::uint8_t>(0x80U | cap));   // DISP_LCO + TUN_CAP
        HAL_Delay(5);
        g_lco_edges = 0;
        g_lco_counting = true;
        HAL_Delay(100);
        g_lco_counting = false;
        const std::uint32_t hz = g_lco_edges * 10U * 16U;
        const std::uint32_t err = hz > 500000U ? hz - 500000U : 500000U - hz;
        if (err < best_err) {
            best_err = err;
            best = cap;
            lco_hz10 = static_cast<std::uint16_t>(hz / 10U);
        }
        HAL_IWDG_Refresh(&hiwdg);
    }
    as_write(0x08, best);
    HAL_Delay(2);
    as_write(0x3D, 0x96);   // CALIB_RCO after changing the tank
    g_bank.set_status_bit(crown::kStTuning, false);
    g_bank.set_status_bit(crown::kStAs3935Tuned, best_err < 17500U);   // within the +/-3.5 % the AS3935 accepts
    return best;
}

bool as_init(std::uint8_t& tun_cap, std::uint16_t& lco_hz10) {
    as_write(0x3C, 0x96);   // PRESET_DEFAULT
    HAL_Delay(2);
    const auto& cfg = g_bank.config();
    as_write(0x00, cfg.as3935_outdoor ? (0x0EU << 1) : (0x12U << 1));   // AFE gain outdoor / indoor
    as_write(0x01, static_cast<std::uint8_t>((2U << 4) | 2U));         // noise floor 2, watchdog 2
    const std::uint8_t ms = cfg.as3935_min_strikes >= 16 ? 3 : cfg.as3935_min_strikes >= 9 ? 2 : cfg.as3935_min_strikes >= 5 ? 1 : 0;
    as_write(0x02, static_cast<std::uint8_t>(0x40U | (ms << 4) | 2U));  // CL_STAT, MIN_NUM_LIGH, SREJ 2
    const bool ok = (as_read(0x00) & 0x3EU) == (cfg.as3935_outdoor ? (0x0EU << 1) : (0x12U << 1));
    g_bank.set_status_bit(crown::kStAs3935Ok, ok);
    if (ok) {
        tun_cap = as_tune(lco_hz10);
    }
    return ok;
}

void as_service() {
    if (!g_as_irq || HAL_GetTick() - g_as_irq_ms < 3) {   // the AS3935 needs 2 ms before INT is readable
        return;
    }
    const bool during_burst = g_burst_active;
    g_as_irq = false;
    const std::uint8_t irq = as_read(0x03) & 0x0FU;
    if (irq == 0x08) {
        const std::uint32_t energy = as_read(0x04) | (static_cast<std::uint32_t>(as_read(0x05)) << 8) |
                                     (static_cast<std::uint32_t>(as_read(0x06) & 0x1FU) << 16);
        g_bank.push_strike(1, as_read(0x07) & 0x3FU, energy, during_burst, HAL_GetTick());
    } else if (irq == 0x04) {
        g_bank.push_strike(2, 0, 0, during_burst, HAL_GetTick());
    } else if (irq == 0x01) {
        g_bank.push_strike(3, 0, 0, during_burst, HAL_GetTick());
    }
}

// ------------------------------------------------------------------ ultrasonic
void set_gain(std::uint8_t code) {
    static const std::uint32_t kGain[5] = {OPAMP_PGA_GAIN_2_OR_MINUS_1, OPAMP_PGA_GAIN_4_OR_MINUS_3,
                                          OPAMP_PGA_GAIN_8_OR_MINUS_7, OPAMP_PGA_GAIN_16_OR_MINUS_15,
                                          OPAMP_PGA_GAIN_32_OR_MINUS_31};
    MODIFY_REG(OPAMP1->CSR, OPAMP_CSR_PGGAIN, kGain[code > 4 ? 4 : code]);
}

// One burst on `path`: returns the samples centred at 0 in `out`.
bool shoot(std::uint8_t path, std::int16_t* out) {
    const std::uint8_t tx = kTx[path], rx = kRx[path];
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, (rx & 1U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, (rx & 2U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    set_gain(g_gain[path]);
    HAL_GPIO_WritePin(GPIOC, kEnPin[tx], GPIO_PIN_SET);
    __HAL_TIM_SET_AUTORELOAD(&htim8, kTimerHz / 40000U - 1U);
    htim8.Instance->RCR = static_cast<std::uint32_t>(g_bank.config().burst_cycles - 1U);
    htim8.Instance->EGR = TIM_EGR_UG;           // load RCR
    HAL_Delay(1);                                 // mux / PGA settle, driver enable
    g_echo_done = false;
    HAL_ADC_Start_DMA(&hadc1, reinterpret_cast<std::uint32_t*>(g_echo_raw), kEchoN);
    __disable_irq();                              // fixed instruction path: sample 0 <-> drive edge offset is constant
    g_burst_active = true;
    htim6.Instance->CNT = 0;
    htim6.Instance->CR1 |= TIM_CR1_CEN;
    htim8.Instance->BDTR |= TIM_BDTR_MOE;
    htim8.Instance->CR1 |= TIM_CR1_CEN;
    __enable_irq();
    const std::uint32_t t0 = HAL_GetTick();
    while (!g_echo_done && HAL_GetTick() - t0 < 5) {
    }
    htim6.Instance->CR1 &= ~TIM_CR1_CEN;
    htim8.Instance->BDTR &= ~TIM_BDTR_MOE;
    g_burst_active = false;
    HAL_GPIO_WritePin(GPIOC, kEnPin[tx], GPIO_PIN_RESET);
    HAL_ADC_Stop_DMA(&hadc1);
    if (!g_echo_done) {
        return false;
    }
    for (std::size_t k = 0; k < kEchoN; ++k) {
        out[k] = static_cast<std::int16_t>(static_cast<int>(g_echo_raw[k]) - crown::dsp::kAdcFullScale);
    }
    return true;
}

void measure_quad(std::uint32_t seq) {
    crown::WindBlock w{};
    w.seq = seq;
    crown::dsp::EchoParams p{};
    p.fs_hz = kFs;
    p.t0_s = 0.0F;
    p.min_tof_s = 150e-6F;     // 96 mm V-path at 50 C and 60 m/s head wind is still > 200 us
    p.max_tof_s = 700e-6F;
    for (std::uint8_t pair = 0; pair < 2; ++pair) {
        const std::uint8_t a = static_cast<std::uint8_t>(2 * pair), b = static_cast<std::uint8_t>(a + 1);
        for (std::uint8_t k = 0; k < 2; ++k) {
            if (!shoot(static_cast<std::uint8_t>(a + k), g_echo[k])) {
                w.flags[a + k] |= crown::kPathNoEcho;
            }
            HAL_IWDG_Refresh(&hiwdg);
        }
        crown::dsp::Echo ea{}, eb{};
        const auto r = crown::dsp::pair_times(g_echo[0], g_echo[1], kEchoN, p, 0.0F, g_scratch, &ea, &eb);
        const auto grade = [&w](std::uint8_t path, const crown::dsp::Echo& e) {
            w.snr_db[path] = static_cast<std::uint8_t>(e.snr_db < 0.0F ? 0.0F : e.snr_db > 99.0F ? 99.0F : e.snr_db);
            if (e.clipped) w.flags[path] |= crown::kPathClipped;
            if (!e.ok) w.flags[path] |= crown::kPathNoEcho;
            g_gain[path] = crown::dsp::next_gain(g_gain[path], e);
        };
        grade(a, ea);
        grade(b, eb);
        w.corr_pct[pair] = static_cast<std::uint8_t>(r.coeff < 0.0F ? 0 : r.coeff * 100.0F);
        if (r.ok) {
            w.tof_ps[a] = static_cast<std::uint32_t>(r.t_ab_s * 1e12F);
            w.tof_ps[b] = static_cast<std::uint32_t>(r.t_ba_s * 1e12F);
            if (r.coeff < 0.8F) {
                w.flags[a] |= crown::kPathLowCorr;
                w.flags[b] |= crown::kPathLowCorr;
            }
        }
    }
    w.gain_code = g_gain[0];
    __disable_irq();
    g_bank.publish_wind(w);
    __enable_irq();
}

void rain_process(const std::uint16_t* s, std::size_t n) {
    crown::RainImpact imp{};
    for (std::size_t i = 0; i < n; ++i) {
        g_rain_us += 20U;
        if (g_rain.add(s[i], g_rain_us, imp)) {
            g_bank.set_now_ms(HAL_GetTick());
            g_bank.push_rain(imp);
        }
    }
}

}  // namespace

// ------------------------------------------------------------------ interrupt handlers / HAL callbacks
extern "C" {
void SysTick_Handler() { HAL_IncTick(); }
void DMA1_Channel1_IRQHandler() { HAL_DMA_IRQHandler(&hdma_adc1); }
void DMA1_Channel2_IRQHandler() { HAL_DMA_IRQHandler(&hdma_adc2); }
void I2C2_EV_IRQHandler() { HAL_I2C_EV_IRQHandler(&hi2c2); }
void I2C2_ER_IRQHandler() { HAL_I2C_ER_IRQHandler(&hi2c2); }
void EXTI9_5_IRQHandler() { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_9); }

void HAL_GPIO_EXTI_Callback(uint16_t pin) {
    if (pin != GPIO_PIN_9) return;
    if (g_lco_counting) {
        ++g_lco_edges;
    } else {
        g_as_irq = true;
        g_as_irq_ms = HAL_GetTick();
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* h) {
    if (h == &hadc1) {
        g_echo_done = true;
    } else {
        rain_process(g_rain_buf + 128, 128);
    }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* h) {
    if (h == &hadc2) {
        rain_process(g_rain_buf, 128);
    }
}

void HAL_I2C_AddrCallback(I2C_HandleTypeDef* h, uint8_t direction, uint16_t) {
    if (direction == I2C_DIRECTION_TRANSMIT) {   // master writes: register pointer (+ payload)
        HAL_I2C_Slave_Seq_Receive_IT(h, g_i2c_rx, sizeof(g_i2c_rx), I2C_FIRST_FRAME);
    } else {                                     // master reads the register selected by the last write
        g_bank.set_now_ms(HAL_GetTick());
        const std::size_t n = g_bank.read(g_i2c_reg, g_i2c_tx, sizeof(g_i2c_tx));
        HAL_I2C_Slave_Seq_Transmit_IT(h, g_i2c_tx, n ? n : 1, I2C_LAST_FRAME);
    }
}

void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef* h) {
    // end of a write transfer (STOP or repeated start): the received count tells register-only vs. register + data
    const std::size_t got = sizeof(g_i2c_rx) - h->XferCount;
    if (got >= 1) {
        g_i2c_reg = g_i2c_rx[0];
        if (got >= 2) {
            g_bank.write(g_i2c_rx, got);
        }
    }
    HAL_I2C_EnableListen_IT(h);
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef* h) { HAL_I2C_ListenCpltCallback(h); }

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* h) {
    if (HAL_I2C_GetError(h) == HAL_I2C_ERROR_AF) {   // NACK at the end of a master read: normal
        HAL_I2C_ListenCpltCallback(h);
        return;
    }
    HAL_I2C_EnableListen_IT(h);
}
}

int main() {
    if (g_boot_flag == kBootMagic) {   // set by kCmdEnterBootloader before the reset
        g_boot_flag = 0;
        jump_to_bootloader();
    }
    HAL_Init();
    clock_init();
    gpio_init();
    tim8_init();
    adc_init();
    spi_init();
    i2c_init();
    watchdog_init();
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)) {
        g_bank.set_status_bit(crown::kStWatchdogReset, true);
    }
    __HAL_RCC_CLEAR_RESET_FLAGS();

    crown::DiagBlock diag{};
    std::uint16_t lco_hz10 = 0;
    as_init(diag.tun_cap, lco_hz10);
    diag.lco_hz10 = lco_hz10;

    std::uint32_t seq = 0, last_quad = 0, last_second = HAL_GetTick(), bursts = 0;
    for (;;) {
        HAL_IWDG_Refresh(&hiwdg);
        const std::uint32_t now = HAL_GetTick();
        const std::uint8_t qps = g_bank.config().quads_per_s;   // 0 = bursts paused
        const std::uint32_t period = qps ? 1000U / qps : 1000U;
        if (qps && now - last_quad >= period) {
            last_quad = now;
            measure_quad(++seq);
            bursts += 4;
        }
        as_service();
        g_rain.set_trigger_mv(static_cast<float>(g_bank.config().rain_trigger_mv));
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, g_bank.irq_level() ? GPIO_PIN_SET : GPIO_PIN_RESET);
        switch (g_bank.take_command()) {
            case crown::kCmdTuneAntenna:
                diag.tun_cap = as_tune(lco_hz10);
                diag.lco_hz10 = lco_hz10;
                break;
            case crown::kCmdReset:
                NVIC_SystemReset();
                break;
            case crown::kCmdEnterBootloader:
                g_boot_flag = kBootMagic;
                NVIC_SystemReset();
                break;
            default:
                break;
        }
        if (now - last_second >= 1000U) {
            last_second = now;
            diag.uptime_s = now / 1000U;
            diag.bursts_per_s = static_cast<std::uint16_t>(bursts);
            bursts = 0;
            __disable_irq();
            g_bank.publish_diag(diag);
            __enable_irq();
        }
    }
}
