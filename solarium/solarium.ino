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
//const byte RelayPin = 17;                         // номер выхода, подключенный к реле, А3
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

// Переменные для работы с соляриями
#define pause_before              0
#define pause_after               1
#define price                     2
#define remote_start              3
#define solarium_type             4
#define work_regime               5
#define signal_rele               6
#define weight_impulse            7
#define reset_device              8
#define COUNT_BYTE_PARAMETER      9
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
  0
};

#define long_starts_counter       0
#define long_money_counter        1
#define long_time_counter         2
#define short_starts_counter      3
#define short_money_counter       4
#define short_time_counter        5
#define impulse_counter           6
#define COUNT_LONG_PARAMETER      7
unsigned long all_long_parameters[COUNT_LONG_PARAMETER];

#define time_seance               0
#define time_delay                1
#define time_delay                1
#define COUNT_TEXT_PARAMETER      2
char text_parameters[COUNT_TEXT_PARAMETER][6];

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
  char unit[5];
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
  char unit[5];
};

union param_data {
    parameter_list list;
    parameter_digit digit;
    parameter_text text;
    parameter_header header;
    parameter_menu menu;
};

struct menu_line {
  char string[SIZE_SCREEN_LINE];
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
        "     BHECEHO",
        DIGIT_VIEW_LINE,
        {
          impulse_counter,
          {
              0,
              0,
          },
          "rub"
        }
      },
      {
        "",
        DIGIT_VIEW_LINE,
        {
          time_seance,
          {
              0,
              0,
          },
          ""
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
          "CEK"
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
        "     CEAHC",
        FIXED_LINE,
        {0}
      },
      {
        "    ",
        DIGIT_VIEW_LINE,
        {
          time_seance,
          {
              0,
              0,
          },
          "MUH"
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
        "     KOHEU",
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
        {BANK_MENU}
      },
      {
        "Password",
        MENU_LINE,
        {PASSWORD_MENU}
      },
      {
        "Reset",
        LIST_PARAM_LINE,
        {
          reset_device,
          {
              0,
              1,
          },
          {
              "     ",
              "start"
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
        "    STATISTIC",
        FIXED_LINE,
        {0}
      },
      {
        "Long counters",
        MENU_LINE,
        {LONG_COUNTER_MENU}
      },
      {
        "Short counters",
        MENU_LINE,
        {SHORT_COUNTER_MENU}
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
          },
          {
              "Off",
              "On "
          }
        }
      },
      {
        "Type",
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
        "Regime",
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
        "Signal",
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
        "    BANK",
        FIXED_LINE,
        {0}
      },
      {
        "Rub/imp",
        DIGIT_PARAM_LINE,
        {
          weight_impulse,
          {
              0,
              100,
          },
          ""
        }
      },
    },
    2
  },
  // Меню 5
  {
    {
      {
        "    PASSWORD",
        FIXED_LINE,
        {0}
      },
      {
        "",
        DIGIT_PARAM_LINE,
        {
          weight_impulse,
          {
              0,
              100,
          },
          ""
        }
      },
    },
    2
  },
  // Меню 6
  {
    {
      {
        "LONG COUNTERS",
        FIXED_LINE,
        {0}
      },
      {
        "Starts",
        DIGIT_VIEW_LINE,
        {
          long_starts_counter,
          {
              0,
              0,
          },
          ""
        }
      },
      {
        "Money",
        DIGIT_VIEW_LINE,
        {
          long_money_counter,
          {
              0,
              0,
          },
          "rub"
        }
      },
      {
        "Time",
        DIGIT_VIEW_LINE,
        {
          long_time_counter,
          {
              0,
              0,
          },
          "sec"
        }
      },
    },
    4
  },
  // Меню 7
  {
    {
      {
        "SHORT COUNTERS",
        FIXED_LINE,
        {0}
      },
      {
        "Starts",
        DIGIT_VIEW_LINE,
        {
          short_starts_counter,
          {
              0,
              0,
          },
          ""
        }
      },
      {
        "Money",
        DIGIT_VIEW_LINE,
        {
          short_money_counter,
          {
              0,
              0,
          },
          "rub"
        }
      },
      {
        "Time",
        DIGIT_VIEW_LINE,
        {
          short_time_counter,
          {
              0,
              0,
          },
          "sec"
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
    
    all_long_parameters[impulse_counter] = 0;
    save_long_parameter(impulse_counter);
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
                             all_byte_parameters[current_menu_screen.menu_lines[index_line].parameter.digit.param_index], 
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
                             current_menu_screen.menu_lines[index_line].parameter.list.list_data[all_byte_parameters[current_menu_screen.menu_lines[index_line].parameter.list.param_index]]);
        lcd.print(line);       
    }
    else if(current_menu_screen.menu_lines[index_line].type == DIGIT_VIEW_LINE)
    {
        char line[21];
        sprintf(line,"%s %ld %s", current_menu_screen.menu_lines[index_line].string, 
                                 all_long_parameters[current_menu_screen.menu_lines[index_line].parameter.digit.param_index], 
                                 current_menu_screen.menu_lines[index_line].parameter.digit.unit);
        lcd.print(line);        
    }
    else if(current_menu_screen.menu_lines[index_line].type == TEXT_PARAM_LINE)
    {
        char line[21];
        sprintf(line,"%s %s %s", current_menu_screen.menu_lines[index_line].string, 
                                  text_parameters[current_menu_screen.menu_lines[index_line].parameter.text.param_index], 
                                  current_menu_screen.menu_lines[index_line].parameter.text.unit);
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

/* 
 * Функция подсчета импульсов от купюроприемника
 * Когда импульсов нет, записываются значения reading = trueState = lastState = HIGH, при поступлении импульса reading = LOW,
 * фиксируется время появления импульса в lastStateChangeTime. Если длительность импульса > debounceDelay (время дребезга),
 * значит это полезный импульс, значения изменяются reading = trueState = lastState = LOW
 */
void read_money_impulse ()
{
  int reading = digitalRead(moneyPin);
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
          all_long_parameters[impulse_counter] += all_byte_parameters[weight_impulse];
      }
    }
  }
  lastState = reading;   
} 

/*
  Прием денег
*/
void get_money ()
{
    read_money_impulse ();

    minute = all_long_parameters[impulse_counter] / all_byte_parameters[price];
    remain = all_long_parameters[impulse_counter] % all_byte_parameters[price];
    second = remain * 60 / all_byte_parameters[price];

    if (all_long_parameters[impulse_counter] >= all_byte_parameters[price])
    {
        // достаточно денег для оказания услуги
        sprintf(text_parameters[time_seance]," CEAHC %02d:%02d MUH", minute, second);

        digitalWrite(LEDPin, HIGH);                     // зажигаем светодиод 


        if (digitalRead(buttonPin_Start) == HIGH)
        {
          digitalWrite(inhibitPin, HIGH);               // выставляем запрет приема монет 
          digitalWrite(LEDPin, LOW);                    // гасим светодиод  
          lcd.clear();

          sprintf(text_parameters[time_seance],"%2d", all_byte_parameters[pause_before]);
          delay(all_byte_parameters[pause_before]);

          bill_enable =! bill_enable;                   // устанавливаем флаг: не принимаем деньги
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
          reset_parameter();
          all_byte_parameters[reset_device] = 0;
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

/*
    Событие секунды
*/
void second_event()
{

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

void setup() {
  Serial.begin(115200);

  lcd.init();                                       // инициализация LCD
  lcd.backlight();                                  // включаем подсветку
  lcd.createChar(0, rubl);                          // создаем символ и записываем его в память LCD по 0 адресу

  pinMode(inhibitPin, OUTPUT);                      // устанавливает режим работы - выход
  pinMode(moneyPin, INPUT_PULLUP);                  // устанавливает режим работы - вход, подтягиваем к +5В через встроенный подтягивающий резистор (на всякий случай)
  pinMode(LEDPin, OUTPUT);                          // инициализируем пин, подключенный к светодиоду, как выход
  pinMode(buttonPin_Service, INPUT_PULLUP);         // инициализируем пин, подключенный к кнопке, как вход
  pinMode(buttonPin_Start, INPUT_PULLUP);           // инициализируем пин, подключенный к кнопке, как вход

  digitalWrite(LEDPin,LOW);                         // изначально светодиод погашен
  digitalWrite(inhibitPin, LOW);                    // изначально разрешаем прием купюр

  load_parameter();
  memcpy_P( &current_menu_screen, &menu_main[0], sizeof(menu_screen));
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
