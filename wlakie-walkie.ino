#include <ESP32Servo.h>

// Пины сервоприводов для всех 4 ног
const int SERVO_PINS[4][2] = {
  {21, 19}, // Передний правый: горизонтальный, вертикальный
  {33, 25}, // Задний правый: горизонтальный, вертикальный  
  {27, 14}, // Задний левый: горизонтальный, вертикальный
  {12, 13}  // Передний левый: горизонтальный, вертикальный
};

// Диапазоны движения (в градусах)
const int VERTICAL_RANGE = 60;    // ±30° от центра
const int HORIZONTAL_RANGE = 90;  // ±45° от центра

// Центральные положения
const int CENTER_POS = 90;

// Переменные для сервоприводов
Servo servos[4][2]; // [нога][тип: 0-горизонтальный, 1-вертикальный]

// Параметры анимации
const int ANIMATION_STEPS = 60;    // Количество шагов для полного цикла
const int PHASE_SHIFT = ANIMATION_STEPS / 2; // Сдвиг фаз 0.5 (половина цикла)

unsigned long previousMillis = 0;
const long INTERVAL = 10; // Интервал между шагами (мс)
int currentStep = 0;

// Фазы для каждой ноги (в шагах) - ДИАГОНАЛЬНЫЕ ПАРЫ
const int LEG_PHASES[4] = {
  0,           // Передний правый (диагональ 1)
  PHASE_SHIFT, // Задний правый (диагональ 2) 
  0,           // Задний левый (диагональ 1) - ОДИНАКОВАЯ фаза с передним правым!
  PHASE_SHIFT  // Передний левый (диагональ 2) - ОДИНАКОВАЯ фаза с задним правым!
};

// Направление для горизонтальных сервоприводов (1 = прямое, -1 = обратное)
const int HORIZONTAL_DIRECTION[4] = {
  1,  // Передний правый
  1,  // Задний правый
  -1, // Задний левый (зеркально)
  -1  // Передний левый (зеркально)
};

void setup() {
  // Инициализация всех сервоприводов
  for (int leg = 0; leg < 4; leg++) {
    servos[leg][0].attach(SERVO_PINS[leg][0]); // Горизонтальный
    servos[leg][1].attach(SERVO_PINS[leg][1]); // Вертикальный
    
    // Установка в исходное положение
    servos[leg][0].write(CENTER_POS);
    servos[leg][1].write(CENTER_POS);
  }
  
  delay(1000); // Пауза для стабилизации
  
  Serial.begin(115200); // Для отладки
}

void loop() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - previousMillis >= INTERVAL) {
    previousMillis = currentMillis;
    
    // Управление всеми ногами
    for (int leg = 0; leg < 4; leg++) {
      moveLeg(leg, currentStep);
    }
    
    // Отладка каждые 10 шагов
    if (currentStep % 10 == 0) {
      debugAllPositions(currentStep);
    }
    
    // Переход к следующему шагу
    currentStep = (currentStep + 1) % ANIMATION_STEPS;
  }
}

void moveLeg(int leg, int step) {
  // Вычисление фазированного шага для текущей ноги
  int phasedStep = (step + LEG_PHASES[leg]) % ANIMATION_STEPS;
  
  // Нормализованная позиция в цикле [0, 1)
  float t = (float)phasedStep / ANIMATION_STEPS;
  
  // Вычисление позиции горизонтального серво (вперед/назад)
  // Синусоида для плавного движения с учетом направления
  float horizontalAngle = CENTER_POS - 
                         HORIZONTAL_DIRECTION[leg] * HORIZONTAL_RANGE/2 * sin(t * 2 * PI);
  
  // Вычисление позиции вертикального серво (подъем/опускание)
  // Косинусоида для вертикального движения (подъем в положительной полуволне)
  float verticalAngle = CENTER_POS + VERTICAL_RANGE/2 * cos(t * 2 * PI);
  
  // Ограничение углов
  horizontalAngle = constrain(horizontalAngle, CENTER_POS - HORIZONTAL_RANGE/2, CENTER_POS + HORIZONTAL_RANGE/2);
  verticalAngle = constrain(verticalAngle, CENTER_POS - VERTICAL_RANGE/2, CENTER_POS + VERTICAL_RANGE/2);
  
  // Установка позиций сервоприводов
  servos[leg][0].write((int)horizontalAngle); // Горизонтальный
  servos[leg][1].write((int)verticalAngle);   // Вертикальный
}

// Функция для отладки
void debugAllPositions(int step) {
  Serial.print("Step: ");
  Serial.println(step);
  
  for (int leg = 0; leg < 4; leg++) {
    int phasedStep = (step + LEG_PHASES[leg]) % ANIMATION_STEPS;
    float t = (float)phasedStep / ANIMATION_STEPS;
    
    float horizontalAngle = CENTER_POS + 
                           HORIZONTAL_DIRECTION[leg] * HORIZONTAL_RANGE/2 * sin(t * 2 * PI);
    float verticalAngle = CENTER_POS + VERTICAL_RANGE/2 * cos(t * 2 * PI);
    
    Serial.print("Leg ");
    Serial.print(leg);
    Serial.print(" (phase ");
    Serial.print(LEG_PHASES[leg]);
    Serial.print("): H=");
    Serial.print(horizontalAngle);
    Serial.print(" V=");
    Serial.println(verticalAngle);
  }
  Serial.println();
}