#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2818b.pio.h" // Biblioteca PIO gerada para os WS2812

// ------------------------- Definições dos Pinos -------------------------
#define LED_MATRIX_PIN 7 // WS2812 (matriz 5x5)
#define LED_RED_PIN 13   // LED vermelho do LED RGB
#define BUTTON_A_PIN 5   // Botão A (incrementa)
#define BUTTON_B_PIN 6   // Botão B (decrementa)

#define NUM_LEDS 25        // 5x5
#define DEBOUNCE_US 100000 // 100 ms em microsegundos

// ------------------------- Variáveis Globais -------------------------
volatile int current_digit = 0;      // Dígito atual (0-9)
volatile bool display_update = true; // Flag para atualizar a matriz

// Variáveis para debouncing (armazenadas em microsegundos)
volatile uint64_t last_button_a_time = 0;
volatile uint64_t last_button_b_time = 0;

// ------------------------- Prototipos de Funções -------------------------
void draw_digit(int digit);
int getIndex(int x, int y); // Função para converter coordenadas (x,y) para índice no vetor

// Função de callback para as interrupções dos botões
void gpio_callback(uint gpio, uint32_t events);

// ------------------------- Funções de Controle dos WS2812 -------------------------

// Estrutura para um pixel (formato GRB)
typedef struct
{
    uint8_t G, R, B;
} pixel_t;

pixel_t leds[NUM_LEDS];

PIO ws2812_pio;
uint sm; // state machine

// Inicializa o PIO para controle dos WS2812
void npInit(uint pin)
{
    uint offset = pio_add_program(pio0, &ws2818b_program);
    ws2812_pio = pio0;
    sm = pio_claim_unused_sm(ws2812_pio, true);
    ws2818b_program_init(ws2812_pio, sm, offset, pin, 800000);
    // Inicializa o buffer apagado
    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

// Define a cor de um LED na posição index
void npSetLED(uint index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < NUM_LEDS)
    {
        leds[index].R = r;
        leds[index].G = g;
        leds[index].B = b;
    }
}

// Limpa (apaga) todos os LEDs
void npClear()
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        npSetLED(i, 0, 0, 0);
    }
}

// Envia os dados do buffer para os LEDs
void npWrite()
{
    for (int i = 0; i < NUM_LEDS; i++)
    {
        pio_sm_put_blocking(ws2812_pio, sm, leds[i].G);
        pio_sm_put_blocking(ws2812_pio, sm, leds[i].R);
        pio_sm_put_blocking(ws2812_pio, sm, leds[i].B);
    }
    sleep_us(100); // Tempo para reset (segundo datasheet)
}

// Mapeamento de coordenadas (x,y) para índice no vetor de LEDs
// Adota a variação “zig-zag” conforme o exemplo do BitDogLab
int getIndex(int x, int y)
{
    // Se a linha for par (0,2,4): da esquerda para a direita
    // Se ímpar: da direita para a esquerda
    if (y % 2 == 0)
    {
        return 24 - (y * 5 + x);
    }
    else
    {
        return 24 - (y * 5 + (4 - x));
    }
}

// ------------------------- Função para desenhar dígitos -------------------------

// Padrões para dígitos 0 a 9 – cada um é uma matriz 5x5 (1 = LED ligado; 0 = apagado)
// Você pode modificar os padrões para um estilo “digital” ou criativo
const int digit_patterns[10][5][5] = {
    // 0
    {{1, 1, 1, 1, 1},
     {1, 0, 0, 0, 1},
     {1, 0, 0, 0, 1},
     {1, 0, 0, 0, 1},
     {1, 1, 1, 1, 1}},
    // 1
    {{0, 0, 1, 0, 0},
     {0, 1, 1, 0, 0},
     {1, 0, 1, 0, 0},
     {0, 0, 1, 0, 0},
     {1, 1, 1, 1, 1}},
    // 2
    {{1, 1, 1, 1, 1},
     {0, 0, 0, 0, 1},
     {1, 1, 1, 1, 1},
     {1, 0, 0, 0, 0},
     {1, 1, 1, 1, 1}},
    // 3
    {{1, 1, 1, 1, 1},
     {0, 0, 0, 0, 1},
     {0, 1, 1, 1, 1},
     {0, 0, 0, 0, 1},
     {1, 1, 1, 1, 1}},
    // 4
    {{1, 0, 0, 1, 0},
     {1, 0, 0, 1, 0},
     {1, 1, 1, 1, 1},
     {0, 0, 0, 1, 0},
     {0, 0, 0, 1, 0}},
    // 5
    {{1, 1, 1, 1, 1},
     {1, 0, 0, 0, 0},
     {1, 1, 1, 1, 1},
     {0, 0, 0, 0, 1},
     {1, 1, 1, 1, 1}},
    // 6
    {{1, 1, 1, 1, 1},
     {1, 0, 0, 0, 0},
     {1, 1, 1, 1, 1},
     {1, 0, 0, 0, 1},
     {1, 1, 1, 1, 1}},
    // 7
    {{1, 1, 1, 1, 1},
     {0, 0, 0, 0, 1},
     {0, 0, 0, 1, 0},
     {0, 0, 1, 0, 0},
     {0, 1, 0, 0, 0}},
    // 8
    {{1, 1, 1, 1, 1},
     {1, 0, 0, 0, 1},
     {1, 1, 1, 1, 1},
     {1, 0, 0, 0, 1},
     {1, 1, 1, 1, 1}},
    // 9
    {{1, 1, 1, 1, 1},
     {1, 0, 0, 0, 1},
     {1, 1, 1, 1, 1},
     {0, 0, 0, 0, 1},
     {1, 1, 1, 1, 1}}};

/// Desenha o dígito (0-9) na matriz de LEDs, acendendo os pixels em branco (RGB = 255,255,255)
void draw_digit(int digit)
{
    if (digit < 0 || digit > 9)
        return;
    npClear();
    for (int y = 0; y < 5; y++)
    {
        for (int x = 0; x < 5; x++)
        {
            if (digit_patterns[digit][y][x] == 1)
            {
                int pos = getIndex(x, y);
                npSetLED(pos, 255, 255, 255);
            }
        }
    }
    npWrite();
}

// ------------------------- Rotina de Interrupção dos Botões -------------------------

void gpio_callback(uint gpio, uint32_t events)
{
    uint64_t now = time_us_64();
    // Botão A: incremento
    if (gpio == BUTTON_A_PIN)
    {
        if (now - last_button_a_time < DEBOUNCE_US)
            return; // ignora se for bouncing
        last_button_a_time = now;
        current_digit = (current_digit + 1) % 10;
        display_update = true;
    }
    // Botão B: decremento
    else if (gpio == BUTTON_B_PIN)
    {
        if (now - last_button_b_time < DEBOUNCE_US)
            return;
        last_button_b_time = now;
        current_digit = (current_digit + 9) % 10; // decremento com wrap-around (0 -> 9)
        display_update = true;
    }
}

// ------------------------- Função Principal -------------------------

int main()
{
    stdio_init_all();

    // Inicializa a matriz de LEDs WS2812
    npInit(LED_MATRIX_PIN);
    npClear();
    npWrite();

    // Configura o LED vermelho (LED RGB) como saída
    gpio_init(LED_RED_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);
    gpio_put(LED_RED_PIN, 0);

    // Configura os botões com resistor de pull-up interno
    gpio_init(BUTTON_A_PIN);
    gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_A_PIN);

    gpio_init(BUTTON_B_PIN);
    gpio_set_dir(BUTTON_B_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_B_PIN);

    // Configura interrupções para os botões: borda de descida (quando o botão é pressionado)
    gpio_set_irq_enabled(BUTTON_A_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(BUTTON_B_PIN, GPIO_IRQ_EDGE_FALL, true);
    // Registra a rotina de callback (única para os dois botões)
    gpio_set_irq_callback(gpio_callback);
    // Habilita as interrupções do IO_BANK0 na NVIC
    irq_set_enabled(IO_IRQ_BANK0, true);
    // Variáveis para controle do piscar do LED vermelho
    uint64_t last_blink_time = time_us_64();
    bool led_state = false;
    const uint32_t BLINK_INTERVAL_US = 100000; // 100ms: LED alterna a cada 100ms → 5 ciclos por segundo

    // Desenha inicialmente o dígito 0
    draw_digit(current_digit);
    display_update = false;

    while (true)
    {
        // Atualiza a matriz se a flag estiver ativa
        if (display_update)
        {
            draw_digit(current_digit);
            display_update = false;
        }
        // Controle do piscar do LED vermelho
        uint64_t now = time_us_64();
        if (now - last_blink_time >= BLINK_INTERVAL_US)
        {
            led_state = !led_state;
            gpio_put(LED_RED_PIN, led_state);
            last_blink_time = now;
        }
        // Pequena pausa para não sobrecarregar o laço
        sleep_ms(10);
    }
    return 0;
}
