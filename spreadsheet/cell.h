#pragma once

#include "common.h"
#include "formula.h"

#include <optional>
#include <unordered_set>

class Sheet; // возможно, заглушка. Но если добавлять #include "sheet.h",  то будут перекрестные ссылки - не скомпилируется


class Cell : public CellInterface {
public:
    // Конструктор создает пустую ячейку 
    explicit Cell(Sheet& sheet);

    ~Cell();

    /*
    Когда пользователь задаёт текст в методе Cell::Set(), 
    внутри метода определяется тип ячейки в зависимости от заданного текста 
    и создаётся нужный объект-реализация: формульная, текстовая, пустая.
    */
    void Set(std::string text);

    // Совсем удаляет содержимое ячейки 
    void DeleteCell();

    // в impl будет пустая ячейка 
    void ClearContent();

    Value GetValue() const override;
    std::string GetText() const override;

    // Возвращает список ячеек, которые непосредственно задействованы в данной
    // формуле. Список отсортирован по возрастанию и не содержит повторяющихся
    // ячеек. В случае текстовой ячейки список пуст.
    std::vector<Position> GetReferencedCells() const override;

    bool IsFormulaInCell() const;


    // Возвращает список указателей на ячейки, которые содержатся в данной ячейке
    std::unordered_set<Cell*> GetCellsContainedInThis() const;
    
    // Возвращает список указателей на ячейки, которые ссылаются на данную ячейку
    std::unordered_set<Cell*> GetCellsReferencingToThis() const;
    
    // Записывает в перечень содержащихся ссылок новые данные  
    // Предыдущие данные очищаются
    void SetCellsContainedInThis(const std::vector<Position>& referencies_inside);
    void SetCellsContainedInThis(const std::unordered_set<Cell*>& referencies_inside);
    // Записывает в перечень ячеек, ссылающихся на текущую ячейку, новые данные
    // Предварительно имеющиеся данные очищаются
    void SetCellsReferencingToThis(const std::vector<Position>& referencies_to);
    void SetCellsReferencingToThis(const std::unordered_set<Cell*>& referencies_to);

    // Добавляет одну новую ячейку в список ячеек, на которые ссылается данная
    void AddNewCellContainedInThis(Cell* ref);
    // Добавляет одну новую ячейку в список ячеек, которые ссылаются на данную
    void AddNewCellReferencedToThis(Cell* link_from);

    // удаляет ячейку из списка ссылающихся на данную
    void DeleteReferenceToThis(Cell* link_from);

    // проверяет, есть ли зависимые ячейки
    bool HasAnyCellsReferencedToThis();

    bool IsEmptyCell() const;


    // Проверяет, есть ли среди всех зависимостей от данной ячейки, 
    // ячейка с координатами pos. Необходим для обнаружения циклических зависимостей.
    // Если в данную ячейку пытаемся записать ячейку с позицией Y, 
    // то надо вызвать CheckExistingDependenciesOnThisCell(Y) => true - есть зависимость => цикличность
    bool CheckExistingDependenciesOnThisCell(const Cell* cell_to_find) const;

    bool HasCash() const;

    void ClearCash();

private:
//можете воспользоваться нашей подсказкой, но это необязательно.
    class Impl;

    class EmptyImpl;
    class TextImpl;
    class FormulaImpl;

    std::unique_ptr<Impl> impl_;

    // std::unordered_set<Position> cells_contained_in_this_;  // ячейки, на которые ссылается данная ячейка
    // std::unordered_set<Position> cells_referencing_to_this_;  // ячейки, которые ссылаются на данную ячейку

    std::unordered_set<Cell*> cells_contained_in_this_;  // ячейки, на которые ссылается данная ячейка
    std::unordered_set<Cell*> cells_referencing_to_this_;  // ячейки, которые ссылаются на данную ячейку

    mutable std::optional<CellInterface::Value> cash_;  // храним результат расчета, чтобы не считать лишний раз 

    Sheet& sheet_;   // методы Cell могут менять содержимое таблицы
};