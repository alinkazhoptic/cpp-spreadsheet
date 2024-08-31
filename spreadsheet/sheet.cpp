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
        cell = new Cell(*this);
        cell->Set(text);

        std::vector contained_cells = cell->GetReferencedCells();

        if (std::find(contained_cells.begin(), contained_cells.end(), pos) != contained_cells.end()) {
            throw CircularDependencyException("Found circular dependency");
        }
        // помещаем указатель на созданную ячейку в таблицу
        sheet_[pos.row][pos.col].reset(cell); 
    }
    else { 
        // создаем новую ячейку, по которой проверим наличие циклических зависимостей
        Cell* new_cell = new Cell(*this); 
        new_cell->Set(text);

        // Для формульной ячейки надо проверить на циклические зависимости
        if (new_cell->IsFormulaInCell()) {
            std::unordered_set<Cell*> contained_refs = new_cell->GetCellsContainedInThis();
            std::unordered_set<Cell*> cells_depending_on_cell = cell->GetCellsReferencingToThis();

            if (contained_refs.count(cell)) {
                throw CircularDependencyException("The cell contained itself");
            }

            if (!contained_refs.empty() && !cells_depending_on_cell.empty()) {
                // проверяем на цикличность:
                // Для каждой ячейки из списка ссылок (contained_refs[i]) 
                // проверяем её наличие среди ячеек, которые ссылаются на исходную ячейку cell
                for (const Cell* cell_tmp : contained_refs) {
                    if (cell->CheckExistingDependenciesOnThisCell(cell_tmp)) {
                        throw CircularDependencyException("Found circular dependency");
                    } 
                }
            }     
        }
        /* сюда пришли если циклических зависимостей нет */
        ClearCashOfDependentCells(cell);
        // удаляем зависимость в каждой ячейке, на которую ранее ссылалась изменяемая ячейка
        DeleteConnections(cell, cell->GetReferencedCells());

        // цикличности нет => можно менять значение для cell
        cell->Set(text);  // (может выпасти исключение FormulaException)
        // сбрасываем кеш
        cell->ClearCash();
        /* внутри Set обновились связи у ячеек, на которые ссылается cell */
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
        // удаляем ячейку и обновляем данные по печатаемой области
        DeleteCell(pos);
    } else {
        // есть ссылки => надо обновить кеш, а ячейку сделать пустой
        ClearCashOfDependentCells(cell_to_clear);
        cell_to_clear->ClearContent();

        // TODO: надо ли обновлять размер, если ячейка очищена, но не удалена окончательно из-за ссылок?
        // Обновляем размер при необходимости
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


// Очистить кэш у ячеек, зависящих от ячейки на заданной позиции pos
// Необходимо вызвать после валидного изменения ячейки pos 
void Sheet::ClearCashOfDependentCells(const Cell* cell_updated) {
    std::unordered_set<Cell*> cells_dependent_on_updated_cell = cell_updated->GetCellsReferencingToThis(); 

    // Случай 1 - зависимый ячеек нет
    if (cells_dependent_on_updated_cell.empty()) {
        return;
    }

    // Случай 2 - зависимые есть - обход графа в ширину
    std::queue<Cell*> cells_to_visit;
    
    // Добавляем ячейки в очередь просмотра
    for (Cell* cell : cells_dependent_on_updated_cell) {
        cells_to_visit.push(cell);
    }
    
    while (!cells_to_visit.empty()) {
        // берем первую (очередную) ячейку из очереди
        Cell* cell_cur = cells_to_visit.front();

        // Очищаем кэш только у ячеек, где он есть
        if (cell_cur->HasCash()) {
            // очищаем кеш
            cell_cur->ClearCash();

            // Добавляем в очередь ячейки, которые ссылаются на cell_cur
            for (Cell* cell_tmp : cell_cur->GetCellsReferencingToThis()) {
                cells_to_visit.push(cell_tmp);
            }
        }

        // удаляем обработанную ячейку!!
        cells_to_visit.pop();
    }

    return;
}


// Обновляет граф при изменении заданной ячейки pos
void Sheet::AddConnections(Cell* cell_added, const std::vector<Position>& ref_list) {
    // Случай 1 - ссылок нет
    if (ref_list.empty()) {
        return;
    }

    // Случай 2 - ссылки есть 
    // => получаем/создаем ячейки на указанных позициях и добавляем им связи с cell_added
    for (const Position& pos : ref_list) {
        Cell* cell_tmp = GetConcreteCell(pos);

        // если ячейки нет, то создаем новую пустую ячейку
        if (!cell_tmp) {
            cell_tmp = AddNewEmptyCell(pos);
        }

        // для существующих ячеек добавляем связь
        cell_tmp->AddNewCellReferencedToThis(cell_added);
    }

    return;
} 


void Sheet::DeleteConnections(Cell* cell_changed, const std::vector<Position>& ref_list) {
     // Случай 1 - ссылок нет
    if (ref_list.empty()) {
        return;
    }

    // Случай 2 - ссылки есть 
    // => получаем ячейки на указанных позициях и удаляем их связи с cell_changed
    for (const Position& pos : ref_list) {
        Cell* cell_tmp = GetConcreteCell(pos);
        cell_tmp->DeleteReferenceToThis(cell_changed);

        // удаляем пустые ячейки без связей:
        if (cell_tmp->IsEmptyCell() && !cell_tmp->HasAnyCellsReferencedToThis()) {
            DeleteCell(pos);
        }
    }

}

// создает пустую ячейку в месте pos и возвращает указатель на неё
Cell* Sheet::AddNewEmptyCell(Position pos) {
    SetCell(pos, ""s);
    return GetConcreteCell(pos);
}

std::unique_ptr<SheetInterface> CreateSheet() {
    return std::make_unique<Sheet>();
}


