#include "common.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <iostream>
#include <sstream>
#include <vector>
#include <cstdint>

using namespace std::literals;

const int LETTERS = 26;
const int MAX_POSITION_LENGTH = 17;
const int MAX_POS_LETTER_COUNT = 3;
const int MAX_POS_DIGITS_COUNT = 5;

const Position Position::NONE = {-1, -1};

const std::vector<char> letters = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
const std::string max_position_string = "XFD16384"s;


struct UserPosition {
    std::string column;
    int row = -1;

    bool operator==(UserPosition rhs) const {
        return (this->column == rhs.column) && (this->row == rhs.row);
    }

    static const UserPosition NONE;
};
const UserPosition UserPosition::NONE = {""s, -1};

// Реализуйте методы:
bool Position::operator==(const Position rhs) const {
    return (rhs.col == this->col) && (rhs.row == this->row);
}

bool Position::operator<(const Position rhs) const {
    // учитывается, что столбец или строка могут быть равны
    bool var_1 = this->col <= rhs.col && this->row < rhs.row;
    bool var_2 = this->col < rhs.col && this->row <= rhs.row;
    return var_1 || var_2;
}

bool Position::IsValid() const {
    bool is_negative = (row < 0) || (col < 0);
    bool is_exceed = (row >= MAX_ROWS) || (col >= MAX_COLS);
    return !is_negative && !is_exceed && !(*this == Position::NONE);
}




std::deque<int> ConvertDecimalTo26LettersSystem(int num_10) {
    int remainder = num_10;  // остаток
    int dividend = num_10;  // делимое
    std::deque<int> num_26_indexes;

    while (dividend >= LETTERS) {
        int quotient = dividend / LETTERS;  // частное
        remainder = dividend - quotient * LETTERS;  // новый остаток от деления
        dividend = quotient - 1;  // следующее деление
        // добавляем в массив 
        num_26_indexes.push_front(remainder);
    }
    
    num_26_indexes.push_front(dividend);
    return num_26_indexes;
}

std::string Position::ToString() const {
    if (!this->IsValid()) {
        return ""s;
    }
    std::string row_str = std::to_string(row + 1);
    std::string col_str;
    std::deque<int> num_26_letters_indexes = ConvertDecimalTo26LettersSystem(col);
    for (int num : num_26_letters_indexes) {
        col_str += letters.at(num);
    }
    return col_str + row_str;
}





// Разделяет буквенно-цифровое обозначение на название колонки и строки.
// Если исходная строка (название) некорректное вернет структуру UserPosition::NONE = {""s, -1} )с row = -1) 
UserPosition DivideStringToColumnAndRow(const std::string_view str) {
    std::string col;
    std::string row;

    bool is_letter_begin = false;
    bool is_digit_begin = false;

    uint8_t n_letters = 0;
    uint8_t n_digits = 0;

    UserPosition res = UserPosition::NONE;
    
    for (const char& ch : str) {
        // начало с букв
        if (isalpha(static_cast<unsigned char>(ch))) {
            if (is_digit_begin) {
                return UserPosition::NONE;
            }

            is_letter_begin = true;
            
            // проверяем, что буква заглавная
            if (!isupper(static_cast<unsigned char>(ch))) {
                return UserPosition::NONE;
            }
            // прибавляем новую букву к названию колонки
            col += ch;
            
            // проверка, что не зашкаливаем по количеству букв в названии столбца
            if (++n_letters > MAX_POS_LETTER_COUNT) {
                return UserPosition::NONE;
            }
        }
        else if (isdigit(static_cast<unsigned char>(ch))) {
            // если цифра в начале, то ошибка
            if (!is_letter_begin && !is_digit_begin) {
                return UserPosition::NONE;
            }
            // начинаем писать цифры
            is_letter_begin = false;
            is_digit_begin = true; 
            // добавляем цифру к номеру строки
            row += ch;

            // проверка, что не зашкаливаем по количеству цифр в номере строки
            if (++ n_digits > MAX_POS_DIGITS_COUNT) {
                return UserPosition::NONE;
            }
        }
        // какой-то неразрешенный символ
        else {
            // std::cerr << "Invalid charachter in cell's name" << std::endl;
            return UserPosition::NONE;
        }
    }
    // проверяем, что были цифры и буквы:
    if (col.empty() || row.empty()) {
        return UserPosition::NONE;
    }

    res.column = col;
    res.row = std::stoi(row);
    return res;
}

int ConvertColNameToDecIndex(std::string_view column_str) {
    uint8_t n = 0;
    int number = 0; 
    int letter_index;
    // будем идти с конца и добавлять 26 в нужной степени
    for (auto it = column_str.rbegin(); it != column_str.rend(); it++) {
        char cur_letter = *it;
        // определяем индекс элемента = значение, соответствующее данной букве
        auto pos = std::lower_bound(letters.begin(), letters.end(), cur_letter);
        if (pos == letters.end()) {
            // std::cerr << "The letter \'"sv << *it << "\' is not found in letters!"sv << std::endl;
            return -1;
        }
        letter_index = std::distance(letters.begin(), pos);
        number += (letter_index + 1) * std::pow(LETTERS, n);
        n++;
    }
    number -= 1;
    return number;
}


Position Position::FromString(std::string_view str) {
    if (str.empty()) {
        return Position::NONE;
    }
    // Делим на буквенную и числовую части, там же проверяется валидность букв и цифр
    UserPosition user_position = DivideStringToColumnAndRow(str);
    if (user_position == UserPosition::NONE) {
        return Position::NONE;
    }

    // Если дошли до сюда, то исходная строка адекватная, 
    // Осталось проверить, что не заходит за границы
    // предельная позиция ячейки — (16383, 16383) с индексом** “XFD16384”
    if (user_position.column.size() == MAX_POS_LETTER_COUNT) {
        bool is_out_of_frame = lexicographical_compare(max_position_string.begin(), max_position_string.end(), str.begin(), str.end());
        if (is_out_of_frame){
            return Position::NONE;
        }
    }
    
    // Здесь уже точно проверенные пользовательские координаты 
    // Инициализируем возвращаемое значение
    Position res;
    res.row = user_position.row - 1;

    int col_number = ConvertColNameToDecIndex(user_position.column);
    
    if (col_number == -1) {
        return Position::NONE;
    }
    res.col = col_number;
    return res;

}

bool Size::operator==(Size rhs) const {
    return cols == rhs.cols && rows == rhs.rows;
}