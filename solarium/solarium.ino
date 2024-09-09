#include <Wire.h>

/*
  Управление соляриями
*/
#include <Wire.h>                                   // Подключаем библиотеку для работы с шиной I2C
#include <LiquidCrystal_I2C.h>                      // Подключаем библиотеку для работы с LCD
#include <EEPROMex.h>

// ===============================задаем константы =========================================================================
const byte moneyPin = 2;                            // номер пина, к которому подключён купюроприемник, DB2
const byte inhibitPin = 4;                          // +Inhibit (зеленый) на купюроприемник, DB4
const byte buttonPin_Start = 15;                    // номер входа, подключенный к кнопке "Старт", А0
const byte buttonPin_Service = 13;                  // номер входа, подключенный к кнопке "Сервис", А1
const byte LEDPin = 14;                             // номер выхода светодиода кнопки Старт, DB13
//const byte RelayPin = 17;                           // номер выхода, подключенный к реле, А3
const byte Device_SerNum = 1;                       // серийный номер устройства
const char Device_Ver[] = "0.0";                    // версия ПО устройства
const char Device_Date[] = "09/09/24";              // дата производства устройства
const unsigned long block = 500000;                 // блокировка устройства при превышении этой суммы денег

boolean lastReading = false;                        // флаг предыдущего состояния кнопки
boolean buttonSingle = false;                       // флаг состояния "краткое нажатие"
boolean buttonDouble = false;                       // флаг состояния "двойное нажатие"
boolean buttonHold = false;                         // флаг состояния "долгое нажатие"
unsigned long onTime = 0;                           // переменная обработки временного интервала
unsigned long lastSwitchTime = 0;                   // переменная времени предыдущего переключения состояния
unsigned long ledStartTime = 0;                     // переменная-флаг времени начала включения LED

const int bounceTime = 10;                          // задержка для подавления дребезга
const int holdTime = 1000;                          // время, в течение которого нажатие можно считать удержанием кнопки
const int doubleTime = 500;                         // время, в течение которого нажатия можно считать двойным

// ============================== Описываем свой символ "Рубль" ========================================================================
// Просто "рисуем" символ единицами. Единицы при выводе на экран окажутся закрашенными точками, нули - не закрашенными
byte rubl[8] = {
  0b00000,
  0b01110,
  0b01001,
  0b01001,
  0b01110,
  0b01000,
  0b11110,
  0b01000,
};

LiquidCrystal_I2C lcd(0x27, 16, 2);                 // устанавливаем адрес 0x27, и дисплей 16 символов 2 строки

void setup() {
  Serial.begin(115200);

  lcd.init();                                       // инициализация LCD
  lcd.backlight();                                  // включаем подсветку
  lcd.createChar(0, rubl);                          // создаем символ и записываем его в память LCD по 0 адресу

  pinMode(inhibitPin, OUTPUT);                      // устанавливает режим работы - выход
  pinMode(moneyPin, INPUT_PULLUP);                  // устанавливает режим работы - вход, подтягиваем к +5В через встроенный подтягивающий резистор (на всякий случай)
  pinMode(LEDPin, OUTPUT);                          // инициализируем пин, подключенный к светодиоду, как выход
  pinMode(buttonPin_Service, INPUT_PULLUP);                // инициализируем пин, подключенный к кнопке, как вход
  pinMode(buttonPin_Start, INPUT_PULLUP);                  // инициализируем пин, подключенный к кнопке, как вход

  digitalWrite(LEDPin,LOW);                        // изначально светодиод погашен
  digitalWrite(inhibitPin, LOW);                    // изначально разрешаем прием купюр
}

void read_buttons(byte x)
{
  boolean reading = !digitalRead(x);

  if (reading && !lastReading)                      // проверка первичного нажатия
  {
    onTime = millis(); 
  }
  if (reading && lastReading)                       // проверка удержания
  {
    if ((millis() - onTime) > holdTime)
    {
      buttonHold = true;
      //digitalWrite(LEDPin, HIGH);                 // при удержании кнопки загорается светодиод
      digitalWrite(LEDPin, !digitalRead(LEDPin));   // при удержании кнопки мигает светодиод
    }
  }
  if (!reading && lastReading)                      // проверка отпускания кнопки
  {
    if (((millis() - onTime) > bounceTime) && !buttonHold)
    {
      if ((millis() - lastSwitchTime) >= doubleTime)
      {
        lastSwitchTime = millis();
        buttonSingle = true;
      }
      else
      {
        lastSwitchTime = millis();
        buttonDouble = true;
        buttonSingle = false;
        isButtonDouble();
        buttonDouble = false;                       // сброс состояния после выполнения команды
      }
    }
    if (buttonHold)
    {
      buttonDouble = false;
      isButtonHold();
      buttonHold = false;                           // сброс состояния после выполнения команды
    }
  }
  lastReading = reading;
  if (buttonSingle && (millis() - lastSwitchTime) > doubleTime)
  {
    buttonDouble = false;
    isButtonSingle();
    buttonSingle = false;                           // сброс состояния после выполнения команды
  }
}

// ============================== удержание кнопки ===============================================================================
void isButtonHold()
{
  Serial.println( F("isButtonHold") );
}

// ============================== одиночное нажатие кнопки ========================================================================
void isButtonSingle() 
{
  Serial.println( F("isButtonSingle") );
}

// ================================ двойное нажатие кнопки ========================================================================
void isButtonDouble() 
{
  Serial.println( F("isButtonDouble") );
}

void loop() {

  read_buttons(buttonPin_Service);

}
