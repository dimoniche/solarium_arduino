/*
  Управление соляриями
*/
#include <Wire.h>                                   // Подключаем библиотеку для работы с шиной I2C
#include <LiquidCrystal_I2C.h>                      // Подключаем библиотеку для работы с LCD
#include <EEPROMex.h>

// активный уровень кнопок
#define KEY_LEVEL       1

// ===============================задаем константы =========================================================================
const byte moneyPin = 2;                            // номер пина, к которому подключён купюроприемник, DB2
const byte inhibitPin = 4;                          // +Inhibit (зеленый) на купюроприемник, DB4
#if KEY_LEVEL == 1
const byte buttonPin_Start = 15;                    // номер входа, подключенный к кнопке "Старт", А0
const byte buttonPin_Service = 13;                  // номер входа, подключенный к кнопке "Сервис", А1
const byte LEDPin = 14;                             // номер выхода светодиода кнопки Старт, DB13
#else
const byte buttonPin_Start = 15;                    // номер входа, подключенный к кнопке "Старт", А0
const byte buttonPin_Service = 14;                  // номер входа, подключенный к кнопке "Сервис", А1
const byte LEDPin = 13;                             // номер выхода светодиода кнопки Старт, DB13
#endif

// ноги управления соляриями
const byte lamp_start_pin = 5;          // Запуск солярия Luxura. Включение ламп солярия FireSun, SunFlower
const byte vent_pin = 6;                // Включение вентиляторов солярия FireSun, SunFlower
const byte start_solarium_pin = 7;      // Удаленный старт от солярия, DB7

const byte Device_SerNum = 1;                       // серийный номер устройства
const PROGMEM char Device_Ver[] = "0.0";            // версия ПО устройства
const PROGMEM char Device_Date[] = "09/09/24";      // дата производства устройства
const unsigned long block = 500000;                 // блокировка устройства при превышении этой суммы денег

//======Переменные обработки клавиш=================
boolean lastReading[2] = {false, false};            // флаг предыдущего состояния кнопки
boolean buttonSingle[2] = {false, false};           // флаг состояния "краткое нажатие"
boolean buttonDouble[2] = {false, false};           // флаг состояния "двойное нажатие"
boolean buttonHold[2] = {false, false};             // флаг состояния "долгое нажатие"
unsigned long onTime[2] = {0, 0};                   // переменная обработки временного интервала
unsigned long lastSwitchTime[2] = {0, 0};           // переменная времени предыдущего переключения состояния

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
byte last_menu_cursor_index[MENU_INTER_COUNT];      // положение курсора на экране c предыдущего экрана
byte show_window_first_line = 0;                    // индекс первой отображаемой строки меню на экране
boolean need_reload_menu = true;                    // флаг перерисовки экрана
boolean need_clear_menu = false;                    // флаг очистки экрана
boolean need_hide_cursor = false;                   // флаг скытия курсора на экране
boolean start_edit_parameter = false;               // флаг старта редактирования параметра

const PROGMEM char sprintf_format[][SIZE_SCREEN_LINE*2] = {
  "ver %s %s",
  "Введите старый",
  "Введите пароль",
  "Введите новый",
  "Неверный пароль",
  "Пароль обновлен",
  " Сброс прошел",
  "Неверный пароль",
  "%s %d %s  ",
  "%04ld",
  "%s%04ld",
  "не требуется", //11
  "требуется",    //12
};

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

#define UV_REGIME                 0
#define COLLATEN_REGIME           1
#define UV_COLLATEN_REGIME        2

#define work_regime               5
#define signal_rele               6
#define weight_impulse            7
#define COUNT_BYTE_PARAMETER      8
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
};

#define long_starts_counter       0
#define long_money_counter        1
#define long_time_counter         2
#define short_starts_counter      3
#define short_money_counter       4
#define short_time_counter        5
#define money_counter             6
#define password                  7
#define serial_number             8
#define COUNT_LONG_PARAMETER      9
unsigned long all_long_parameters[COUNT_LONG_PARAMETER];

#define time_seance               0
#define time_delay                0
#define stage_password            0
#define version_date              0
#define service_line              1
#define COUNT_TEXT_PARAMETER      2
char text_parameters[COUNT_TEXT_PARAMETER][SIZE_SCREEN_LINE*2];

LiquidCrystal_I2C lcd(0x27, SIZE_SCREEN_LINE, SIZE_SCREEN);                 // устанавливаем адрес 0x27, и дисплей 16 символов 2 строки

void read_buttons(byte x)
{
  #if KEY_LEVEL == 1
  boolean reading = !digitalRead(x);
  #else
  boolean reading = digitalRead(x);
  #endif

  int index = (x == buttonPin_Service ? 0 : 1);

  if (reading && !lastReading[index])              // проверка первичного нажатия
  {
    onTime[index] = millis();
    !index ? Serial.println("click button Service") : Serial.println("click button Start"); 
  }
  if (reading && lastReading[index])               // проверка удержания
  {
    if ((millis() - onTime[index]) > holdTime)
    {
      buttonHold[index] = true;
      !index ? Serial.println("buttonHold Service") : Serial.println("buttonHold Start");
      digitalWrite(LEDPin, !digitalRead(LEDPin));   // при удержании кнопки мигает светодиод
      isButtonHoldRepeate(x);
    }
  }
  if (!reading && lastReading[index])              // проверка отпускания кнопки
  {
    if (((millis() - onTime[index]) > bounceTime) && !buttonHold[index])
    {
      if ((millis() - lastSwitchTime[index]) >= doubleTime)
      {
        lastSwitchTime[index] = millis();
        buttonSingle[index] = true;
        !index ? Serial.println("buttonSingle Service") : Serial.println("buttonSingle Start");
      }
      else
      {
        lastSwitchTime[index] = millis();
        buttonDouble[index] = true;
        !index ? Serial.println("buttonDouble Service") : Serial.println("buttonDouble Start");
        buttonSingle[index] = false;
        buttonDouble[index] = false;                 // сброс состояния после выполнения команды
        isButtonDouble(x);
      }
    }
    if (buttonHold[index])
    {
      buttonDouble[index] = false;
      buttonHold[index] = false;                     // сброс состояния после выполнения команды
      isButtonHold(x);
    }
  }
  lastReading[index] = reading;
  if (buttonSingle[index] && (millis() - lastSwitchTime[index]) > doubleTime)
  {
    buttonDouble[index] = false;
    buttonSingle[index] = false;                     // сброс состояния после выполнения команды
    isButtonSingle(x);
  }
}

#define MAIN_MENU             0
#define SETTING_MENU          1
#define STATISTIC_MENU        2
#define SOLARIUM_MENU_PAUSE   3
#define BANK_MENU             4
#define PASSWORD_MENU         5
#define LONG_COUNTER_MENU     6
#define SHORT_COUNTER_MENU    7
#define SOLARIUM_MENU         8
#define SOLARIUM_MENU_PAY     9
#define SOLARIUM_MENU_DEV     10
#define RESET_DEVICE_MENU     11
#define RESET_COUNTER_MENU    12
#define DEVICE_SETTING_MENU   13

enum type_menu_line {
  MENU_LINE = 0,
  TEXT_LINE,
  DIGIT_PARAM_LINE,
  FIXED_LINE,
  LIST_PARAM_LINE,
  TEXT_PARAM_LINE,
  DIGIT_VIEW_LINE,
  PASSWORD_SET_LINE,
  PASSWORD_VERIFY_LINE,
  INTEGER_PARAM_LINE,
  STRING_PARAM_LINE,
  DIGIT_INT_VIEW_LINE,
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
  char list_data[4][11];
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
    menu_line menu_lines[5];
    byte count_lines;
};

// текущее меню
menu_screen current_menu_screen;

#define WAIT_MONEY              0
#define WAIT_BEFORE             1
#define SEANCE_SCREEN           2
#define WAIT_AFTER              3
#define SCREEN_START_SOL        4

/*
  Описание основного меню
*/
const menu_screen menu_main[] PROGMEM = {
  // Меню внесения денег и отображения времени сеанкса
  {
    {
      {
        " ЦЕНА",
        DIGIT_INT_VIEW_LINE,
        {
          price,
          {
              0,
              0,
          },
          "руб/мин"
        }
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
      {
        "",
        TEXT_PARAM_LINE,
        {
          service_line,
          {
              0,
              0,
          },
          " "
        }
      },
    },
    4
  },
  // Время задержки до
  {
    {
      {
        " ДО СТАРТА",
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
        " СЕАНС ЗАГАРА",
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
        "  ВЕНТИЛЯЦИЯ",
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
  // Удаленный запуск
  {
    {
      {
        "",
        FIXED_LINE,
        {0}
      },
      {
        "ОТЛОЖЕННЫЙ СТАРТ",
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
        "Устройство",
        MENU_LINE,
        {DEVICE_SETTING_MENU}
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
    4
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
        "Сброс настроек",
        MENU_LINE,
        {RESET_DEVICE_MENU}
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
        "Обнуление",
        MENU_LINE,
        {RESET_COUNTER_MENU}
      },
    },
    4
  },
  // Меню 3
  {
    {
      {
        "COЛЯРИЙ ПАУЗА",
        FIXED_LINE,
        {1}
      },
      {
        "Пaузa дo",
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
        "Пaузa пocлe",
        DIGIT_PARAM_LINE,
        {
          pause_after,
          {
              0,
              3,
          },
          "mин"
        }
      },
    },
    3
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
        "ПАРОЛЬ",
        FIXED_LINE,
        {0}
      },
      {
        "",
        PASSWORD_SET_LINE,
        {
          password,
          {
              0,
              9999,
          },
          " "
        }
      },
      {
        "",
        TEXT_PARAM_LINE,
        {
          stage_password,
          {
              0,
              0,
          },
          ""
        },
      },
    },
    3
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
  // Меню 8
  {
    {
      {
        "COЛЯРИЙ",
        FIXED_LINE,
        {1}
      },
      {
        "Пауза",
        MENU_LINE,
        {SOLARIUM_MENU_PAUSE}
      },
      {
        "Цена",
        MENU_LINE,
        {SOLARIUM_MENU_PAY}
      },
      {
        "Доп.настройки",
        MENU_LINE,
        {SOLARIUM_MENU_DEV}
      },
    },
    4
  },
  // Меню 9
  {
    {
      {
        "COЛЯРИЙ ЦЕНА",
        FIXED_LINE,
        {1}
      },
      {
        "Цeнa",
        DIGIT_PARAM_LINE,
        {
          price,
          {
              0,
              100,
          },
          "руб/мин"
        }
      },
    },
    2
  },
  // Меню 10
  {
    {
      {
        "ДОП.НАСТРОЙКИ",
        FIXED_LINE,
        {1}
      },
      {
        "Отложен.старт",
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
              "Luxura   ",
              "FS UV    ",
              "FS UV+K  ",
              "SunFlower"
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
              "UV      ",
              "UV+Koll "
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
    5
  },
  // Меню 11
  {
    {
      {
        "",
        FIXED_LINE,
        {1}
      },
      {
        "",
        PASSWORD_VERIFY_LINE,
        {
          password,
          {
              0,
              9999,
          },
          ""
        }
      },
      {
        "",
        TEXT_PARAM_LINE,
        {
          stage_password,
          {
              0,
              0,
          },
          ""
        },
      },
    },
    3
  },
  // Меню 12
  {
    {
      {
        "",
        FIXED_LINE,
        {1}
      },
      {
        "",
        PASSWORD_VERIFY_LINE,
        {
          password,
          {
              0,
              9999,
          },
          ""
        }
      },
      {
        "",
        TEXT_PARAM_LINE,
        {
          stage_password,
          {
              0,
              0,
          },
          ""
        },
      },
    },
    3
  },
  // Меню 13
  {
    {
      {
        "УСТРОЙСТВО",
        FIXED_LINE,
        {1}
      },
      {
        "SN",
        DIGIT_VIEW_LINE,
        {
          serial_number,
          {
              0,
              9999,
          },
          ""
        }
      },
      {
        "",
        TEXT_PARAM_LINE,
        {
          version_date,
          {
              0,
              0,
          },
          ""
        }
      },
      {
        "Обсл.",
        TEXT_PARAM_LINE,
        {
          service_line,
          {
              0,
              0,
          },
          ""
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

// временный пароль для проверки
unsigned long temp_password = 0;
// уровень проверки пароля
byte password_stage = 0;
// текущая редактируемая цифра пароля
int current_digit = 0;
/*
  удержание кнопки на повторе
*/
void isButtonHoldRepeate(byte x)
{
    need_reload_menu = true;

    if(x == buttonPin_Start)
    {
        if(start_edit_parameter)
        {
            Serial.println("isButtonHoldRepeate");
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
        return;
    }
}

/*
  удержание кнопки
*/
void isButtonHold(byte x)
{
  need_reload_menu = true;

  if(x == buttonPin_Start)
  {
      return;
  }

  if(!menu_enable && (all_long_parameters[money_counter] == 0) || (bill_enable == false))
  {   // в меню входим только если нет внесенных денег и не запрещен прием денег, тк идет работа соляриев
      menu_index = MAIN_MENU;

      memcpy_P( &current_menu_screen, &menu_settings[menu_index], sizeof(menu_screen));
      find_first_line_menu();

      menu_enable = true;
  }
  else
  {
      if(!start_edit_parameter)
      {
          if(menu_index == MAIN_MENU) 
          {
              menu_enable = false;
              sprintf(text_parameters[stage_password],"");
          }
          else
          {
              menu_inter--;
              menu_index = last_menu_index[menu_inter];

              memcpy_P( &current_menu_screen, &menu_settings[menu_index], sizeof(menu_screen));

              cursor_index = (last_menu_cursor_index[menu_inter] >= SIZE_SCREEN ) ? SIZE_SCREEN - 1 : last_menu_cursor_index[menu_inter];
              current_line_index = last_menu_cursor_index[menu_inter];
              show_window_first_line = 0;

              digitalWrite(LEDPin, HIGH);
              lcd.clear();
          }
      } else {
          if(current_menu_screen.menu_lines[current_line_index].type == DIGIT_PARAM_LINE
          || current_menu_screen.menu_lines[current_line_index].type == LIST_PARAM_LINE
          || current_menu_screen.menu_lines[current_line_index].type == PASSWORD_SET_LINE
          || current_menu_screen.menu_lines[current_line_index].type == PASSWORD_VERIFY_LINE)
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
            else if(current_menu_screen.menu_lines[current_line_index].type == PASSWORD_SET_LINE)
            {
                if(password_stage == 0)
                {
                    if(temp_password == all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index])
                    {
                        start_edit_parameter = true;
                        hide_cursor();
                        need_hide_cursor = true;

                        char format[SIZE_SCREEN_LINE*2];
                        memcpy_P( &format, &sprintf_format[3], SIZE_SCREEN_LINE*2);
                        sprintf(text_parameters[stage_password], format);
                        all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] = 0;
                        password_stage = 1;
                    }
                    else
                    {
                        char format[SIZE_SCREEN_LINE*2];
                        memcpy_P( &format, &sprintf_format[4], SIZE_SCREEN_LINE*2);
                        sprintf(text_parameters[stage_password], format);
                        password_stage = 0;
                        all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] = temp_password;
                    }
                }
                else if(password_stage == 1)
                {
                    password_stage = 0;
                    save_long_parameter(current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index);

                    char format[SIZE_SCREEN_LINE*2];
                    memcpy_P( &format, &sprintf_format[5], SIZE_SCREEN_LINE*2);
                    sprintf(text_parameters[stage_password], format);
                }
            }
            else if(current_menu_screen.menu_lines[current_line_index].type == PASSWORD_VERIFY_LINE)
            {
                if(temp_password == all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index])
                {
                    char format[SIZE_SCREEN_LINE*2];
                    memcpy_P( &format, &sprintf_format[6], SIZE_SCREEN_LINE*2);
                    sprintf(text_parameters[stage_password], format);

                    if(menu_index == RESET_DEVICE_MENU)
                    {
                        reset_parameter();
                        Serial.println("reset_parameter");
                    }
                    else if(menu_index == RESET_COUNTER_MENU)
                    {
                        reset_short_counters();
                        Serial.println("reset_short_counters");
                    }
                }
                else
                {
                    char format[SIZE_SCREEN_LINE*2];
                    memcpy_P( &format, &sprintf_format[7], SIZE_SCREEN_LINE*2);
                    sprintf(text_parameters[stage_password], format);
                }

                all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] = temp_password;
            }
          }
      }
  }
}

/*
  одиночное нажатие кнопки
*/
void isButtonSingle(byte x) 
{
  need_reload_menu = true;

  if(start_edit_parameter && x == buttonPin_Start)
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

      if(current_menu_screen.menu_lines[current_line_index].type == PASSWORD_SET_LINE
      || current_menu_screen.menu_lines[current_line_index].type == PASSWORD_VERIFY_LINE)
      {
          if(--current_digit == 0) { current_digit = 4; }
      }
  }
  else if(start_edit_parameter && x == buttonPin_Service)
  {
      if(current_menu_screen.menu_lines[current_line_index].type == PASSWORD_SET_LINE
      || current_menu_screen.menu_lines[current_line_index].type == PASSWORD_VERIFY_LINE)
      {
          byte dig = current_digit - 1;
          int scale = 1;

          while(dig--) { scale *= 10; }

          switch(current_digit - 1)
          {
             case 0:
                if(all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] % 10 >= 9) scale = -9;
             break;
             case 1:
                if(all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] % 100 >= 90) scale = -90;
             break;
             case 2:
                if(all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] % 1000 >= 900) scale = -900;
             break;
             case 3:
                if(all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] % 10000 >= 9000) scale = -9000;
             break;
          }

          all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] += scale;
      }
  }
  else if(x == buttonPin_Service)
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
void isButtonDouble(byte x) 
{
    need_reload_menu = true;

    if(x == buttonPin_Start) {
        return;
    }
    
    if(current_menu_screen.menu_lines[current_line_index].type == MENU_LINE)
    {
        sprintf(text_parameters[stage_password],"");
 
        last_menu_index[menu_inter] = menu_index;
        last_menu_cursor_index[menu_inter] = current_line_index;
        menu_inter++;
        menu_index = current_menu_screen.menu_lines[current_line_index].parameter.menu.next_menu_index;

        if(menu_index == DEVICE_SETTING_MENU)
        {
            char format[SIZE_SCREEN_LINE*2];
            memcpy_P( &format, &sprintf_format[0], SIZE_SCREEN_LINE);
            char ver[4];
            memcpy_P( &ver, Device_Ver, 4 );
            char date[9];
            memcpy_P( &date, Device_Date, 9 );

            sprintf(text_parameters[version_date],format, ver, date);

            if(block > all_long_parameters[long_money_counter]) {
              memcpy_P( &format, &sprintf_format[11], SIZE_SCREEN_LINE*2);
              sprintf(text_parameters[service_line],format);
            } else {
              memcpy_P( &format, &sprintf_format[12], SIZE_SCREEN_LINE*2);
              sprintf(text_parameters[service_line],format);             
            }
        }

        memcpy_P( &current_menu_screen, &menu_settings[menu_index], sizeof(menu_screen));
        find_first_line_menu();
        lcd.clear();
    }
    else if(current_menu_screen.menu_lines[current_line_index].type == DIGIT_PARAM_LINE
         || current_menu_screen.menu_lines[current_line_index].type == LIST_PARAM_LINE
         || current_menu_screen.menu_lines[current_line_index].type == PASSWORD_SET_LINE
         || current_menu_screen.menu_lines[current_line_index].type == PASSWORD_VERIFY_LINE)
    {
        if(!start_edit_parameter)
        {
            if(current_menu_screen.menu_lines[current_line_index].type == PASSWORD_SET_LINE)
            {
                temp_password = all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index];
                all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] = 0;
                password_stage = 0;

                char format[SIZE_SCREEN_LINE*2];
                memcpy_P( &format, &sprintf_format[1], SIZE_SCREEN_LINE*2);
                sprintf(text_parameters[stage_password], format);
                current_digit = 4;
            }
            else if(current_menu_screen.menu_lines[current_line_index].type == PASSWORD_VERIFY_LINE)
            {
                temp_password = all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index];
                all_long_parameters[current_menu_screen.menu_lines[current_line_index].parameter.digit.param_index] = 0;
                password_stage = 0;

                char format[SIZE_SCREEN_LINE*2];
                memcpy_P( &format, &sprintf_format[2], SIZE_SCREEN_LINE*2);
                sprintf(text_parameters[stage_password], format);
                current_digit = 4;
            }

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
        char format[11];
        memcpy_P( &format, &sprintf_format[8], 11);
        if(start_edit_parameter && index_line == current_line_index)
        {
           format[2] = '>';
        }
        sprintf(line,format, current_menu_screen.menu_lines[index_line].string, 
                             all_byte_parameters[current_menu_screen.menu_lines[index_line].parameter.digit.param_index], 
                             current_menu_screen.menu_lines[index_line].parameter.digit.unit);
        lcd.print(convertCyr( utf8rus( line )));
    }
    else if(current_menu_screen.menu_lines[index_line].type == PASSWORD_SET_LINE)
    {
        char line[SIZE_SCREEN_LINE * 2];
        if(start_edit_parameter && index_line == current_line_index)
        {
          char format[6];
          memcpy_P( &format, &sprintf_format[9], 6);
          sprintf(line,format, all_long_parameters[current_menu_screen.menu_lines[index_line].parameter.digit.param_index]);
        }
        else
        {
          sprintf(line,"****");
        }
        lcd.print(convertCyr( utf8rus( line )));  
    }
    else if(current_menu_screen.menu_lines[index_line].type == PASSWORD_VERIFY_LINE)
    {
        char line[SIZE_SCREEN_LINE * 2];
        if(start_edit_parameter && index_line == current_line_index)
        {
          char format[8];
          memcpy_P( &format, &sprintf_format[10], 8);
          sprintf(line,format, "      ", all_long_parameters[current_menu_screen.menu_lines[index_line].parameter.digit.param_index]);
        }
        else
        {
          sprintf(line,"      0000");
        }
        lcd.print(convertCyr( utf8rus( line )));        
    }
    else if(current_menu_screen.menu_lines[index_line].type == LIST_PARAM_LINE)
    {
        char line[SIZE_SCREEN_LINE * 2];
        char format[8] = "%s %s  ";
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
    else if(current_menu_screen.menu_lines[index_line].type == DIGIT_INT_VIEW_LINE)
    {
        char line[SIZE_SCREEN_LINE * 2];
        sprintf(line,"%s %d %s", current_menu_screen.menu_lines[index_line].string,
                                 all_byte_parameters[current_menu_screen.menu_lines[index_line].parameter.digit.param_index],
                                 current_menu_screen.menu_lines[index_line].parameter.digit.unit);
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

/*
  Запуск работы соляриев
*/
void start_solarium_work()
{
    switch(all_byte_parameters[solarium_type])
    {
        case LUXURA_SOL:
          if(all_byte_parameters[signal_rele]) digitalWrite(lamp_start_pin, HIGH);
          else digitalWrite(lamp_start_pin, LOW);
        break;
        case FIRESUN_UV_SOL:
          digitalWrite(vent_pin, HIGH);
          digitalWrite(lamp_start_pin, HIGH);
          delay(500);
          digitalWrite(lamp_start_pin, LOW);
          delay(1000);
        break;
        case FIRESUN_UV_K_SOL:
          digitalWrite(vent_pin, HIGH);
          digitalWrite(lamp_start_pin, HIGH);
          switch(all_byte_parameters[work_regime])
          {
              case UV_REGIME:
                delay(500);
                digitalWrite(lamp_start_pin, LOW);
                delay(1000);
              break;
              case COLLATEN_REGIME:
                delay(500);
                digitalWrite(lamp_start_pin, LOW);
                delay(500);
                digitalWrite(lamp_start_pin, HIGH);
                delay(500);
                digitalWrite(lamp_start_pin, LOW);
                delay(500);
              break;
              case UV_COLLATEN_REGIME:
                delay(500);
                digitalWrite(lamp_start_pin, LOW);
                delay(500);
              break;
          }
          digitalWrite(lamp_start_pin, HIGH);
        break;
        case SUNFLOWER_SOL:
          digitalWrite(vent_pin, HIGH);
          digitalWrite(lamp_start_pin, HIGH);
        break;
    }
}

/*
    Остановка ламп соляриев
*/
void stop_solarium_work()
{
    switch(all_byte_parameters[solarium_type])
    {
      case LUXURA_SOL:
        if(all_byte_parameters[signal_rele]) digitalWrite(lamp_start_pin, LOW);
        else digitalWrite(lamp_start_pin, HIGH);
      break;
      default:
        digitalWrite(lamp_start_pin, LOW);
      break;
    }
}

/*
    Остановка вентилятора
*/
void stop_vent_work()
{
    switch(all_byte_parameters[solarium_type])
    {
        case LUXURA_SOL:
        break;
        default:
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
        sprintf(text_parameters[service_line]," НАЖМИТЕ СТАРТ");

        digitalWrite(LEDPin, HIGH);                     // зажигаем светодиод 

        #if KEY_LEVEL == 1
        if (digitalRead(buttonPin_Start) == LOW)
        #else
        if (digitalRead(buttonPin_Start) == HIGH)
        #endif
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

          if(all_byte_parameters[remote_start])
          {
              // удаленный старт кнопкой
              memcpy_P( &current_menu_screen, &menu_main[SCREEN_START_SOL], sizeof(menu_screen));
              lcd.clear();
              show_menu();

              while(1)
              {
                  if (digitalRead(start_solarium_pin) == LOW)
                  {
                      break;
                  }
                  delay(1);
              }
          }
          else
          {
              // задержка до запуска
              memcpy_P( &current_menu_screen, &menu_main[WAIT_BEFORE], sizeof(menu_screen));
              sprintf(text_parameters[time_delay],"%2d", all_byte_parameters[pause_before]);
              lcd.clear();
              show_menu();

              for(int i = 0; i < all_byte_parameters[pause_before]; i++)
              {
                  delay(1000);
                  sprintf(text_parameters[time_delay],"%2d", all_byte_parameters[pause_before] - i);
                  show_menu();
              }
          }

          memcpy_P( &current_menu_screen, &menu_main[SEANCE_SCREEN], sizeof(menu_screen));
          menu_index = 2;
          sprintf(text_parameters[time_seance]," %02d:%02d", minute, second);
          need_clear_menu = true;
          need_reload_menu = true;

          bill_enable = !bill_enable;                   // устанавливаем флаг: не принимаем деньги

          // Запускаем работу солярия
          start_solarium_work();
        }
    }
    else
    {
       sprintf(text_parameters[service_line]," Внесите оплату");
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
      read_buttons(buttonPin_Start);

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

/*
    Событие полусекунды
*/
void one_half_second()
{

}

void restart_menu()
{
    memcpy_P( &current_menu_screen, &menu_main[WAIT_MONEY], sizeof(menu_screen));
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

    if(menu_index == WAIT_AFTER && time_remain == 0)
    {
        stop_vent_work();
        restart_menu();

        need_clear_menu = true;
        need_reload_menu = true;
    }
    if(menu_index == SEANCE_SCREEN && time_remain == 0)
    {
        stop_solarium_work();

        if(all_byte_parameters[solarium_type] == LUXURA_SOL)
        {
            restart_menu();
        }
        else
        {
            memcpy_P( &current_menu_screen, &menu_main[WAIT_AFTER], sizeof(menu_screen));
            menu_index = WAIT_AFTER;
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
  pinMode(start_solarium_pin, INPUT_PULLUP);        // удаленный старт солярия

  digitalWrite(LEDPin,LOW);                         // изначально светодиод погашен
  digitalWrite(inhibitPin, LOW);                    // изначально разрешаем прием купюр
  digitalWrite(lamp_start_pin, LOW);                // изначально выключен
  digitalWrite(vent_pin, LOW);                      // изначально выключен

  load_parameter();
  memcpy_P( &current_menu_screen, &menu_main[WAIT_MONEY], sizeof(menu_screen));
  sprintf(text_parameters[time_seance],"");
  menu_index = 0;

  #if KEY_LEVEL == 1
  if(!digitalRead(buttonPin_Start))
  #else
  if(digitalRead(buttonPin_Start))
  #endif
  {   // сброс пароля по умолчанию
      all_long_parameters[password] = 1111;
      save_long_parameter(password);

      Serial.println("reset password");
  }

  all_long_parameters[money_counter] = 0;
  all_long_parameters[serial_number] = Device_SerNum;
}

void loop() 
{
  read_buttons(buttonPin_Service);
  //read_buttons(buttonPin_Start);

  hide_cursor();
  need_hide_cursor = true;

  if (menu_enable == true)
  {
      menu();
      need_reload_menu = true;
      memcpy_P( &current_menu_screen, &menu_main[WAIT_MONEY], sizeof(menu_screen));
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