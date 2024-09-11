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
#define MENU_INTER_COUNT      5                     // количество возможных вложений меню
#define SIZE_SCREEN           4                     // количество строк на экране
#define SIZE_SCREEN_LINE      20                    // количество символов в строке на экране

boolean menu_enable = false;                        // изначально не в МЕНЮ
byte menu_index = 0;                                // текущий номер меню
byte menu_inter = 0;                                // индекс вложенности меню
byte last_menu_index[MENU_INTER_COUNT];             // стек переходов по меню
byte current_line_index = 1;                        // текущая выбранная строка меню
byte cursor_index = 1;                              // положение курсора на экране
byte last_cursor_index = 0;                         // предпоследнее положение курсора на экране
byte show_window_first_line = 0;                    // индекс первой отображаемой строки меню на экране
boolean need_reload_menu = true;
boolean need_clear_menu = false;
boolean need_hide_cursor = false;
boolean start_edit_parameter = false;

// Переменные для работы с соляриями

byte all_parameters[10] = {11,22,50,0};

#define pause_before    0
#define pause_after     1
#define price           2
#define remote_start    3

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

LiquidCrystal_I2C lcd(0x27, SIZE_SCREEN_LINE, SIZE_SCREEN);                 // устанавливаем адрес 0x27, и дисплей 16 символов 2 строки

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

  load_parameter();
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

#define MAIN_MENU       0
#define SETTING_MENU    1
#define STATISTIC_MENU  2
#define SOLARIUM_MENU   3

enum type_menu_line {
  MENU_LINE = 0,
  TEXT_LINE,
  DIGIT_PARAM_LINE,
  FIXED_LINE,
  LIST_PARAM_LINE,
};

struct param_limit {
    byte min;
    byte max;
    byte default_value;
};

struct parameter_menu {
  byte next_menu_index;
};

struct parameter_digit {
  byte param_index;
  param_limit limit;
  char unit[5];
};

struct parameter_list {
  byte param_index;
  param_limit limit;

  char list_data[10][4];
};

union param_data {
    parameter_list list;
    parameter_digit digit;
    parameter_menu menu;
};

struct menu_line {
  char string[SIZE_SCREEN_LINE];
  type_menu_line type;
  
  param_data parameter;
};

struct menu_screen {
    menu_line menu_lines[5];
    byte count_lines;
};

// текущее меню
menu_screen current_menu_screen;

/*
  описание меню
*/
const menu_screen menu_all[] PROGMEM = {
  // Меню 0
  {
    {
      {
        "    MAIN MENU",
        FIXED_LINE,
        {0}
      },
      {
        "Settings",
        MENU_LINE,
        {SETTING_MENU}
      },
      {
        "Statistic",
        MENU_LINE,
        {STATISTIC_MENU}
      }
    },
    3
  },
  // Меню 1
  {
    {
      {
        "    SETTINGS",
        FIXED_LINE,
        {0}
      },
      {
        "Solarium",
        MENU_LINE,
        {SOLARIUM_MENU}
      },
      {
        "Bank",
        MENU_LINE,
        {2}
      },
      {
        "Password",
        MENU_LINE,
        {2}
      },
      {
        "Reset",
        MENU_LINE,
        {2}
      }
    },
    5
  },
  // Меню 2
  {
    {
      {
        "    STATISTIC",
        FIXED_LINE,
        {0}
      },
      {
        "Long counters",
        MENU_LINE,
        {1}
      },
      {
        "Short counters",
        MENU_LINE,
        {2}
      }
    },
    3
  },
  // Меню 3
  {
    {
      {
        "    DEVICE",
        FIXED_LINE,
        {1}
      },
      {
        "Pause before",
        DIGIT_PARAM_LINE,
        {
          pause_before,
          {
              0,
              100,
              30
          },
          "sec"
        }
      },
      {
        "Pause after",
        DIGIT_PARAM_LINE,
        {
          pause_after,
          {
              0,
              3,
              3
          },
          "min"
        }
      },
      {
        "Price",
        DIGIT_PARAM_LINE,
        {
          price,
          {
              0,
              100,
              20
          },
          "rub"
        }
      },
      {
        "Remote start",
        LIST_PARAM_LINE,
        {
          remote_start,
          {
              0,
              1,
              0
          },
          {
              "Off",
              "On "
          }
        }
      } 
    },
    5
  },
};

void find_first_line_menu()
{
  byte cursor = 0;
  for(cursor = 0; cursor < current_menu_screen.count_lines; cursor++)
  {
      if(current_menu_screen.menu_lines[cursor].type != FIXED_LINE) break;
  }
  cursor_index = (cursor_index >= SIZE_SCREEN ) ? SIZE_SCREEN - 1 : cursor;
  current_line_index = cursor;
  show_window_first_line = 0;
}

/*
  Загрузка параметров из памяти
*/
void load_parameter()
{

}

/*
  Сохранение параметра в память
*/
void save_parameter(byte index_param)
{

}

/*
  удержание кнопки
*/
void isButtonHold()
{
  need_reload_menu = true;

  if(!menu_enable)
  {
      menu_index = MAIN_MENU;

      memcpy_P( &current_menu_screen, &menu_all[menu_index], sizeof(menu_screen));
      find_first_line_menu();

      menu_enable = true;
  }
  else
  {
      if(start_edit_parameter)
      {
          start_edit_parameter = false;
          need_hide_cursor = false;
          if(current_menu_screen.menu_lines[current_line_index].type == DIGIT_PARAM_LINE)
          {
              save_parameter(current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index);
          }
          else if(current_menu_screen.menu_lines[current_line_index].type == LIST_PARAM_LINE)
          {
              save_parameter(current_menu_screen.menu_lines[current_line_index].parameter.list.param_index);
          }
      }
      else
      {
          if(menu_index == MAIN_MENU) 
          {
              menu_enable = false;
          }
          else
          {
              menu_inter--;
              menu_index = last_menu_index[menu_inter];

              memcpy_P( &current_menu_screen, &menu_all[menu_index], sizeof(menu_screen));
              find_first_line_menu();

              digitalWrite(LEDPin, HIGH);
              lcd.clear();
          }
      }
  }
}

/*
  одиночное нажатие кнопки
*/
void isButtonSingle() 
{
  need_reload_menu = true;

  if(start_edit_parameter)
  {
      if(current_menu_screen.menu_lines[current_line_index].type == DIGIT_PARAM_LINE)
      {
          if(all_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index]++ >= current_menu_screen.menu_lines[current_line_index].parameter.digit.limit.max)
          {
              all_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] = current_menu_screen.menu_lines[current_line_index].parameter.digit.limit.min;
          }
      }
      else if(current_menu_screen.menu_lines[current_line_index].type == LIST_PARAM_LINE)
      {
          if(all_parameters[current_menu_screen.menu_lines[current_line_index].parameter.list.param_index]++ >= current_menu_screen.menu_lines[current_line_index].parameter.list.limit.max)
          {
              all_parameters[current_menu_screen.menu_lines[current_line_index].parameter.list.param_index] = current_menu_screen.menu_lines[current_line_index].parameter.list.limit.min;
          }
      }
  }
  else
  {
      last_cursor_index = cursor_index;
      cursor_index++;
      current_line_index++;

      if(cursor_index >= SIZE_SCREEN || cursor_index >= current_menu_screen.count_lines)
      {
        cursor_index = SIZE_SCREEN - 1;
        show_window_first_line++;
        need_clear_menu = true;

        if(current_line_index >= current_menu_screen.count_lines)
        {
            find_first_line_menu();
        }
      }
  }
}

/*
  двойное нажатие кнопки
*/
void isButtonDouble() 
{
    need_reload_menu = true;

    if(current_menu_screen.menu_lines[current_line_index].type == MENU_LINE)
    {
        last_menu_index[menu_inter] = menu_index;
        menu_inter++;
        menu_index = current_menu_screen.menu_lines[current_line_index].parameter.menu.next_menu_index;

        memcpy_P( &current_menu_screen, &menu_all[menu_index], sizeof(menu_screen));
        find_first_line_menu();
        lcd.clear();
    }
    else if(current_menu_screen.menu_lines[current_line_index].type == DIGIT_PARAM_LINE
         || current_menu_screen.menu_lines[current_line_index].type == LIST_PARAM_LINE)
    {
        if(!start_edit_parameter)
        {
            start_edit_parameter = true;
            hide_cursor();
            need_hide_cursor = true;
        }
    } 
}

void show_line(byte index_line)
{
    if(current_menu_screen.menu_lines[index_line].type == MENU_LINE)
    {
        lcd.print(current_menu_screen.menu_lines[index_line].string);
    }
    else if(current_menu_screen.menu_lines[index_line].type == FIXED_LINE)
    {
        lcd.print(current_menu_screen.menu_lines[index_line].string);
    }
    else if(current_menu_screen.menu_lines[index_line].type == DIGIT_PARAM_LINE)
    {
        char line[21];
        char format[9] = "%s %d %s";
        if(start_edit_parameter && index_line == current_line_index)
        {
           format[2] = '>';
        }
        sprintf(line,format, current_menu_screen.menu_lines[index_line].string, 
                             all_parameters[current_menu_screen.menu_lines[index_line].parameter.digit.param_index], 
                             current_menu_screen.menu_lines[index_line].parameter.digit.unit);
        lcd.print(line);
    }
    else if(current_menu_screen.menu_lines[index_line].type == LIST_PARAM_LINE)
    {
        char line[21];
        char format[6] = "%s %s";
        if(start_edit_parameter && index_line == current_line_index)
        {
           format[2] = '>';
        }
        sprintf(line,format, current_menu_screen.menu_lines[index_line].string, 
                             current_menu_screen.menu_lines[index_line].parameter.list.list_data[all_parameters[current_menu_screen.menu_lines[index_line].parameter.list.param_index]]);
        lcd.print(line);       
    }
}

/*
  Отображение текущего кадра меню
*/
void show_menu()
{
    for(byte i = 0; i < current_menu_screen.count_lines; i++)
    {
        if(show_window_first_line != 0 && i < show_window_first_line) continue;
        if(i >= show_window_first_line + SIZE_SCREEN) break;

        lcd.setCursor(1, i - show_window_first_line);
        show_line(i);
    }
}

/*
  Отображение курсора в текущем положении
*/
void show_cursor() 
{
    lcd.setCursor(0, last_cursor_index);
    lcd.print(F(" "));

    if(!need_hide_cursor)
    {
      lcd.setCursor(0, cursor_index);
      lcd.print(F(">"));
    }
}

void hide_cursor() 
{
    lcd.setCursor(0, cursor_index);
    lcd.print(F(" "));
}

// ============================== процедура прорисовки меню и изменения значения параметров =======================================
void menu ()  
{
  lcd.clear();
  digitalWrite(LEDPin, HIGH);
  while (menu_enable == true)
  {
      read_buttons(buttonPin_Service);

      if(need_clear_menu)
      {
        lcd.clear();
        need_clear_menu = false;
      }
      if(need_reload_menu)
      {
        show_menu();
        show_cursor();
        need_reload_menu = false;
      }
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
