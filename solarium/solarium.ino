/*
  Управление соляриями
*/
#include <Wire.h>                                   // Подключаем библиотеку для работы с шиной I2C
#include <LiquidCrystal_I2C.h>                      // Подключаем библиотеку для работы с LCD
#include <EEPROMex.h>

// ===============================задаем константы =========================================================================
const byte moneyPin = 2;                            // номер пина, к которому подключён купюроприемник, DB2
const byte inhibitPin = 4;                          // +Inhibit (зеленый) на купюроприемник, DB4
//const byte buttonPin_Start = 15;                    // номер входа, подключенный к кнопке "Старт", А0
//const byte buttonPin_Service = 14;                  // номер входа, подключенный к кнопке "Сервис", А1
//const byte LEDPin = 13;                             // номер выхода светодиода кнопки Старт, DB13
const byte buttonPin_Start = 15;                    // номер входа, подключенный к кнопке "Старт", А0
const byte buttonPin_Service = 13;                  // номер входа, подключенный к кнопке "Сервис", А1
const byte LEDPin = 14;                             // номер выхода светодиода кнопки Старт, DB13

// ноги управления соляриями
const byte lamp_start_pin = 5;          // Запуск солярия Luxura. Включение ламп солярия FireSun, SunFlower
const byte vent_pin = 6;                // Включение вентиляторов солярия FireSun, SunFlower
const byte start_solarium = 7;          // Удаленный старт от солярия

const byte Device_SerNum = 1;                       // серийный номер устройства
const PROGMEM char Device_Ver[] = "0.0";            // версия ПО устройства
const PROGMEM char Device_Date[] = "09/09/24";      // дата производства устройства
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

// Переменные приема денег
boolean bill_enable = true;                         // изначально купюроприемник принимает деньги

// Данные сеанса
int minute = 0;
int remain = 0;
int second = 0;

volatile unsigned int impulseCounter = 0;           // счетчик импульсов от купюроприемника (1 = 10 руб.). volatile для видимости переменной и в функции обработки прерывания 
byte debounceDelay = 10;                            // для устранения дребезга устанавливаем мин. длительность принимаемого импульса
int trueState = LOW;
int lastState = LOW;
unsigned long lastStateChangeTime = 0;              // положительные целые числа (4 байта)

boolean counter = false;                            // счетчик для полусекунд
unsigned long previousMillis = 0;                   // переменная для хранения значений таймера

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
boolean need_reload_menu = true;                    // флаг перерисовки экрана
boolean need_clear_menu = false;                    // флаг очистки экрана
boolean need_hide_cursor = false;                   // флаг скытия курсора на экране
boolean start_edit_parameter = false;               // флаг старта редактирования параметра

bool enable_reset = false;                          // разрешение сброса настроек

// Переменные для работы с соляриями
#define pause_before              0
#define pause_after               1
#define price                     2
#define remote_start              3

#define LUXURA_SOL                0
#define FIRESUN_UV_SOL            1
#define FIRESUN_UV_K_SOL          2
#define SUNFLOWER_SOL             3

#define solarium_type             4
#define work_regime               5
#define signal_rele               6
#define weight_impulse            7
#define reset_device              8
#define reset_counters            9
#define password                  10
#define COUNT_BYTE_PARAMETER      11
byte all_byte_parameters[COUNT_BYTE_PARAMETER];

const byte all_byte_parameters_default[COUNT_BYTE_PARAMETER] = {
  30,
  3,
  20,
  0,
  0,
  0,
  0,
  10,
  0,
  0,
  0,
};

#define long_starts_counter       0
#define long_money_counter        1
#define long_time_counter         2
#define short_starts_counter      3
#define short_money_counter       4
#define short_time_counter        5
#define money_counter             6
#define COUNT_LONG_PARAMETER      7
unsigned long all_long_parameters[COUNT_LONG_PARAMETER];

#define time_seance               0
#define time_delay                1
#define COUNT_TEXT_PARAMETER      2
char text_parameters[COUNT_TEXT_PARAMETER][SIZE_SCREEN_LINE];

LiquidCrystal_I2C lcd(0x27, SIZE_SCREEN_LINE, SIZE_SCREEN);                 // устанавливаем адрес 0x27, и дисплей 16 символов 2 строки

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
#define BANK_MENU       4
#define PASSWORD_MENU   5
#define LONG_COUNTER_MENU   6
#define SHORT_COUNTER_MENU  7

enum type_menu_line {
  MENU_LINE = 0,
  TEXT_LINE,
  DIGIT_PARAM_LINE,
  FIXED_LINE,
  LIST_PARAM_LINE,
  TEXT_PARAM_LINE,
  DIGIT_VIEW_LINE,
};

struct param_limit {
    byte min;
    byte max;
};

struct parameter_menu {
  byte next_menu_index;
};

struct parameter_digit {
  byte param_index;
  param_limit limit;
  char unit[8];
};

struct parameter_list {
  byte param_index;
  param_limit limit;
  char list_data[4][13];
};

struct parameter_header {
  byte param_index;
  param_limit limit;
};

struct parameter_text {
  byte param_index;
  param_limit limit;
  char unit[8];
};

union param_data {
    parameter_list list;
    parameter_digit digit;
    parameter_text text;
    parameter_header header;
    parameter_menu menu;
};

struct menu_line {
  char string[SIZE_SCREEN_LINE * 2];
  type_menu_line type;
  
  param_data parameter;
};

struct menu_screen {
    menu_line menu_lines[8];
    byte count_lines;
};

// текущее меню
menu_screen current_menu_screen;

/*
  Описание основного меню
*/
const menu_screen menu_main[] PROGMEM = {
  // Меню внесения денег и отображения времени сеанкса
  {
    {
      {
        "",
        FIXED_LINE,
        {0}
      },
      {
        "  ВНЕСЕНО",
        DIGIT_VIEW_LINE,
        {
          money_counter,
          {
              0,
              0,
          },
          "руб"
        }
      },
      {
        "",
        TEXT_PARAM_LINE,
        {
          time_seance,
          {
              0,
              0,
          },
          " "
        }
      },
    },
    3
  },
  // Время задержки до
  {
    {
      {
        "",
        FIXED_LINE,
        {0}
      },
      {
        "   ",
        TEXT_PARAM_LINE,
        {
          time_delay,
          {
              0,
              0,
          },
          "СЕК"
        },
      },
    },
    2
  },
  // Меню ведения сеанса
  {
    {
      {
        "",
        FIXED_LINE,
        {0}
      },
      {
        "      CEAHC",
        FIXED_LINE,
        {0}
      },
      {
        "  ",
        TEXT_PARAM_LINE,
        {
          time_seance,
          {
              0,
              0,
          },
          "МИН"
        }
      },
    },
    3
  },
  // Меню паузы после сеанса
  {
    {
      {
        "",
        FIXED_LINE,
        {0}
      },
      {
        "     ПАУЗА",
        FIXED_LINE,
        {0}
      },
      {
        "   ",
        TEXT_PARAM_LINE,
        {
          time_seance,
          {
              0,
              0,
          },
          " "
        }
      },
    },
    3
  },
  // Меню окончания сеанса
  {
    {
      {
        "",
        FIXED_LINE,
        {0}
      },
      {
        "     КОНЕЦ",
        FIXED_LINE,
        {0}
      },
    },
    2
  },
};

/*
  описание настроечного меню
*/
const menu_screen menu_settings[] PROGMEM = {
  // Меню 0
  {
    {
      {
        "ГЛАВНОЕ МЕНЮ",
        FIXED_LINE,
        {0}
      },
      {
        "Настройки",
        MENU_LINE,
        {SETTING_MENU}
      },
      {
        "Статистика",
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
        "НАСТРОЙКИ",
        FIXED_LINE,
        {0}
      },
      {
        "Солярий",
        MENU_LINE,
        {SOLARIUM_MENU}
      },
      {
        "Банк",
        MENU_LINE,
        {BANK_MENU}
      },
      {
        "Пароль",
        MENU_LINE,
        {PASSWORD_MENU}
      },
      {
        "Сброс",
        LIST_PARAM_LINE,
        {
          reset_device,
          {
              0,
              1,
          },
          {
              "      ",
              "запуск"
          }
        }
      },
    },
    5
  },
  // Меню 2
  {
    {
      {
        "СТАТИСТИКА",
        FIXED_LINE,
        {0}
      },
      {
        "Длинные счетчики",
        MENU_LINE,
        {LONG_COUNTER_MENU}
      },
      {
        "Короткие счетчики",
        MENU_LINE,
        {SHORT_COUNTER_MENU}
      },
      {
        "Сброс",
        LIST_PARAM_LINE,
        {
          reset_counters,
          {
              0,
              1,
          },
          {
              "      ",
              "запуск"
          }
        }
      },
    },
    4
  },
  // Меню 3
  {
    {
      {
        "СОЛЯРИЙ",
        FIXED_LINE,
        {1}
      },
      {
        "Пауза до",
        DIGIT_PARAM_LINE,
        {
          pause_before,
          {
              0,
              100,
          },
          "сек"
        }
      },
      {
        "Пауза после",
        DIGIT_PARAM_LINE,
        {
          pause_after,
          {
              0,
              3,
          },
          "мин"
        }
      },
      {
        "Цена",
        DIGIT_PARAM_LINE,
        {
          price,
          {
              0,
              100,
          },
          "руб"
        }
      },
      {
        "Удален.старт",
        LIST_PARAM_LINE,
        {
          remote_start,
          {
              0,
              1,
          },
          {
              "Выкл",
              "Вкл "
          }
        }
      },
      {
        "Тип",
        LIST_PARAM_LINE,
        {
          solarium_type,
          {
              0,
              3,
          },
          {
              "Luxura      ",
              "FireSun UV  ",
              "FireSun UV+K",
              "SunFlower   "
          }
        }
      },
      {
        "Режим",
        LIST_PARAM_LINE,
        {
          work_regime,
          {
              0,
              1,
          },
          {
              "Kollaten",
              "UV      "
          }
        }
      },
      {
        "Реле",
        LIST_PARAM_LINE,
        {
          signal_rele,
          {
              0,
              1,
          },
          {
              "high",
              "low "
          }
        }
      },
    },
    8
  },
  // Меню 4
  {
    {
      {
        "БАНК",
        FIXED_LINE,
        {0}
      },
      {
        "Руб/имп",
        DIGIT_PARAM_LINE,
        {
          weight_impulse,
          {
              0,
              100,
          },
          " "
        }
      },
    },
    2
  },
  // Меню 5
  {
    {
      {
        "УСТАНОВКА ПАРОЛЯ",
        FIXED_LINE,
        {0}
      },
      {
        "Пароль",
        DIGIT_PARAM_LINE,
        {
          password,
          {
              0,
              255,
          },
          " "
        }
      },
    },
    2
  },
  // Меню 6
  {
    {
      {
        "ДЛИННЫЕ СЧЕТЧИКИ",
        FIXED_LINE,
        {0}
      },
      {
        "Запуски",
        DIGIT_VIEW_LINE,
        {
          long_starts_counter,
          {
              0,
              0,
          },
          " "
        }
      },
      {
        "Деньги",
        DIGIT_VIEW_LINE,
        {
          long_money_counter,
          {
              0,
              0,
          },
          "руб"
        }
      },
      {
        "Время",
        DIGIT_VIEW_LINE,
        {
          long_time_counter,
          {
              0,
              0,
          },
          "сек"
        }
      },
    },
    4
  },
  // Меню 7
  {
    {
      {
        "КОРОТКИЕ СЧЕТЧИКИ",
        FIXED_LINE,
        {0}
      },
      {
        "Запуски",
        DIGIT_VIEW_LINE,
        {
          short_starts_counter,
          {
              0,
              0,
          },
          " "
        }
      },
      {
        "Деньги",
        DIGIT_VIEW_LINE,
        {
          short_money_counter,
          {
              0,
              0,
          },
          "руб"
        }
      },
      {
        "Время",
        DIGIT_VIEW_LINE,
        {
          short_time_counter,
          {
              0,
              0,
          },
          "сек"
        }
      },
    },
    4
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
    for(int i = 0; i < COUNT_BYTE_PARAMETER; i++ )
    {
        all_byte_parameters[i] = EEPROM.readByte(i);
    }
    for(int i = COUNT_BYTE_PARAMETER, j = 0; j < COUNT_LONG_PARAMETER; i += 4, j++ )
    {
        all_long_parameters[j] = EEPROM.readLong(i);
    }
}

/*
  Сохранение параметра в память
*/
void save_byte_parameter(byte index_param)
{
    EEPROM.updateByte(index_param, all_byte_parameters[index_param]);
}

/*
  Сохранение параметра в память
*/
void save_long_parameter(byte index_param)
{
    EEPROM.updateLong(COUNT_BYTE_PARAMETER + index_param * 4, all_long_parameters[index_param]);
}

/*
  Сброс параметров в памяти
*/
void reset_parameter()
{
    for(int i = 0; i < COUNT_BYTE_PARAMETER; i++)
    {
        all_byte_parameters[i] = all_byte_parameters_default[i];
        EEPROM.updateByte(i, all_byte_parameters_default[i]);
    }

    all_long_parameters[short_starts_counter] = 0;
    save_long_parameter(short_starts_counter);

    all_long_parameters[short_money_counter] = 0;
    save_long_parameter(short_money_counter);

    all_long_parameters[short_time_counter] = 0;
    save_long_parameter(short_time_counter);
    
    all_long_parameters[long_starts_counter] = 0;
    save_long_parameter(long_starts_counter);

    all_long_parameters[long_money_counter] = 0;
    save_long_parameter(long_money_counter);

    all_long_parameters[long_time_counter] = 0;
    save_long_parameter(long_time_counter);

    all_long_parameters[money_counter] = 0;
    save_long_parameter(money_counter);
}

void reset_short_counters()
{
    all_long_parameters[short_starts_counter] = 0;
    save_long_parameter(short_starts_counter);

    all_long_parameters[short_money_counter] = 0;
    save_long_parameter(short_money_counter);

    all_long_parameters[short_time_counter] = 0;
    save_long_parameter(short_time_counter);
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

      memcpy_P( &current_menu_screen, &menu_settings[menu_index], sizeof(menu_screen));
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
              save_byte_parameter(current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index);
          }
          else if(current_menu_screen.menu_lines[current_line_index].type == LIST_PARAM_LINE)
          {
              save_byte_parameter(current_menu_screen.menu_lines[current_line_index].parameter.list.param_index);
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

              memcpy_P( &current_menu_screen, &menu_settings[menu_index], sizeof(menu_screen));
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
          if(all_byte_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index]++ >= current_menu_screen.menu_lines[current_line_index].parameter.digit.limit.max)
          {
              all_byte_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] = current_menu_screen.menu_lines[current_line_index].parameter.digit.limit.min;
          }
      }
      else if(current_menu_screen.menu_lines[current_line_index].type == LIST_PARAM_LINE)
      {
          if(all_byte_parameters[current_menu_screen.menu_lines[current_line_index].parameter.list.param_index]++ >= current_menu_screen.menu_lines[current_line_index].parameter.list.limit.max)
          {
              all_byte_parameters[current_menu_screen.menu_lines[current_line_index].parameter.list.param_index] = current_menu_screen.menu_lines[current_line_index].parameter.list.limit.min;
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

        memcpy_P( &current_menu_screen, &menu_settings[menu_index], sizeof(menu_screen));
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
        lcd.print(convertCyr( utf8rus( current_menu_screen.menu_lines[index_line].string)));
    }
    else if(current_menu_screen.menu_lines[index_line].type == FIXED_LINE)
    {
        lcd.print(convertCyr( utf8rus( current_menu_screen.menu_lines[index_line].string)));
    }
    else if(current_menu_screen.menu_lines[index_line].type == DIGIT_PARAM_LINE)
    {
        char line[SIZE_SCREEN_LINE * 2];
        char format[9] = "%s %d %s";
        if(start_edit_parameter && index_line == current_line_index)
        {
           format[2] = '>';
        }
        sprintf(line,format, current_menu_screen.menu_lines[index_line].string, 
                             all_byte_parameters[current_menu_screen.menu_lines[index_line].parameter.digit.param_index], 
                             current_menu_screen.menu_lines[index_line].parameter.digit.unit);
        lcd.print(convertCyr( utf8rus( line )));
    }
    else if(current_menu_screen.menu_lines[index_line].type == LIST_PARAM_LINE)
    {
        char line[SIZE_SCREEN_LINE * 2];
        char format[6] = "%s %s";
        if(start_edit_parameter && index_line == current_line_index)
        {
           format[2] = '>';
        }
        sprintf(line,format, current_menu_screen.menu_lines[index_line].string, 
                             current_menu_screen.menu_lines[index_line].parameter.list.list_data[all_byte_parameters[current_menu_screen.menu_lines[index_line].parameter.list.param_index]]);
        lcd.print(convertCyr( utf8rus( line )));
    }
    else if(current_menu_screen.menu_lines[index_line].type == DIGIT_VIEW_LINE)
    {
        char line[SIZE_SCREEN_LINE * 2];
        sprintf(line,"%s %ld %s", current_menu_screen.menu_lines[index_line].string, 
                                 all_long_parameters[current_menu_screen.menu_lines[index_line].parameter.digit.param_index],    
                                 current_menu_screen.menu_lines[index_line].parameter.digit.unit);
        lcd.print(convertCyr( utf8rus( line )));
    }
    else if(current_menu_screen.menu_lines[index_line].type == TEXT_PARAM_LINE)
    {
        char line[SIZE_SCREEN_LINE * 2];
        sprintf(line,"%s %s %s", current_menu_screen.menu_lines[index_line].string, 
                                  text_parameters[current_menu_screen.menu_lines[index_line].parameter.text.param_index], 
                                  current_menu_screen.menu_lines[index_line].parameter.text.unit);
        lcd.print(convertCyr( utf8rus( line )));
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

/* 
 * Функция подсчета импульсов от купюроприемника
 * Когда импульсов нет, записываются значения reading = trueState = lastState = HIGH, при поступлении импульса reading = LOW,
 * фиксируется время появления импульса в lastStateChangeTime. Если длительность импульса > debounceDelay (время дребезга),
 * значит это полезный импульс, значения изменяются reading = trueState = lastState = LOW
 */
bool read_money_impulse ()
{
  int reading = digitalRead(moneyPin);
  bool impulse = false;
  if (reading != lastState) 
  {
    lastStateChangeTime = millis();
  }
  if ((millis() - lastStateChangeTime) > debounceDelay)
  {
    if (reading != trueState)
    {
      trueState = reading;
      if (trueState == LOW) 
      {
          all_long_parameters[money_counter] += all_byte_parameters[weight_impulse];
          impulse = true;
      }
    }
  }
  lastState = reading;

  return impulse;
}

void start_solarium_work()
{
    if(all_byte_parameters[signal_rele]) digitalWrite(lamp_start_pin, HIGH);
    else digitalWrite(lamp_start_pin, LOW);
 
    switch(all_byte_parameters[solarium_type])
    {
        case LUXURA_SOL:
        break;
        case FIRESUN_UV_SOL:
          digitalWrite(vent_pin, HIGH);
        break;
        case FIRESUN_UV_K_SOL:
        break;
        case SUNFLOWER_SOL:
          digitalWrite(vent_pin, HIGH);
        break;
    }
}

void stop_solarium_work()
{
    if(all_byte_parameters[signal_rele]) digitalWrite(lamp_start_pin, LOW);
    else digitalWrite(lamp_start_pin, HIGH);
}

void stop_vent_work()
{
    switch(all_byte_parameters[solarium_type])
    {
        case LUXURA_SOL:
        break;
        case FIRESUN_UV_SOL:
          digitalWrite(vent_pin, LOW);
        break;
        case FIRESUN_UV_K_SOL:
        break;
        case SUNFLOWER_SOL:
          digitalWrite(vent_pin, LOW);
        break;
    }
}

/*
  Прием денег
*/
void get_money ()
{
    bool impulse = read_money_impulse();

    minute = all_long_parameters[money_counter] / all_byte_parameters[price];
    remain = all_long_parameters[money_counter] % all_byte_parameters[price];
    second = remain * 60 / all_byte_parameters[price];

    if(impulse)
    {
        need_reload_menu = true;
    }
  
    if (all_long_parameters[money_counter] >= all_byte_parameters[price])
    {
        // достаточно денег для оказания услуги
        sprintf(text_parameters[time_seance],"СЕАНС %02d:%02d МИН", minute, second);

        digitalWrite(LEDPin, HIGH);                     // зажигаем светодиод 

        if (digitalRead(buttonPin_Start) == LOW)
        {
          digitalWrite(inhibitPin, HIGH);               // выставляем запрет приема монет 
          digitalWrite(LEDPin, LOW);                    // гасим светодиод

          // сохраняем статистику
          {
            all_long_parameters[long_starts_counter]++;
            save_long_parameter(long_starts_counter);
            all_long_parameters[short_starts_counter]++;
            save_long_parameter(short_starts_counter);
            all_long_parameters[long_time_counter] += minute * 60 + second;
            save_long_parameter(long_time_counter);
            all_long_parameters[short_time_counter] += minute * 60 + second;
            save_long_parameter(short_time_counter);
            all_long_parameters[long_money_counter] += all_long_parameters[money_counter];
            save_long_parameter(long_money_counter);
            all_long_parameters[short_money_counter] += all_long_parameters[money_counter];
            save_long_parameter(short_money_counter);
            all_long_parameters[money_counter] = 0;
            save_long_parameter(money_counter);
          }

          // задержка до запуска
          memcpy_P( &current_menu_screen, &menu_main[1], sizeof(menu_screen));
          sprintf(text_parameters[time_delay],"%2d", all_byte_parameters[pause_before]);
          lcd.clear();
          show_menu();
          delay(all_byte_parameters[pause_before] * 1000);

          memcpy_P( &current_menu_screen, &menu_main[2], sizeof(menu_screen));
          menu_index = 2;
          sprintf(text_parameters[time_seance]," %02d:%02d", minute, second);
          need_clear_menu = true;
          need_reload_menu = true;

          bill_enable = !bill_enable;                   // устанавливаем флаг: не принимаем деньги

          // Запускаем работу солярия
          start_solarium_work();
        }
    }
}

// ============================== процедура прорисовки меню и изменения значения параметров =======================================
void menu()
{
  lcd.clear();
  digitalWrite(LEDPin, HIGH);
  show_cursor();
  need_hide_cursor = false;

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
      if(all_byte_parameters[reset_device])
      {
          if(enable_reset) reset_parameter();
          all_byte_parameters[reset_device] = 0;
      }
      if(all_byte_parameters[reset_counters])
      {
          if(enable_reset) reset_short_counters();
          all_byte_parameters[reset_counters] = 0;
      }
      if(all_byte_parameters[password] == 22)
      {
          enable_reset = true;
          all_byte_parameters[password] = 0;
          save_byte_parameter(password);
      }
  }

  lcd.clear();
  digitalWrite(LEDPin, LOW);
}

/*
    Событие полусекунды
*/
void one_half_second()
{

}

void restart_menu()
{
    memcpy_P( &current_menu_screen, &menu_main[0], sizeof(menu_screen));
    sprintf(text_parameters[time_seance],"");
    menu_index = 0;

    need_clear_menu = true;
    need_reload_menu = true;

    bill_enable = !bill_enable;
    
    digitalWrite(inhibitPin, LOW);
}

/*
    Событие секунды
*/
void second_event()
{
    unsigned long time_remain = minute * 60 + second - 1;

    minute = time_remain / 60;
    second = time_remain % 60;

    sprintf(text_parameters[time_seance]," %02d:%02d", minute, second);
    need_reload_menu = true;

    if(menu_index == 4 && time_remain == 0)
    {
        restart_menu();
    }
    if(menu_index == 3 && time_remain == 0)
    {
        stop_vent_work();

        memcpy_P( &current_menu_screen, &menu_main[4], sizeof(menu_screen));
        menu_index = 4;
        second = 10;

        need_clear_menu = true;
        need_reload_menu = true;
    }
    if(menu_index == 2 && time_remain == 0)
    {
        stop_solarium_work();

        if(all_byte_parameters[solarium_type] == LUXURA_SOL)
        {
            memcpy_P( &current_menu_screen, &menu_main[4], sizeof(menu_screen));
            menu_index = 4;
            second = 10;
        }
        else
        {
            memcpy_P( &current_menu_screen, &menu_main[3], sizeof(menu_screen));
            menu_index = 3;
            minute = all_byte_parameters[pause_after];
        }

        need_clear_menu = true;
        need_reload_menu = true;
    }
}

void countdown_timer() 
{
    if (millis() - previousMillis > 500)
    {
        one_half_second();

        previousMillis = millis();
        counter = !counter; 
        if (counter == false)
        {
            second_event();
        }
    }
}

void setup()
{
  Serial.begin(115200);

  lcd.init();                                       // инициализация LCD
  lcd.backlight();                                  // включаем подсветку

  pinMode(inhibitPin, OUTPUT);                      // устанавливает режим работы - выход
  pinMode(moneyPin, INPUT_PULLUP);                  // устанавливает режим работы - вход, подтягиваем к +5В через встроенный подтягивающий резистор (на всякий случай)
  pinMode(LEDPin, OUTPUT);                          // инициализируем пин, подключенный к светодиоду, как выход
  pinMode(buttonPin_Service, INPUT_PULLUP);         // инициализируем пин, подключенный к кнопке, как вход
  pinMode(buttonPin_Start, INPUT_PULLUP);           // инициализируем пин, подключенный к кнопке, как вход
  pinMode(lamp_start_pin, OUTPUT);                  // управление лампами 
  pinMode(vent_pin, OUTPUT);                        // управление вентиляторами

  digitalWrite(LEDPin,LOW);                         // изначально светодиод погашен
  digitalWrite(inhibitPin, LOW);                    // изначально разрешаем прием купюр
  digitalWrite(lamp_start_pin, LOW);                // изначально выключен
  digitalWrite(vent_pin, LOW);                      // изначально выключен

  load_parameter();
  memcpy_P( &current_menu_screen, &menu_main[0], sizeof(menu_screen));
  sprintf(text_parameters[time_seance],"");
  menu_index = 0;

  all_long_parameters[money_counter] = 0;
}

void loop() 
{
  read_buttons(buttonPin_Service);
  hide_cursor();
  need_hide_cursor = true;

  if (menu_enable == true)                          // если флаг menu_enable = ИСТИНА, то входим в меню 
  {
      menu();
      need_reload_menu = true;
      memcpy_P( &current_menu_screen, &menu_main[0], sizeof(menu_screen));
  }
  else
  {
    if (bill_enable == true && menu_enable == false)  
    {
        get_money();
    }
    if(need_clear_menu)
    {
      lcd.clear();
      need_clear_menu = false;
    }
    if(need_reload_menu)
    {
      show_menu();
      need_reload_menu = false;
    }
  }
  if (bill_enable == false && menu_enable == false)
  {
    countdown_timer();                              // запускаем таймер обратного отсчета
  }
}

String utf8rus(String source) {
  int i,k;
  String target;
  unsigned char n;
  char m[2] = { '0', '\0' };

  k = source.length(); i = 0;

  while (i < k) {
    n = source[i]; i++;

    if (n >= 0xC0) {
      switch (n) {
        case 0xD0: {
          n = source[i]; i++;
          if (n == 0x81) { n = 0xA8; break; }
          if (n >= 0x90 && n <= 0xBF) n = n + 0x30;
          break;
        }
        case 0xD1: {
          n = source[i]; i++;
          if (n == 0x91) { n = 0xB8; break; }
          if (n >= 0x80 && n <= 0x8F) n = n + 0x70;
          break;
        }
      }
    }
    m[0] = n; target = target + String(m);
  }
  return target;
}

String convertCyr( const String &s ){
  String target = s;
  for( int idx = 0; idx<s.length(); idx++ ){
    target[idx] = getCharCyr( s[idx] );
  }
  return target;
}

uint8_t getCharCyr( uint8_t ch ){
  char rch = ch;
  switch (ch){
    case 0xC0: rch = 0x41; break;
    case 0xC1: rch = 0xA0; break;
    case 0xC2: rch = 0x42; break;
    case 0xC3: rch = 0xA1; break;
    case 0xC4: rch = 0xE0; break;
    case 0xC5: rch = 0x45; break;
    case 0xC6: rch = 0xA3; break;
    case 0xC7: rch = 0xA4; break;
    case 0xC8: rch = 0xA5; break;
    case 0xC9: rch = 0xA6; break;
    case 0xCA: rch = 0x4B; break;
    case 0xCB: rch = 0xA7; break;
    case 0xCC: rch = 0x4D; break;
    case 0xCD: rch = 0x48; break;
    case 0xCE: rch = 0x4F; break;
    case 0xCF: rch = 0xA8; break;

    case 0xD0: rch = 0x50; break;
    case 0xD1: rch = 0x43; break;
    case 0xD2: rch = 0x54; break;
    case 0xD3: rch = 0xA9; break;
    case 0xD4: rch = 0xAA; break;
    case 0xD5: rch = 0x58; break;
    case 0xD6: rch = 0xE1; break;
    case 0xD7: rch = 0xAB; break;
    case 0xD8: rch = 0xAC; break;
    case 0xD9: rch = 0xE2; break;
    case 0xDA: rch = 0xAD; break;
    case 0xDB: rch = 0xAE; break;
    case 0xDC: rch = 0x62; break;
    case 0xDD: rch = 0xAF; break;
    case 0xDE: rch = 0xB0; break;
    case 0xDF: rch = 0xB1; break;

    case 0xE0: rch = 0x61; break;
    case 0xE1: rch = 0xB2; break;
    case 0xE2: rch = 0xB3; break;
    case 0xE3: rch = 0xB4; break;
    case 0xE4: rch = 0xE3; break;
    case 0xE5: rch = 0x65; break;
    case 0xE6: rch = 0xB6; break;
    case 0xE7: rch = 0xB7; break;
    case 0xE8: rch = 0xB8; break;
    case 0xE9: rch = 0xB9; break;
    case 0xEA: rch = 0xBA; break;
    case 0xEB: rch = 0xBB; break;
    case 0xEC: rch = 0xBC; break;
    case 0xED: rch = 0xBD; break;
    case 0xEE: rch = 0x6F; break;
    case 0xEF: rch = 0xBE; break;

    case 0xF0: rch = 0x70; break;
    case 0xF1: rch = 0x63; break;
    case 0xF2: rch = 0xBF; break;
    case 0xF3: rch = 0x79; break;
    case 0xF4: rch = 0xE4; break;
    case 0xF5: rch = 0x78; break;
    case 0xF6: rch = 0xE5; break;
    case 0xF7: rch = 0xC0; break;
    case 0xF8: rch = 0xC1; break;
    case 0xF9: rch = 0xE6; break;
    case 0xFA: rch = 0xC2; break;
    case 0xFB: rch = 0xC3; break;
    case 0xFC: rch = 0xC4; break;
    case 0xFD: rch = 0xC5; break;
    case 0xFE: rch = 0xC6; break;
    case 0xFF: rch = 0xC7; break;

    case 0xA8: rch = 0xA2; break;
    case 0xB8: rch = 0xB5; break;
  }
  return rch;
}