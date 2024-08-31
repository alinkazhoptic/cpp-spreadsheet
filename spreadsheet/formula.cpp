#include "formula.h"

// #include "FormulaAST.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <sstream>

using namespace std::literals;

std::ostream& operator<<(std::ostream& output, FormulaError fe) {
    return output << fe.ToString();
}

std::ostream& operator<<(std::ostream& output, FormulaException fe) {
    return output << "#FORMAT!";
}


namespace {

class Formula : public FormulaInterface {
public:
// Реализуйте следующие методы:

    // Конструктор формулы
    // Может выкинуть исключение при вводе лексически некорректной формулы
    // Обрабатываем это исключение прямо в списке инициализации (try catch).
    explicit Formula(std::string expression) try
        : ast_(ParseFormulaAST(expression)) {}
    catch(...)
    {
        throw FormulaException("Can\'t construct formula"s);
    }

    Value Evaluate(const SheetInterface& sheet) const override {
        double res;
        try {
            res = ast_.Execute(sheet);  // тут могут выкинуться исключения FormulaError любой категории
            return res;
        } catch (const FormulaError& err) {
            return FormulaError(err.GetCategory());
        }
    }


    std::string GetExpression() const override {
        std::string res_str;
        std::ostringstream out(res_str);
        ast_.PrintFormula(out);
        res_str = out.str();
        return res_str;
    }


    // Возвращает упорядоченный список уникальных ячеек, на которые ссылается данная формула
    std::vector<Position> GetReferencedCells() const override {
        const std::forward_list<Position>& cells_list = ast_.GetCells();
        // переписываем позиции из односвязного списка в вектор
        std::vector<Position> cells_v(cells_list.begin(), cells_list.end());
        
        // Сортируем и оставляем только уникальные 
        // (сортировка обязательно перед выделением уникальных - см. принцип работы unique)
        sort(cells_v.begin(), cells_v.end(), [] (const Position& lhs, const Position rhs) {
                                        return (lhs < rhs); });
        auto last = std::unique(cells_v.begin(), cells_v.end());
        // Удаляем неуникальные
        cells_v.erase(last, cells_v.end());

        return cells_v;
    }

private:
    FormulaAST ast_;

};
}  // namespace

std::unique_ptr<FormulaInterface> ParseFormula(std::string expression) {
    return std::make_unique<Formula>(std::move(expression));
}