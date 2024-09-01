#include "cell.h"
#include "sheet.h"  // включили сюда класс Sheet, чтобы были доступны его методы. Иначе Cell ничего не знает про Sheet

#include <cassert>
#include <iostream>
#include <string>
#include <optional>
#include <queue>


class Cell::Impl {
public:
    Impl() = default;
    virtual ~Impl() = default;

    virtual void Set(std::string text) = 0;
    virtual void Clear() = 0;

    virtual Value GetValue(const SheetInterface& sheet) const = 0;
    virtual std::string GetText() const = 0;

    virtual bool IsFormulaInCell() const = 0;

    virtual bool IsEmptyCell() const = 0;

    virtual std::vector<Position> GetReferencedCells() const = 0;

};


class Cell::EmptyImpl final : public Cell::Impl {
public:
    EmptyImpl() = default;
    ~EmptyImpl() = default;
    
    // не делает ничего
    void Set(std::string text) override {
        return;
    }

    void Clear() override {
        return;
    } 

    Value GetValue(const SheetInterface& /* sheet is not used */) const override {
        return std::string();
    }

    std::string GetText() const override {
        return std::string();
    }

    virtual bool IsFormulaInCell() const override {
        return false;
    }

    std::vector<Position> GetReferencedCells() const override {
        return {};
    }

    bool IsEmptyCell() const override {
        return true;
    }

private:
    std::string text_;
};


class Cell::TextImpl final : public Cell::Impl {
public:
    TextImpl() = default;

    TextImpl(std::string text) 
    : text_(text) {}

    TextImpl(const TextImpl& other) 
    : text_(other.GetText()) {}

    TextImpl(TextImpl&& other) {
        text_ = std::move(other.GetText());
    }

    TextImpl& operator=(const TextImpl& other) {
        text_ = other.GetText();
        return *this;
    }

    TextImpl& operator=(TextImpl&& other) {
        text_ = std::move(other.GetText());
        return *this;
    }
    

    ~TextImpl() = default;

    // 
    void Set(std::string text) override {
        text_ = text;
    }

    void Clear() override {
        text_.clear();
    } 

    Value GetValue(const SheetInterface& /* sheet is not used */) const override {
        if (IsFormulaInText()) {
            return text_.substr(1, text_.size()-1);
        }
        return text_;
    }

    std::string GetText() const override {
        return text_;
    } 

    virtual bool IsFormulaInCell() const override {
        return false;
    } 

    std::vector<Position> GetReferencedCells() const override {
        return {};
    }

    bool IsEmptyCell() const override {
        return false;
    }

private:
    std::string text_;

    bool IsFormulaInText() const {
        // подразумевается, что ячейка с текстом не может быть пустой, 
        // для пустых есть EmptyImpl
        return (text_.at(0) == ESCAPE_SIGN);
    }
};


class Cell::FormulaImpl final : public Cell::Impl {
public:
    FormulaImpl() = default;

    FormulaImpl(std::string expression) {
        try {
            formula_interf_ = ParseFormula(expression);
        } catch(const FormulaException& err) {
            formula_interf_ = nullptr;
            throw;
        }
    }
    

    ~FormulaImpl() {
        Clear();
    }

    // не делает ничего
    void Set(std::string expr) override {
        try {
            formula_interf_ = ParseFormula(expr);
        } catch(const FormulaException& err) {
            formula_interf_ = nullptr;
            throw;
        }

    }

    void Clear() override {
        formula_interf_.reset();
    } 

    Value GetValue(const SheetInterface& sheet) const override {

        // Если как выше не получится, то надо переписывать код ниже с вытягиванием разных типов
        FormulaInterface::Value tmp = formula_interf_->Evaluate(sheet);
        if (std::holds_alternative<double>(tmp)) {
            return std::get<double>(tmp);
        }
        else if (std::holds_alternative<FormulaError>(tmp)) {
            return FormulaError(std::get<FormulaError>(tmp).GetCategory());
        }
        else { // в эту ветку не должны зайти, она нужна для обнаружения ошибок в коде 
            return "unknown empty string";
        }
        
    }

    std::string GetText() const override {
        return ("=" + formula_interf_->GetExpression());
    }

    virtual bool IsFormulaInCell() const override {
        return true;
    }

    std::vector<Position> GetReferencedCells() const override {
        return formula_interf_->GetReferencedCells();
    }


    bool IsEmptyCell() const override {
        return false;
    }
    

private:
    std::unique_ptr<FormulaInterface> formula_interf_;

};
    

// Конструктор создает пустую ячейку 
Cell::Cell(Sheet& sheet) 
: impl_(std::make_unique<EmptyImpl>())
, sheet_(sheet) {}


Cell::~Cell() {
    DeleteCell();
}


void Cell::Set(std::string text) {
    std::unique_ptr<Impl> new_impl;
    // в зависимости от содержимого, определяем тип ячейки
    
    // Случай 1 - пустая строка => пустая ячейка
    if (text.empty()) {
        new_impl.reset(new EmptyImpl());
    }
    // Случай 2 - формула
    // символ '=' и наличие содержательной части после '=' как признак формулы 
    else if (text.size() > 1 && text.at(0) == FORMULA_SIGN) {
        new_impl = std::make_unique<FormulaImpl>();
        // записываем формулу без =
        new_impl->Set(text.substr(1, text.size() - 1));

        // проверяем на циклические зависимости:
        for (const Position& pos : new_impl->GetReferencedCells()) {
            const Cell* cell_to_find = sheet_.GetConcreteCell(pos);
            if (CheckExistingDependenciesOnThisCell(cell_to_find) || (cell_to_find == this)) {
                throw CircularDependencyException("Found circular dependency");
            }
        }
    }
    // Случай 3 - текст (в том числе текст с формулой если он начинается на ')
    else {
        new_impl = std::make_unique<TextImpl>();
        new_impl->Set(text);
    }

    // Так как содержимое изменилось - надо очистить кэш в зависимых ячейках
    ClearCacheOfDependentCells();
    // Удаляем связи с данной ячейкой в ячейках, на которые ранее она ссылалась
    DeleteConnections();
    // очищаем информацию о содержащихся ссылках
    cells_contained_in_this_.clear();
    // записываем новые данные в ячейку
    impl_ = std::move(new_impl);

    // обновляем граф: добавляем связи с данной ячейкой (при необходимости создаются новые ячейки)
    AddConnections();

    // сбрасываем кэш
    ClearCache();
    
    // после обновления графа уверены, что все ячейки, 
    // на которые ссылается данная, существуют, можно их добавить в словарь 
    SetCellsContainedInThis(GetReferencedCells());
}

// Cовсем удаляет содержимое ячейки, 
void Cell::DeleteCell() {
    impl_.reset();
}

// Превращает ячейку в пустую
void Cell::ClearContent() {
    impl_.reset(new EmptyImpl());
}


CellInterface::Value Cell::GetValue() const {
    CellInterface::Value res;
    // для формул вычисляем кеш при необходимости
    if (IsFormulaInCell()) {
        if (!HasCache()) {
            cache_ = impl_->GetValue(sheet_);  // исключения тоже записываются в кеш
        } 
        res = cache_.value();
    } else { // для текста берем просто GetValue()
        res = impl_->GetValue(sheet_);
    }
    
    return res;
}


std::string Cell::GetText() const {
    return impl_->GetText();
}


bool Cell::IsFormulaInCell() const {
    return impl_->IsFormulaInCell();
}


// Возвращает список указателей на ячейки, которые содержатся в данной ячейке
std::unordered_set<Cell*> Cell::GetCellsContainedInThis() const {
    return cells_contained_in_this_;
}


// Возвращает список указателей на ячейки, которые ссылаются на данную ячейку
std::unordered_set<Cell*> Cell::GetCellsReferencingToThis() const {
    return cells_referencing_to_this_;
}


void Cell::SetCellsContainedInThis(const std::vector<Position>& referencies_inside) {
    cells_contained_in_this_.clear();
    for (const Position pos : referencies_inside) {
        cells_contained_in_this_.insert(sheet_.GetConcreteCell(pos));
    }
}

void Cell::SetCellsContainedInThis(const std::unordered_set<Cell*>& referencies_inside) {
    cells_contained_in_this_.clear();
    cells_contained_in_this_ = referencies_inside;
}


void Cell::SetCellsReferencingToThis(const std::vector<Position>& referencies_to) {
    cells_referencing_to_this_.clear();
    for (const Position pos : referencies_to) {
        cells_referencing_to_this_.insert(sheet_.GetConcreteCell(pos));
    }
}

void Cell::SetCellsReferencingToThis(const std::unordered_set<Cell*>& referencies_to) {
    cells_referencing_to_this_.clear();
    cells_referencing_to_this_ = referencies_to;
}


// Добавляет одну новую ячейку в список ячеек, на которые ссылается данная
void Cell::AddNewCellContainedInThis(Cell* cell) {
    cells_contained_in_this_.insert(cell);
}


// Добавляет одну новую ячейку в список ячеек, которые ссылаются на данную
void Cell::AddNewCellReferencedToThis(Cell* link_from) {
    cells_referencing_to_this_.insert(link_from);
}


// удаляет ячейку из списка ссылающихся на данную
void Cell::DeleteReferenceToThis(Cell* link_from) {
    if (cells_referencing_to_this_.count(link_from)) {
        cells_referencing_to_this_.erase(link_from);
    }
    
}


bool Cell::HasAnyCellsReferencedToThis() {
    return !cells_referencing_to_this_.empty();
}


bool Cell::IsEmptyCell() const {
    return impl_->IsEmptyCell();
}




/* Проверяет, есть ли среди всех зависимостей от данной ячейки, 
ячейка с адресом cell_to_find. Необходим для обнаружения циклических зависимостей.
Если в ДАННУЮ ячейку пытаемся записать ячейку cell_to_find, 
то надо вызвать CheckExistingDependenciesOnThisCell(cell_to_find) => true - есть зависимость => цикличность
*/
bool Cell::CheckExistingDependenciesOnThisCell(const Cell* cell_to_find) const {

    bool is_found = false;
    // Случай 0 - связей нет
    if (cells_referencing_to_this_.empty()) {
        return false;     
    }

    // Случай 2 - связи есть - инициируем обход графа
    std::unordered_set<Cell*> visited_cells;
    std::queue<Cell*> cells_to_visit;
    
    // Добавляем ячейки в очередь просмотра
    for (Cell* cell : cells_referencing_to_this_) {
        cells_to_visit.push(cell);
    }
    
    while (!cells_to_visit.empty()) {
        // берем первую (очередную) ячейку из очереди
        Cell* cell_cur = cells_to_visit.front();
        // Проверяем только ранее не посещенные ячейки
        if (!visited_cells.count(cell_cur)) {
            // пополняем список посещенных ячеек
            visited_cells.insert(cell_cur);
            // проверяем
            if (cell_cur == cell_to_find) {
                is_found = true;
                return is_found; 
            }
            // Добавляем в очередь ячейки, которые ссылаются на cell_cur
            for (Cell* cell_tmp : cell_cur->GetCellsReferencingToThis()) {
                cells_to_visit.push(cell_tmp);
            }
        }
        // удаляем посещенную ячейку!!
        cells_to_visit.pop();
    }
    
    return is_found;
}


// Возвращает список ячеек, которые непосредственно задействованы в данной
// формуле. Список отсортирован по возрастанию и не содержит повторяющихся
// ячеек. В случае текстовой ячейки список пуст.
std::vector<Position> Cell::GetReferencedCells() const {
    return impl_->GetReferencedCells();
}


bool Cell::HasCache() const {
    return (cache_.has_value());
}

void Cell::ClearCache() {
    cache_.reset();
}

// Очистить кэш у ячеек, зависящих от ДАННОЙ ячейки 
// Необходимо вызвать после валидного изменения 
void Cell::ClearCacheOfDependentCells() {

    // Случай 1 - зависимый ячеек нет
    if (cells_referencing_to_this_.empty()) {
        return;
    }

    // Случай 2 - зависимые есть - обход графа в ширину
    std::queue<Cell*> cells_to_visit;
    
    // Добавляем ячейки в очередь просмотра
    for (Cell* cell : cells_referencing_to_this_) {
        cells_to_visit.push(cell);
    }
    
    while (!cells_to_visit.empty()) {
        // берем первую (очередную) ячейку из очереди
        Cell* cell_cur = cells_to_visit.front();

        // Очищаем кэш только у ячеек, где он есть
        if (cell_cur->HasCache()) {
            // очищаем кеш
            cell_cur->ClearCache();

            // Добавляем в очередь ячейки, которые ссылаются на cell_cur
            for (Cell* cell_dependent_on_cur : cell_cur->GetCellsReferencingToThis()) {
                cells_to_visit.push(cell_dependent_on_cur);
            }
        }

        // удаляем обработанную ячейку!!
        cells_to_visit.pop();
    }
    
    return;
}

void Cell::DeleteConnections() {
     // Случай 1 - ссылок нет
    if (cells_contained_in_this_.empty()) {
        return;
    }

    // Случай 2 - ссылки есть 
    // => получаем ячейки на указанных позициях и удаляем их связи с cell_changed
    for (Cell* cell_tmp : cells_contained_in_this_) {
        cell_tmp->DeleteReferenceToThis(this /* удаляем указатель на данную ячейку из данных cell_tmp */);
    }
}

// Обновляет граф при изменении заданной ячейки pos
void Cell::AddConnections() {
    std::vector<Position> ref_list = GetReferencedCells();

    // Случай 1 - ссылок нет => в таблице ничего изменять не надо
    if (ref_list.empty()) {
        return;
    }

    // Случай 2 - ссылки есть 
    // => получаем/создаем ячейки на указанных позициях и добавляем каждой связь с текущей
    for (const Position& pos : ref_list) {
        Cell* cell_tmp = sheet_.GetConcreteCell(pos);

        // если ячейки нет, то создаем новую пустую ячейку
        if (!cell_tmp) {
            cell_tmp = sheet_.AddNewEmptyCell(pos);
        }

        // для существующих ячеек добавляем связь с данной
        cell_tmp->AddNewCellReferencedToThis(this);
    }

    return;
} 