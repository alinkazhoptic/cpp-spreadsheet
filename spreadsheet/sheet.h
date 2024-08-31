#pragma once

#include "cell.h"
#include "common.h"

#include <functional>
#include <unordered_map>

class Sheet : public SheetInterface {
public:
    Sheet()
    {   
        try{
            sheet_.reserve(Position::MAX_ROWS);

        } catch (const std::exception & err) {
            std::cout << "ERROR IN Sheet" << std::endl;
            std::cout << err.what() << std::endl;
        }
    }

    ~Sheet();

    void SetCell(Position pos, std::string text) override;

    const CellInterface* GetCell(Position pos) const override;
    CellInterface* GetCell(Position pos) override;

    /*
    Ячейка может быть удалена или очищена. Если на неё ссылаются элементы - очищать (= EmptyCell)
    Если ссылок на нее нет, то удалить.
    */
    void ClearCell(Position pos) override;

    Size GetPrintableSize() const override;

    void PrintValues(std::ostream& output) const override;
    void PrintTexts(std::ostream& output) const override;

    // Эти методы нужны, чтобы иметь доступ к специфическим методам класса Cell,
    // которые не доступны через CellInterface
    const Cell* GetConcreteCell(Position pos) const;
    Cell* GetConcreteCell(Position pos);

    // Добавляет ячейкам из ref_list связи с ячейкой cell_added
    void AddConnections(Cell* cell_added, const std::vector<Position>& ref_list); 

    // Удаляет связи ячеек из ref_list с ячейкой cell_changed
    // метод надо вызывать до add_connections 
    void DeleteConnections(Cell* cell_changed, const std::vector<Position>& ref_list);

    // создает пустую ячейку в месте pos и возвращает указатель на неё
    Cell* AddNewEmptyCell(Position pos);

private:

    Size printable_size_;
    std::unordered_map<int, int> rows_volume;  // кол-во ячеек в строке
    std::unordered_map<int, int> cols_volume;  // кол-во ячеек в столбце

    using Table = std::vector<std::vector<std::unique_ptr<Cell>>>;
    Table sheet_; 

    bool IsPositionInsidePrintableZone(Position pos) const;

    // Очистить кэш у ячеек, зависящих от ячейки на заданной позиции pos
    // Необходимо вызвать после валидного изменения ячейки pos 
    void ClearCashOfDependentCells(const Cell* cell_updated);

    void DeleteCell(Position pos);

    // Определяет новый размер печатаемой области после удаления ячейки из pos
    // Также обновляет данные по кол-ву ячеек в строках и столбцах
    void UpdatePrintableAreaAfterClearPosition(Position pos);

};