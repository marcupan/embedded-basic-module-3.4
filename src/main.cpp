// ═══════════════════════════════════════════════════════════════════════════
// Модуль 3.4 · неблокуючий плеєр на зумері через LEDC, тік 50 мс
// Плата: YD-ESP32-S3 (ESP32-S3-WROOM-1-N16R8) · PlatformIO · Arduino framework
//
//   t — тест зумера: постійні 3,3 В на S (пищить рівно = активний, мовчить = пасивний)
//   m — грати мелодію / зупинити
//   ? — підказка
//
// Без delay і без задач FreeRTOS: червоний LED блимає незалежно від мелодії,
// і це видно: цикл не блокується.
//
// Залізо: map.svg
// ═══════════════════════════════════════════════════════════════════════════
#include <Arduino.h>

namespace {
    // ── 1. Конфігурація ──────────────────────────────────────────────────────────
    struct Config {
        static constexpr uint8_t BUZZER_PIN = 15;
        static constexpr uint8_t LED_PIN = 18;

        static constexpr uint8_t BUZZER_CH = 0;
        static constexpr uint8_t PWM_BITS = 10;
        static constexpr uint32_t TICK_MS = 50; // плеєр думає лише раз на тік
        static constexpr uint32_t LED_HALF_MS = 300;
        static constexpr uint8_t GAP_TICKS = 1; // тиша між нотами, інакше однакові зливаються
    };

    // Частоти нот, Гц (A4 = 440)
    constexpr uint32_t C4 = 262, D4 = 294, E4 = 330, F4 = 349, G4 = 392, A4_NOTE = 440;

    struct Note {
        uint32_t freqHz;
        uint8_t ticks; // скільки тіків по 50 мс звучить
    };

    // Twinkle Twinkle: C C G G A A G | F F E E D D C
    constexpr Note MELODY[] = {
        {.freqHz = C4, .ticks = 7}, {.freqHz = C4, .ticks = 7}, {.freqHz = G4, .ticks = 7}, {.freqHz = G4, .ticks = 7},
        {.freqHz = A4_NOTE, .ticks = 7}, {.freqHz = A4_NOTE, .ticks = 7}, {.freqHz = G4, .ticks = 14},
        {.freqHz = F4, .ticks = 7}, {.freqHz = F4, .ticks = 7}, {.freqHz = E4, .ticks = 7}, {.freqHz = E4, .ticks = 7},
        {.freqHz = D4, .ticks = 7}, {.freqHz = D4, .ticks = 7}, {.freqHz = C4, .ticks = 14},
    };
    constexpr size_t MELODY_LEN = sizeof(MELODY) / sizeof(MELODY[0]);

    // ── 2. Плеєр: машина станів на тіках ─────────────────────────────────────────
    enum class Player : uint8_t { Stopped, Tone, Gap };

    auto player = Player::Stopped;
    size_t noteIndex = 0;
    uint8_t ticksLeft = 0;

    void startNote() {
        const Note &note = MELODY[noteIndex];
        ledcWriteTone(Config::BUZZER_CH, note.freqHz); // частота ноти, duty 50 %
        ticksLeft = note.ticks;
        player = Player::Tone;
        Serial.printf(
            "нота %2u/%u · %lu Гц\n", static_cast<unsigned>(noteIndex + 1),
            static_cast<unsigned>(MELODY_LEN), static_cast<unsigned long>(note.freqHz)
        );
    }

    void stopPlayer() {
        ledcWriteTone(Config::BUZZER_CH, 0);
        player = Player::Stopped;
    }

    void onTick() {
        if (player == Player::Stopped) return;

        if (--ticksLeft > 0) return; // поточна нота або пауза ще триває

        if (player == Player::Tone) {
            ledcWriteTone(Config::BUZZER_CH, 0);
            ticksLeft = Config::GAP_TICKS;
            player = Player::Gap;
            return;
        }

        if (++noteIndex >= MELODY_LEN) {
            // пауза скінчилась: наступна нота або кінець
            stopPlayer();
            Serial.println("── кінець ──");
            return;
        }

        startNote();
    }

    // ── 3. Тест зумера: постійний рівень без ШІМ ─────────────────────────────────
    bool steadyTest = false;

    void toggleSteadyTest() {
        stopPlayer();
        steadyTest = !steadyTest;

        if (steadyTest) {
            ledcDetachPin(Config::BUZZER_PIN);
            pinMode(Config::BUZZER_PIN, OUTPUT);
            digitalWrite(Config::BUZZER_PIN, HIGH);
            Serial.println("── тест: 3,3 В на S. Пищить рівно = активний, мовчить = пасивний. t = вимкнути ──");
        } else {
            digitalWrite(Config::BUZZER_PIN, LOW);
            ledcAttachPin(Config::BUZZER_PIN, Config::BUZZER_CH);
            Serial.println("── тест вимкнено ──");
        }
    }

    // ── 4. Клавіші ───────────────────────────────────────────────────────────────
    void printHelp() {
        Serial.println("t тест активний/пасивний · m мелодія старт/стоп · ? підказка");
    }

    void handleKeys() {
        if (!Serial.available()) return;

        switch (Serial.read()) {
            case 't':
                toggleSteadyTest();
                break;
            case 'm':
                if (steadyTest) toggleSteadyTest();
                if (player == Player::Stopped) {
                    noteIndex = 0;
                    startNote();
                } else {
                    stopPlayer();
                    Serial.println("── стоп ──");
                }
                break;
            case '?':
                printHelp();
                break;
            default:
                break; // Enter, пробіли тощо — ігноруємо
        }
    }
} // namespace

void setup() {
    Serial.begin(115200);
    delay(1500); // час на підключення монітора (рідний USB)

    pinMode(Config::LED_PIN, OUTPUT);

    ledcSetup(Config::BUZZER_CH, 1000, Config::PWM_BITS);
    ledcAttachPin(Config::BUZZER_PIN, Config::BUZZER_CH);
    ledcWriteTone(Config::BUZZER_CH, 0);

    printHelp();
}

void loop() {
    const uint32_t now = millis();
    static uint32_t lastTick = 0;
    static uint32_t lastLed = 0;
    static bool ledOn = false;

    handleKeys();

    if (now - lastTick >= Config::TICK_MS) {
        lastTick += Config::TICK_MS; // крок від попереднього тіку: похибка не накопичується
        onTick();
    }

    if (now - lastLed >= Config::LED_HALF_MS) {
        lastLed = now;
        ledOn = !ledOn;
        digitalWrite(Config::LED_PIN, ledOn ? HIGH : LOW);
    }
}
