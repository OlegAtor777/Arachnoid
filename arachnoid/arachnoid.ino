#include <Bluepad32.h>
#include <ESP32Servo.h>

// ========= КОНСТАНТЫ И ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ========= //

// Пины сервоприводов для всех 4 ног
int SERVO_PINS[4][2] = {
  {21, 19}, // Передний правый: горизонтальный, вертикальный
  {33, 25}, // Задний правый: горизонтальный, вертикальный  
  {27, 14}, // Задний левый: горизонтальный, вертикальный
  {12, 13}  // Передний левый: горизонтальный, вертикальный
};

// Диапазоны движения (в градусах)
int VERTICAL_RANGE = 50;    // ±30° от центра
float HORIZONTAL_RANGE = 110;  // ±45° от центра

// Центральные положения
int CENTER_POS = 90;

// Переменные для сервоприводов
Servo servos[4][2]; // [нога][тип: 0-горизонтальный, 1-вертикальный]

// Параметры анимации
int ANIMATION_STEPS = 60;    // Количество шагов для полного цикла
int PHASE_SHIFT = ANIMATION_STEPS / 2; // Сдвиг фаз 0.5 (половина цикла)
//int PHASE_SHIFT = ANIMATION_STEPS / 4; // Сдвиг фаз 0.25 (четверть цикла)

// Фазы для каждой ноги (в шагах) - ДИАГОНАЛЬНЫЕ ПАРЫ
int LEG_PHASES[4] = {
  0,           // Передний правый (диагональ 1)
  PHASE_SHIFT, // Задний правый (диагональ 2) 
  0,           // Задний левый (диагональ 1) - ОДИНАКОВАЯ фаза с передним правым!
  PHASE_SHIFT  // Передний левый (диагональ 2) - ОДИНАКОВАЯ фаза с задним правым!
};

// Направление для горизонтальных сервоприводов (1 = прямое, -1 = обратное)
volatile int HORIZONTAL_DIRECTION[4] = {
  1,  // Передний правый
  1,  // Задний правый
  -1, // Задний левый (зеркально)
  -1  // Передний левый (зеркально)
};

// ========= FreeRTOS ЗАДАЧИ И СИНХРОНИЗАЦИЯ ========= //

// Семафор для синхронизации доступа к глобальным переменным
SemaphoreHandle_t xControllerMutex;

// Флаги состояния
volatile bool isWalking = false;  // Флаг ходьбы
volatile bool isUpButtonPressed = false; // Флаг нажатия кнопки "вверх"
volatile bool isSportMode = false;

// Дескрипторы задач
TaskHandle_t xGamepadTaskHandle = NULL;
TaskHandle_t xWalkingTaskHandle = NULL;
TaskHandle_t xServoTaskHandle = NULL;

// Глобальные переменные для анимации
volatile int currentStep = 0;
unsigned long previousMillis = 0;
volatile long INTERVAL = 25; // Интервал между шагами (мс)

ControllerPtr myControllers[BP32_MAX_GAMEPADS];

// ========= CALLBACK ФУНКЦИИ Bluepad32 ========= //

void onConnectedController(ControllerPtr ctl) {
  bool foundEmptySlot = false;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == nullptr) {
      Serial.printf("CALLBACK: Controller is connected, index=%d\n", i);
      ControllerProperties properties = ctl->getProperties();
      Serial.printf("Controller model: %s, VID=0x%04x, PID=0x%04x\n", 
                    ctl->getModelName().c_str(), properties.vendor_id, properties.product_id);
      myControllers[i] = ctl;
      foundEmptySlot = true;
      break;
    }
  }

  if (!foundEmptySlot) {
    Serial.println("CALLBACK: Controller connected, but could not found empty slot");
  }
}

void onDisconnectedController(ControllerPtr ctl) {
  bool foundController = false;

  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == ctl) {
      Serial.printf("CALLBACK: Controller disconnected from index=%d\n", i);
      myControllers[i] = nullptr;
      foundController = true;
      break;
    }
  }

  if (!foundController) {
    Serial.println("CALLBACK: Controller disconnected, but not found in myControllers");
  }
}

// ========= ФУНКЦИИ ДВИЖЕНИЯ НОГ ========= //

void moveLeg(int leg, int step) {
  // Вычисление фазированного шага для текущей ноги
  int phasedStep = (step + LEG_PHASES[leg]) % ANIMATION_STEPS;
  
  // Нормализованная позиция в цикле [0, 1)
  float t = (float)phasedStep / ANIMATION_STEPS;
  
  // Вычисление позиции горизонтального серво (вперед/назад)
  float horizontalAngle = CENTER_POS - 
                         HORIZONTAL_DIRECTION[leg] * HORIZONTAL_RANGE/2 * sin(t * 2 * PI);
  
  // Вычисление позиции вертикального серво (подъем/опускание)
  float verticalAngle = CENTER_POS + VERTICAL_RANGE/2 * cos(t * 2 * PI);
  
  // Ограничение углов
  horizontalAngle = constrain(horizontalAngle, CENTER_POS - HORIZONTAL_RANGE/2, CENTER_POS + HORIZONTAL_RANGE/2);
  verticalAngle = constrain(verticalAngle, CENTER_POS - VERTICAL_RANGE/2, CENTER_POS + VERTICAL_RANGE/2);
  
  // Установка позиций сервоприводов
  servos[leg][0].write((int)horizontalAngle); // Горизонтальный
  servos[leg][1].write((int)verticalAngle);   // Вертикальный
}

// ========= FreeRTOS ЗАДАЧИ ========= //

// ЗАДАЧА 1: Обработка геймпада
void GamepadTask(void *parameter) {
  Serial.println("GamepadTask started");
  
  while (1) {
    // Обработка данных от контроллеров
    bool dataUpdated = BP32.update();
    
    if (dataUpdated) {
      // Захватываем мьютекс для безопасного доступа к переменным
      if (xSemaphoreTake(xControllerMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        
        bool upButtonPressed = false;
        
        // Проверяем все подключенные контроллеры
        for (auto myController : myControllers) {
          if (myController && myController->isConnected() && myController->hasData()) {
            if (myController->isGamepad()) {
              // Проверяем нажата ли кнопка "вверх" (0x01)
              if (myController->dpad() == 0x01) {
                upButtonPressed = true;

                HORIZONTAL_DIRECTION[0] = 1;
                HORIZONTAL_DIRECTION[1] = 1;
                HORIZONTAL_DIRECTION[2] = -1;
                HORIZONTAL_DIRECTION[3] = -1;
                break; // Достаточно одного нажатого контроллера
              }
              // Проверяем нажата ли кнопка "вниз" (0x02)
              if (myController->dpad() == 0x02) {
                upButtonPressed = true;

                HORIZONTAL_DIRECTION[0] = -1;
                HORIZONTAL_DIRECTION[1] = -1;
                HORIZONTAL_DIRECTION[2] = 1;
                HORIZONTAL_DIRECTION[3] = 1;  
                break; // Достаточно одного нажатого контроллера
              }
              // Проверяем нажата ли кнопка "влево" (0x08)
              if (myController->dpad() == 0x08) {
                upButtonPressed = true;

                HORIZONTAL_DIRECTION[0] = 1;
                HORIZONTAL_DIRECTION[1] = -1;
                HORIZONTAL_DIRECTION[2] = -1;
                HORIZONTAL_DIRECTION[3] = 1;  
                break; // Достаточно одного нажатого контроллера
              }
              // Проверяем нажата ли кнопка "вправо" (0x04)
              if (myController->dpad() == 0x04) {
                upButtonPressed = true;

                HORIZONTAL_DIRECTION[0] = -1;
                HORIZONTAL_DIRECTION[1] = 1;
                HORIZONTAL_DIRECTION[2] = 1;
                HORIZONTAL_DIRECTION[3] = -1;  
                break; // Достаточно одного нажатого контроллера
              }

              if (myController -> buttons() == 0x01) {
                isSportMode = !isSportMode;
                if (isSportMode == 0) {
                  INTERVAL = 25;
                  break;
                }
                else INTERVAL = 15;
                break;
              }
                //== RIGHT JOYSTICK - LEFT ==//
              if (myController->axisRX() <= -25) {
                upButtonPressed = true;

                HORIZONTAL_DIRECTION[0] = 1;
                HORIZONTAL_DIRECTION[1] = 1;
                HORIZONTAL_DIRECTION[2] = 1;
                HORIZONTAL_DIRECTION[3] = 1;  
                break; // Достаточно одного нажатого контроллера
              }
            
              //== RIGHT JOYSTICK - RIGHT ==//
              if (myController->axisRX() >= 25) {
                upButtonPressed = true;

                HORIZONTAL_DIRECTION[0] = -1;
                HORIZONTAL_DIRECTION[1] = -1;
                HORIZONTAL_DIRECTION[2] = -1;
                HORIZONTAL_DIRECTION[3] = -1;  
                break; // Достаточно одного нажатого контроллераright
              }
            }
          }
        }
        
        // Обновляем флаг нажатия кнопки
        isUpButtonPressed = upButtonPressed;
        
        // Освобождаем мьютекс
        xSemaphoreGive(xControllerMutex);
      }
    }
    
    // Короткая пауза для других задач
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ЗАДАЧА 2: Управление ходьбой
void WalkingTask(void *parameter) {
  Serial.println("WalkingTask started");
  
  bool lastWalkingState = false;
  
  while (1) {
    // Захватываем мьютекс для чтения состояния
    if (xSemaphoreTake(xControllerMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      bool shouldWalk = isUpButtonPressed;
      xSemaphoreGive(xControllerMutex);
      
      // Если состояние изменилось
      if (shouldWalk != lastWalkingState) {
        // Захватываем мьютекс для изменения состояния ходьбы
        if (xSemaphoreTake(xControllerMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
          isWalking = shouldWalk;
          xSemaphoreGive(xControllerMutex);
          
          if (shouldWalk) {
            Serial.println("Starting to walk");
            // Сбрасываем шаг анимации при начале ходьбы
            currentStep = 0;
            previousMillis = millis();
          } else {
            Serial.println("Stopping walk");
            // Устанавливаем сервоприводы в исходное положение
            for (int leg = 0; leg < 4; leg++) {
              servos[leg][0].write(CENTER_POS);
              servos[leg][1].write(CENTER_POS);
            }
          }
          
          lastWalkingState = shouldWalk;
        }
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(20)); // Проверяем состояние каждые 20 мс
  }
}

// ЗАДАЧА 3: Анимация ходьбы
void ServoAnimationTask(void *parameter) {
  Serial.println("ServoAnimationTask started");
  
  while (1) {
    // Проверяем, нужно ли выполнять анимацию
    bool walking = false;
    if (xSemaphoreTake(xControllerMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      walking = isWalking;
      xSemaphoreGive(xControllerMutex);
    }
    
    if (walking) {
      unsigned long currentMillis = millis();
      
      if (currentMillis - previousMillis >= INTERVAL) {
        previousMillis = currentMillis;
        
        // Управление всеми ногами
        for (int leg = 0; leg < 4; leg++) {
          moveLeg(leg, currentStep);
        }
        
        // Переход к следующему шагу
        currentStep = (currentStep + 1) % ANIMATION_STEPS;
      }
    }
    
    // Короткая пауза для других задач
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ========= ARDUINO SETUP ========= //

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Spider Robot with FreeRTOS ===");
  
  // Инициализация всех сервоприводов
  Serial.println("Initializing servos...");
  for (int leg = 0; leg < 4; leg++) {
    servos[leg][0].attach(SERVO_PINS[leg][0]); // Горизонтальный
    servos[leg][1].attach(SERVO_PINS[leg][1]); // Вертикальный
    
    // Установка в исходное положение
    servos[leg][0].write(CENTER_POS);
    servos[leg][1].write(CENTER_POS);
  }
  
  delay(1000); // Пауза для стабилизации сервоприводов
  
  // Инициализация Bluepad32
  Serial.printf("Bluepad32 Firmware: %s\n", BP32.firmwareVersion());
  const uint8_t* addr = BP32.localBdAddress();
  Serial.printf("Bluetooth Address: %2X:%2X:%2X:%2X:%2X:%2X\n", 
                addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
  
  // Настройка колбэков Bluepad32
  BP32.setup(&onConnectedController, &onDisconnectedController);
  
  // Опционально: сброс сохраненных ключей Bluetooth
  BP32.forgetBluetoothKeys();
  
  // Отключение виртуальных устройств
  BP32.enableVirtualDevice(false);
  
  // Создание мьютекса для синхронизации
  xControllerMutex = xSemaphoreCreateMutex();
  if (xControllerMutex == NULL) {
    Serial.println("Error: Failed to create mutex!");
    while(1); // Остановка при ошибке
  }
  
  // СОЗДАНИЕ ЗАДАЧ FreeRTOS
  
  // Задача 1: Обработка геймпада (самый высокий приоритет)
  xTaskCreatePinnedToCore(
    GamepadTask,           // Функция задачи
    "GamepadTask",         // Имя задачи
    4096,                  // Размер стека
    NULL,                  // Параметры
    3,                     // Приоритет (высокий)
    &xGamepadTaskHandle,   // Дескриптор задачи
    0                      // Ядро (Core 0)
  );
  
  // Задача 2: Управление ходьбой (средний приоритет)
  xTaskCreatePinnedToCore(
    WalkingTask,           // Функция задачи
    "WalkingTask",         // Имя задачи
    4096,                  // Размер стека
    NULL,                  // Параметры
    2,                     // Приоритет (средний)
    &xWalkingTaskHandle,   // Дескриптор задачи
    0                      // Ядро (Core 0)
  );
  
  // Задача 3: Анимация сервоприводов (низкий приоритет)
  xTaskCreatePinnedToCore(
    ServoAnimationTask,    // Функция задачи
    "ServoAnimationTask",  // Имя задачи
    4096,                  // Размер стека
    NULL,                  // Параметры
    1,                     // Приоритет (низкий)
    &xServoTaskHandle,     // Дескриптор задачи
    1                      // Ядро (Core 1) - для распределения нагрузки
  );
  
  Serial.println("All tasks created successfully");
  Serial.println("System ready. Use UP button on gamepad to walk.");
}

// ========= ARDUINO LOOP ========= //

void loop() {
  // Основной цикл Arduino - оставляем пустым или для низкоприоритетных задач
  // Вывод статуса каждые 2 секунды
  static unsigned long lastStatusTime = 0;
  if (millis() - lastStatusTime > 2000) {
    lastStatusTime = millis();
    
    bool walking, buttonPressed;
    if (xSemaphoreTake(xControllerMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      walking = isWalking;
      buttonPressed = isUpButtonPressed;
      xSemaphoreGive(xControllerMutex);
      
      Serial.printf("[Status] Walking: %s, Button: %s, Step: %d\n",
                    walking ? "YES" : "NO",
                    buttonPressed ? "PRESSED" : "RELEASED",
                    currentStep);
    }
  }
  
  // Небольшая задержка для watchdog таймера
  vTaskDelay(pdMS_TO_TICKS(100));
}
