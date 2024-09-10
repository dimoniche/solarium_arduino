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
const PROGMEM char Device_Ver[] = "0.0";                    // версия ПО устройства
const PROGMEM char Device_Date[] = "09/09/24";              // дата производства устройства
const unsigned long block = 500000;                 // блокировка устройства при превышении этой суммы денег

//======Переменные обработки клавиш=================
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

//======Переменные меню=============================
boolean menu_enable = false;                        // изначально не в МЕНЮ
byte menu_index = 0;
byte last_menu_index = 0;
byte line_index = 1;
byte cursor_index = 1;
byte last_cursor_index = 0;
byte show_window_first_line = 0;

// ============================== Описываем свой символ "Рубль" ========================================================================
// Просто "рисуем" символ единицами. Единицы при выводе на экран окажутся закрашенными точками, нули - не закрашенными
const PROGMEM byte rubl[8] = {
  0b00000,
  0b01110,
  0b01001,
  0b01001,
  0b01110,
  0b01000,
  0b11110,
  0b01000,
};

LiquidCrystal_I2C lcd(0x27, 20, 4);                 // устанавливаем адрес 0x27, и дисплей 16 символов 2 строки

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

#define SIZE_SCREEN 4

enum type_menu_line {
  MENU_LINE,
  TEXT_LINE,
  PARAM_LINE,
  FIXED_LINE,
};

struct menu_line {
  char string[20];
  type_menu_line type;
  byte next_menu_index;
};

struct menu_screen {
    menu_line menu_lines[5];
    byte count_lines;
};

const menu_screen menu_all[] = {
  // Меню 0
  {
    {
      {
        "    MAIN MENU   ",
        FIXED_LINE,
        0
      },
      {
        "Settings        ",
        MENU_LINE,
        1
      },
      {
        "Statistic        ",
        MENU_LINE,
        2
      }
    },
    3
  },
  // Меню 1
  {
    {
      {
        "    SETTINGS   ",
        FIXED_LINE,
        0
      },
      {
        "Solarium        ",
        MENU_LINE,
        1
      },
      {
        "Bank            ",
        MENU_LINE,
        2
      },
      {
        "Password         ",
        MENU_LINE,
        2
      },
      {
        "Reset             ",
        MENU_LINE,
        2
      }
    },
    5
  },
  // Меню 2
  {
    {
      {
        "    STATISTIC   ",
        FIXED_LINE,
        0
      },
      {
        "Long counters    ",
        MENU_LINE,
        1
      },
      {
        "Short counters    ",
        MENU_LINE,
        2
      }
    },
    3
  },
};

// ============================== удержание кнопки ===============================================================================
void isButtonHold()
{
  if(!menu_enable)
  {
      menu_index = 0;
      menu_enable = true;
  }
  else
  {
      if(menu_index == 0) 
      {
          menu_enable = false;
      }
      else
      {
          menu_index = last_menu_index;
          lcd.clear();
      }
  }
}

// ============================== одиночное нажатие кнопки ========================================================================
void isButtonSingle() 
{
  last_cursor_index = cursor_index;
  cursor_index++;
  line_index++;

  Serial.print(menu_all[menu_index].count_lines);

  if(cursor_index >= SIZE_SCREEN || cursor_index >= menu_all[menu_index].count_lines)
  {
    cursor_index = SIZE_SCREEN - 1;
    show_window_first_line++;

    if(line_index >= menu_all[menu_index].count_lines)
    {
        byte cursor = 0;
        for(cursor = 0; cursor < menu_all[menu_index].count_lines; cursor++)
        {
            if(menu_all[menu_index].menu_lines[cursor].type != FIXED_LINE) break;
        }
        cursor_index = (cursor_index >= SIZE_SCREEN ) ? SIZE_SCREEN - 1 : cursor;
        line_index = cursor;
        show_window_first_line = 0;
    }
  }
}

// ================================ двойное нажатие кнопки ========================================================================
void isButtonDouble() 
{
    if(menu_all[menu_index].menu_lines[line_index].type == MENU_LINE)
    {
        last_menu_index = menu_index;
        menu_index = menu_all[menu_index].menu_lines[line_index].next_menu_index;
        lcd.clear();
    }    
}

void show_line(byte index_line)
{
    lcd.print(menu_all[menu_index].menu_lines[index_line].string);
}

void show_menu()
{
    for(byte i = 0; i < menu_all[menu_index].count_lines; i++)
    {
        if(show_window_first_line != 0 && i < show_window_first_line) continue;
        if(i >= show_window_first_line + SIZE_SCREEN) break;

        lcd.setCursor(1, i - show_window_first_line);
        show_line(i);
    }
}

void show_cursor() 
{
    lcd.setCursor(0, last_cursor_index);
    lcd.print(F(" "));
    lcd.setCursor(0, cursor_index);
    lcd.print(F(">"));
}

// ============================== процедура прорисовки меню и изменения значения параметров =======================================
void menu ()  
{
  lcd.clear();
  digitalWrite(LEDPin, HIGH);
  while (menu_enable == true)
  {
      read_buttons(buttonPin_Service);
      //lcd.setCursor(0,0);

      show_menu();
      show_cursor();
  }

  lcd.clear();
  digitalWrite(LEDPin, LOW);
}

void loop() {
  read_buttons(buttonPin_Service);
  if (menu_enable == true)                          // если флаг menu_enable = ИСТИНА, то входим в меню 
  {
    menu();  
  }
  else
  { 
    /*if (bill_enable == true && menu_enable == false)  
    {
      get_money ();                                 // принимаем деньги
    }*/
  }
  /*if (bill_enable == false && menu_enable == false)
  {
    countdown_timer(sek, minu);                     // запускаем таймер обратного отсчета
  }*/
}
