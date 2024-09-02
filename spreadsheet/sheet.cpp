#include "sheet.h"

#include "cell.h"
#include "common.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <optional>
#include <queue>

using namespace std::literals;

namespace detail {

    struct ValuePrinter {
        std::ostream& out;

        void operator()(std::string cell_text) const {
            out << cell_text;
        }
        void operator()(double cell_number) const {
            out << cell_number;
        }
        void operator()(const FormulaError& err) const {
            out << err;
        }
    };


}  // namespace detail


bool Sheet::IsPositionInsidePrintableZone(Position pos) const {
    if (!pos.IsValid()) {
        throw InvalidPositionException("Err in IsPositionInsidePrintableZone: Position is out of acceptable table range\n"s);
    }
    if ((pos.row + 1) > printable_size_.rows || (pos.col + 1) > printable_size_.cols) {
        return false;
    }
    return true;
}

// to do:
Sheet::~Sheet() = default;  // так как используются умные указатели, то дефолтный деструктор должен подойти


void Sheet::SetCell(Position pos, std::string text) {

    if (!pos.IsValid()) {
        throw InvalidPositionException("Err in SetCell: Position is out of acceptable table range\n"s);
    }

    // Обновляем размер при необходимости
    if (!IsPositionInsidePrintableZone(pos)) {
        // обновляем кол-во строк
        printable_size_.rows = std::max(pos.row + 1, printable_size_.rows);
        sheet_.resize(printable_size_.rows);

        printable_size_.cols = std::max(pos.col + 1, printable_size_.cols);
        for (int i = 0; i < printable_size_.rows; i ++) {
            sheet_[i].resize(printable_size_.cols);
        }
    }


    // получаем указатель на ячейку, с ним будем работать 
    Cell* cell = GetConcreteCell(pos);

    // Если данных нет, то просто записываем ячейку:
    if (cell == nullptr) {
        // создаем новую ячейку
        auto cell = std::make_unique<Cell>(*this);
        cell->Set(text);

        std::vector contained_cells = cell->GetReferencedCells();

        if (std::find(contained_cells.begin(), contained_cells.end(), pos) != contained_cells.end()) {
            throw CircularDependencyException("Found circular dependency");
        }
        // помещаем указатель на созданную ячейку в таблицу
        sheet_[pos.row][pos.col] = std::move(cell); 
    }
    else { 
        // Если текст ячейки не изменился - ничего делать не надо
        if (cell->GetText() == text) {
            return;
        }

        // временно сохраняем ячейки, которые были связаны с изменяемой
        std::vector<Position> old_referenced_cells = cell->GetReferencedCells();

        cell->Set(text);  // возможны исключения CircularDependency или FormulaException
        /* внутри Set обновились все связи: 
        и для старых и для новых ссылок из|на cell */
        
        // Удаляем пустые ячейки, у которых не осталось связей после изменения cell
        DeleteEmptyUnconnectedCells(old_referenced_cells);

    }

    // обновляем кол-во элементов по строкам и столбцам
    rows_volume[pos.row] += 1;
    cols_volume[pos.col] += 1;

    return;
}

const CellInterface* Sheet::GetCell(Position pos) const {
    // проверяем координаты ячейки
    if (!pos.IsValid()) {
        throw InvalidPositionException("Err in const GetCell: Position is out of acceptable table range ["s+ std::to_string(pos.row) + ", "s + std::to_string(pos.col) + "]"s);
    }

    if (!IsPositionInsidePrintableZone(pos)) {
        return nullptr;
    }

    const CellInterface* cell = sheet_.at(pos.row).at(pos.col).get();
    return cell;
}

CellInterface* Sheet::GetCell(Position pos) {
    // проверяем координаты ячейки
    if (!pos.IsValid()) {
        throw InvalidPositionException("Err in GetCell: Position is out of acceptable table range ["s + std::to_string(pos.row) + ", "s + std::to_string(pos.col) + "]"s);
    }

    if (!IsPositionInsidePrintableZone(pos)) {
        return nullptr;
    }

    CellInterface* cell = sheet_.at(pos.row).at(pos.col).get();
    
    return cell;
}



void Sheet::UpdatePrintableAreaAfterClearPosition(Position pos) {
    // Обновляем данные количества ячеек по строкам и столбцам
    rows_volume[pos.row] -= 1;
    cols_volume[pos.col] -= 1;

    // Обновляем размер при необходимости
    int n_rows = 0;
    int n_cols = 0;
    
    // СПОСОБ 1 - используя map-ы 
    // 1. Ячейка находилась в последней строке
    if (pos.row == printable_size_.rows - 1) {
        // случай 1 - есть еще другие элементы в строке 
        if (rows_volume[pos.row] > 0) {
            n_rows = pos.row + 1;
        }
        else { 
        // случай 2 - удаленный элемент был последним в строке 
        // => ищем другую строку с элементами, двигаясь "снизу вверх" 
            for(int i = pos.row - 1; i >= 0; i--) {
                if (rows_volume[i] > 0) {
                    n_rows = i + 1;
                    break;
                }
            }
        }

        printable_size_.rows = n_rows;
    }

    // 2. Ячейка находилась в последнем столбце
    if (pos.col == printable_size_.cols - 1) {
        // случай 1 - есть еще другие элементы в строке 
        if (cols_volume[pos.col] > 0) {
            n_cols = pos.col + 1;
        }
        else { 
        // случай 2 - удаленный элемент был последним в строке 
        // => ищем другую строку с элементами, двигаясь "справа налево" 
            for(int j = pos.col - 1; j >= 0; j--) {
                if (cols_volume[j] > 0) {
                    n_cols = j + 1;
                    break;
                }
            }
        }
        printable_size_.cols = n_cols;
    }
}


// Удаляет ячейку совсем. Позиция должна быть заранее проверена
void Sheet::DeleteCell(Position pos) {
    sheet_[pos.row][pos.col].reset(nullptr);

    // Обновляем размер при необходимости
    UpdatePrintableAreaAfterClearPosition(pos);

}


/* Очищает ячейку. 
Если есть зависимые ячейки, то у них инвалидируется кеш, а текущая становится пустой.
Если связей нет, то ячейкаа совсем удаляется.
*/ 
void Sheet::ClearCell(Position pos) {
    // проверяем координаты ячейки
    if (!pos.IsValid()) {
        throw InvalidPositionException("Err in ClearCell: Position is out of acceptable table range: ["s + std::to_string(pos.row) + ", "s + std::to_string(pos.col) + "]"s);
    }  

    if (printable_size_ == Size{0,0}) {
        return;
    }

    Cell* cell_to_clear = GetConcreteCell(pos);
    
    // с несуществующими ячейками ничего не делаем
    if (cell_to_clear == nullptr) {
        return;
    }

    // получаем список ячеек, которые ссылались на удаляемую
    std::unordered_set<Cell*> cells_dependent_on_cleared = cell_to_clear->GetCellsReferencingToThis();
    
    // Случай 1 - на ячейку никто не ссылался 
    if (cells_dependent_on_cleared.empty()) {
        // совсем удаляем ячейку и обновляем печатаемую область
        DeleteCell(pos);
    } else {
        // опустошаем ячейку и обновляем печатаемую область
        cell_to_clear->ClearContent();
        UpdatePrintableAreaAfterClearPosition(pos);
    }

}

Size Sheet::GetPrintableSize() const {
    return printable_size_;
}

void Sheet::PrintValues(std::ostream& output) const {
    bool is_first_in_row = true;
    for (int i = 0; i < printable_size_.rows; i++) {
        is_first_in_row = true;
        for (int j = 0; j < printable_size_.cols; j++) {
            if (!is_first_in_row) {
                output << "\t";
            }
            is_first_in_row = false;
            Position pos_tmp{i,j};
            if (const CellInterface* cell = GetCell(pos_tmp)) {
                std::visit(detail::ValuePrinter{output}, cell->GetValue());    
            }
        }
        output << "\n"s;
    }
}

void Sheet::PrintTexts(std::ostream& output) const {
    bool is_first_in_row = true;
    for (int i = 0; i < printable_size_.rows; i++) {
        is_first_in_row = true;
        for (int j = 0; j < printable_size_.cols; j++) {
            if (!is_first_in_row) {
                output << "\t";
            }
            is_first_in_row = false;
            Position pos_tmp{i,j};
            if (const CellInterface* cell = GetCell(pos_tmp)) {
                output << cell->GetText();
            } 
        }
        output << "\n"s;
    }
}


// Эти методы нужны, чтобы иметь доступ к специфическим методам класса Cell,
// которые не доступны через CellInterface
const Cell* Sheet::GetConcreteCell(Position pos) const {
    // проверяем координаты ячейки
    if (!pos.IsValid()) {
        throw InvalidPositionException("Err in GetConcreteCell: Position is out of acceptable table range ["s + std::to_string(pos.row) + ", "s + std::to_string(pos.col) + "]"s);
    }

    if (!IsPositionInsidePrintableZone(pos)) {
        return nullptr;
    }

    const Cell* cell = sheet_.at(pos.row).at(pos.col).get();
    
    return cell;
}


Cell* Sheet::GetConcreteCell(Position pos) {
    // проверяем координаты ячейки
    if (!pos.IsValid()) {
        throw InvalidPositionException("Err in GetConcreteCell: Position is out of acceptable table range ["s + std::to_string(pos.row) + ", "s + std::to_string(pos.col) + "]"s);
    }

    if (!IsPositionInsidePrintableZone(pos)) {
        return nullptr;
    }

    Cell* cell = sheet_.at(pos.row).at(pos.col).get();
    
    return cell;
}


void Sheet::DeleteEmptyUnconnectedCells(const std::vector<Position>& cells_to_check) {
    if (cells_to_check.empty()) {
        return;
    }
    
    for (const Position& pos : cells_to_check) {
        Cell* cell_tmp = GetConcreteCell(pos);
        // удаляем пустые ячейки без связей:
        if (cell_tmp->IsEmptyCell() && !cell_tmp->HasAnyCellsReferencedToThis()) {
            DeleteCell(pos);
        }
    }
    return;
}

// создает пустую ячейку в месте pos и возвращает указатель на неё
Cell* Sheet::AddNewEmptyCell(Position pos) {
    SetCell(pos, std::string());
    return GetConcreteCell(pos);
}

std::unique_ptr<SheetInterface> CreateSheet() {
    return std::make_unique<Sheet>();
}


