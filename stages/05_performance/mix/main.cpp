#include <cstdint>
#include "main.h"
#include <stdio.h>
#include <string.h>

#include "hal/hal.hpp"
#include "drv/led.hpp"

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim3;
UART_HandleTypeDef huart2;

void error_handler();
void init_gpio ();
static void MX_TIM1_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART2_UART_Init(void);

constexpr std::uint32_t HSE_FREQ = 16000000; // 16MHz external oscillator

constexpr hal::systick::SystickConfig systick_config {
    .prio = 0,
    .core_clock_freq = hal::rcc::HSI_FREQ,
    .systick_freq = hal::systick::tick_frequencies::freq_1kHz
};

constexpr hal::rcc::OscInitConfig oscillator_config {
    .hse_state = hal::rcc::hse_states::noconf,
    .lse_state = hal::rcc::lse_states::noconf,
    .hsi_state = hal::rcc::hsi_states::on,
    .hsi_calib_value = hal::rcc::hsi_calibration_default,
    .hsi14_state = hal::rcc::hsi14_states::on,
    .hsi14_calib_value = 16,
    .lsi_state = hal::rcc::lsi_states::noconf,
    .pll = {
        .pll_state = hal::rcc::pll_states::noconf,
        .pll_source = hal::rcc::pll_sources::hsi,
        .pll_mul = hal::rcc::pll_mul_factors::mul2,
        .pll_div = hal::rcc::pll_prediv_factors::div1
    }
};

constexpr hal::rcc::ClkInitConfig clock_config {
    .is_sysclk = true,
    .is_hclk = true,
    .is_pclk1 = true,
    .system_clock_source = hal::rcc::system_clock_sources::hsi,
    .ahb_clk_div = hal::rcc::ahb_clk_dividers::div1,
    .hclk_div = hal::rcc::hclk_dividers::div1
};

constexpr hal::adc::AdcInitConfig adc_config {
    .clock_prescaler = hal::adc::clock_prescalers::async_div_1,
    .resolution = hal::adc::resolutions::res_8_bit,
    .data_alignment = hal::adc::data_alignments::right,
    .scan_direction = hal::adc::scan_directions::forward,
    .eoc_selection = hal::adc::eoc_selections::single,
    .low_power_auto_wait_enabled = false,
    .low_power_auto_power_off_enabled = false,
    .conversion_mode = hal::adc::conversion_modes::continuous,
    .external_trigger = hal::adc::external_triggers::software,
    .external_trigger_edge = hal::adc::external_trigger_edges::none,
    .dma_continuous_request_enabled = false,
    .overrun_behavior = hal::adc::overrun_behaviors::preserve,
    .sample_time_cycle = hal::adc::sample_time_cycles::cycles_1_5
};

constexpr std::uint32_t flash_latency { 0 };

GPIO_TypeDef* ports [8] = { GPIOB, GPIOB, GPIOB, GPIOB, GPIOB, GPIOA, GPIOA, GPIOA };
uint16_t pins [8] = { GPIO_PIN_5, GPIO_PIN_6, GPIO_PIN_7, GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_4 };
void set_leds (uint32_t val) {
	for (int i = 0; i < 8; ++i) {
		if (val >= (0x000000ff / 8) * (i + 1)) {
			HAL_GPIO_WritePin(ports[i], pins[i], GPIO_PIN_SET);
		}
		else {
			HAL_GPIO_WritePin(ports[i], pins[i], GPIO_PIN_RESET);
		}
	}
}

#define PRINT_REG(REG) do { \
    char msg[50] = {0}; \
    snprintf(msg, sizeof(msg), #REG ": %lu\r\n", (unsigned long)(REG)); \
    HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY); \
} while (0)

// record : 7660

int main (void) {
    if (hal::init<systick_config, oscillator_config, clock_config, flash_latency, HSE_FREQ>() != hal::hal_error::ok) {
        error_handler();
    }

    init_gpio();
    hal::adc::CAdc<hal::adc::adc_instances::adc1, adc_config> adc1 {};
    adc1.init<hal::gpio::ports::port_a, hal::gpio::pins::pin_06, hal::gpio::pins::pin_07>();
    MX_TIM3_Init();
    MX_USART2_UART_Init();
    MX_TIM1_Init();

    hal::gpio::CPin<hal::gpio::ports::port_c, hal::gpio::pins::pin_13> gpio {};
    drv::CLed<decltype(gpio)> led { &gpio };

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    while (1)
    {
        __HAL_TIM_SET_COUNTER(&htim1, 0);
        HAL_TIM_Base_Start(&htim1);

        adc1.select_channel<hal::adc::channels::channel_6>();
        adc1.start();
        adc1.poll_for_conversion(HAL_MAX_DELAY);
        uint32_t raw6 = adc1.get();
        set_leds(raw6);
        adc1.stop();

        led.toggle();

        adc1.select_channel<hal::adc::channels::channel_7>();
        adc1.start();
        adc1.poll_for_conversion(HAL_MAX_DELAY);
        uint32_t raw7 = adc1.get();
        TIM3->CCR1 = (uint32_t)((uint64_t)raw7 * 0xFFFFFFFF / 255);
        adc1.stop();

        led.toggle();

        HAL_TIM_Base_Stop(&htim1);
        std::uint32_t ticks = __HAL_TIM_GET_COUNTER(&htim1);
        char msg[50] = {0};
        snprintf(msg, sizeof(msg), "Time taken: %lu ticks\r\n", ticks);
        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
        snprintf(msg, sizeof(msg), "ADC #1: %lu\r\n", raw6);
        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);
        snprintf(msg, sizeof(msg), "ADC #2: %lu\r\n", raw7);
        HAL_UART_Transmit(&huart2, (uint8_t*)msg, strlen(msg), HAL_MAX_DELAY);

        hal::systick::CSysTick::delay_ms(1000);
    }
}

void init_gpio () {
    hal::rcc::CRcc::enable_gpio_clock<hal::gpio::ports::port_a>();
    hal::rcc::CRcc::enable_gpio_clock<hal::gpio::ports::port_b>();
    hal::rcc::CRcc::enable_gpio_clock<hal::gpio::ports::port_c>();

    hal::gpio::CPort<hal::gpio::ports::port_a>::reset<
        hal::gpio::pins::pin_00,
        hal::gpio::pins::pin_01,
        hal::gpio::pins::pin_04,
        hal::gpio::pins::pin_08,
        hal::gpio::pins::pin_09,
        hal::gpio::pins::pin_10,
        hal::gpio::pins::pin_11,
        hal::gpio::pins::pin_12
    >();

    hal::gpio::CPort<hal::gpio::ports::port_b>::reset<
        hal::gpio::pins::pin_02,
        hal::gpio::pins::pin_05,
        hal::gpio::pins::pin_06,
        hal::gpio::pins::pin_07,
        hal::gpio::pins::pin_08,
        hal::gpio::pins::pin_09,
        hal::gpio::pins::pin_10,
        hal::gpio::pins::pin_11,
        hal::gpio::pins::pin_12,
        hal::gpio::pins::pin_13,
        hal::gpio::pins::pin_14,
        hal::gpio::pins::pin_15
    >();

    hal::gpio::CPort<hal::gpio::ports::port_a>::reset<hal::gpio::pins::pin_13>();

    constexpr hal::gpio::GpioInitConfig gpio_init {
        .mode = hal::gpio::modes::output,
        .output_type = hal::gpio::output_types::push_pull,
        .speed = hal::gpio::speeds::low,
        .pull_type = hal::gpio::pull_types::none
    };

    hal::gpio::CPort<hal::gpio::ports::port_a>::configure_pins<gpio_init, 
        hal::gpio::pins::pin_00,
        hal::gpio::pins::pin_01,
        hal::gpio::pins::pin_04,
        hal::gpio::pins::pin_08,
        hal::gpio::pins::pin_09,
        hal::gpio::pins::pin_10,
        hal::gpio::pins::pin_11,
        hal::gpio::pins::pin_12
    >();

    hal::gpio::CPort<hal::gpio::ports::port_b>::configure_pins<gpio_init, 
        hal::gpio::pins::pin_02,
        hal::gpio::pins::pin_05,
        hal::gpio::pins::pin_06,
        hal::gpio::pins::pin_07,
        hal::gpio::pins::pin_08,
        hal::gpio::pins::pin_09,
        hal::gpio::pins::pin_10,
        hal::gpio::pins::pin_11,
        hal::gpio::pins::pin_12,
        hal::gpio::pins::pin_13,
        hal::gpio::pins::pin_14,
        hal::gpio::pins::pin_15,
        hal::gpio::pins::pin_12
    >();

    hal::gpio::CPort<hal::gpio::ports::port_c>::configure_pins<gpio_init, hal::gpio::pins::pin_13>();
}

static void MX_TIM1_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 0;
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = 65535;
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
    {
        Error_Handler();
    }
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
    {
        Error_Handler();
    }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_TIM3_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 0;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 65535;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
    {
        Error_Handler();
    }
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
    {
        Error_Handler();
    }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
    HAL_TIM_MspPostInit(&htim3);
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
}

void error_handler() {
    __disable_irq();
    while (true) {
    }
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}