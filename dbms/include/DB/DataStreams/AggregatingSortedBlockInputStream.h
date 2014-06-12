#pragma once

#include <Yandex/logger_useful.h>

#include <DB/Core/Row.h>
#include <DB/Core/ColumnNumbers.h>
#include <DB/DataStreams/MergingSortedBlockInputStream.h>
#include <DB/AggregateFunctions/IAggregateFunction.h>
#include <DB/Columns/ColumnAggregateFunction.h>


namespace DB
{

/** Соединяет несколько сортированных потоков в один.
  * При этом, для каждой группы идущих подряд одинаковых значений первичного ключа (столбцов, по которым сортируются данные),
  * сливает их в одну строку. При слиянии, производится доагрегация данных - слияние состояний агрегатных функций,
  * соответствующих одному значению первичного ключа. Для столбцов, не входящих в первичный ключ, и не имеющих тип AggregateFunction,
  * при слиянии, выбирается первое попавшееся значение.
  */
class AggregatingSortedBlockInputStream : public MergingSortedBlockInputStream
{
public:
	AggregatingSortedBlockInputStream(BlockInputStreams inputs_, const SortDescription & description_, size_t max_block_size_)
		: MergingSortedBlockInputStream(inputs_, description_, max_block_size_),
		log(&Logger::get("SummingSortedBlockInputStream"))
	{
	}

	String getName() const { return "AggregatingSortedBlockInputStream"; }

	String getID() const
	{
		std::stringstream res;
		res << "AggregatingSorted(inputs";

		for (size_t i = 0; i < children.size(); ++i)
			res << ", " << children[i]->getID();

		res << ", description";

		for (size_t i = 0; i < description.size(); ++i)
			res << ", " << description[i].getID();

		res << ")";
		return res.str();
	}

protected:
	/// Может возвращаться на 1 больше записей, чем max_block_size.
	Block readImpl();

private:
	Logger * log;

	/// Столбцы с какими номерами надо аггрегировать.
	ColumnNumbers column_numbers_to_aggregate;
	std::vector<ColumnAggregateFunction *> columns_to_aggregate;

	Row current_key;		/// Текущий первичный ключ.
	Row next_key;			/// Первичный ключ следующей строки.

	Row current_row;

	/** Делаем поддержку двух разных курсоров - с Collation и без.
	 *  Шаблоны используем вместо полиморфных SortCursor'ов и вызовов виртуальных функций.
	 */
	template<class TSortCursor>
	void merge(Block & merged_block, ColumnPlainPtrs & merged_columns, std::priority_queue<TSortCursor> & queue);

	/// Вставить в результат первую строку для текущей группы.
	void insertCurrentRow(ColumnPlainPtrs & merged_columns);

	/** Извлечь все состояния аггрегатных функций и объединить с текущей группой.
	  */
	template<class TSortCursor>
	void addRow(TSortCursor & cursor)
	{
		for (size_t i = 0, size = column_numbers_to_aggregate.size(); i < size; ++i)
		{
			size_t j = column_numbers_to_aggregate[i];
			columns_to_aggregate[i]->insertMergeFrom(*cursor->all_columns[j], cursor->pos);
		}
	}
};

}