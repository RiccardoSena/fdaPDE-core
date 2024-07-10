// This file is part of fdaPDE, a C++ library for physics-informed
// spatial and functional data analysis.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef __BLOCK_FRAME_H__
#define __BLOCK_FRAME_H__

#include <algorithm>
#include <exception>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "../symbols.h"
#include "../traits.h"

namespace fdapde {
namespace core {
  
// forward declarations
enum ViewType { Row, Range, Sparse };
template <ViewType S, typename... Ts> class BlockView;

// a data structure for handling numerical dataframes. Need to supply beforeahead all intended types to process
template <typename... Ts> class BlockFrame {
    fdapde_static_assert(unique_types<Ts...>::value, BLOCK_FRAME_WITH_SAME_TYPES);
   private:
    // BlockFrame main storage structure
    std::tuple<std::unordered_map<std::string, DMatrix<Ts>>...> data_ {};
    // metadata
    using types_ = std::tuple<Ts...>;       // list of types
    template <typename T> using check_presence_of_type = has_type<T, types_>;
    std::vector<std::string> columns_ {};   // column names
    int rows_ = 0;
    std::vector<bool> dirty_bits_ {};   // asserted true if the i-th column contains modified data
   public:
    // constructor
    BlockFrame() = default;
    BlockFrame(int rows) : rows_(rows) {};   // create empty BlockFrame with specified dimensions

    // getter to raw data
    const std::tuple<std::unordered_map<std::string, DMatrix<Ts>>...>& data() const { return data_; }
    int rows() const { return rows_; }
    int cols() const { return columns_.size(); }
    // return the names of columns corresponding to modified data
    std::vector<std::string> dirty_cols() const {
        std::vector<std::string> result;
        result.reserve(columns_.size());
        for (std::size_t i = 0; i < dirty_bits_.size(); ++i) {
            if (dirty_bits_[i]) { result.emplace_back(columns_[i]); }
        }
        return result;
    }  
    // tests if BlockFrame contains block named "key"
    bool has_block(const std::string& key) const {
        return std::find(columns_.cbegin(), columns_.cend(), key) != columns_.cend();
    }
    // clear specified dirty bit
    void clear_dirty_bit(const std::string& key) {
        if (!has_block(key)) return; // does nothing if queried for a non-inserted block
        dirty_bits_[std::distance(columns_.begin(), std::find(columns_.begin(), columns_.end(), key))] = false;
    }
    // true if block named key as dirty bit set
    bool is_dirty(const std::string& key) const {
      if (!has_block(key)) return false; // a block which is not present is assumed clean
        return dirty_bits_[std::distance(columns_.begin(), std::find(columns_.begin(), columns_.end(), key))];
    }

    // insert new block, if the key is already present insert will overwrite the existing data
    template <typename T> void insert(const std::string& key, const DMatrix<T>& data, bool dirty_bit = true) {
        fdapde_static_assert(check_presence_of_type<T>::value, TYPE_NOT_IN_BLOCK_FRAME);
	fdapde_assert(rows_ == 0 || data.rows() == rows_);
        // store data
        std::get<index_of<T, types_>::index>(data_)[key] = data;
        // update metadata
        if (!has_block(key)) {
            columns_.push_back(key);
            dirty_bits_.push_back(dirty_bit);
        } else {
            auto key_position = std::find(columns_.begin(), columns_.end(), key);
            dirty_bits_[std::distance(columns_.begin(), key_position)] = dirty_bit;
        }
        if (rows_ == 0) rows_ = data.rows();
        return;
    }
    // insert new block in stacked mode: given an n x m block this function inserts a vector block of (n x m) rows
    template <typename T> void stack(const std::string& key, const DMatrix<T>& data) {
        fdapde_static_assert(check_presence_of_type<T>::value, TYPE_NOT_IN_BLOCK_FRAME);
        // compute dimensions
        int n = data.rows();
        int m = data.cols();
        // prepare new block to insert
        DMatrix<T> stacked_data;
        stacked_data.resize(n * m, 1);
        for (int i = 0; i < m; ++i) stacked_data.block(i * n, 0, n, 1) = data.col(i);
        // store data
        insert(key, stacked_data);
        return;
    }

    // access operations
    // return const reference to block given its key
    template <typename T> const DMatrix<T>& get(const std::string& key) const {
        fdapde_static_assert(check_presence_of_type<T>::value, TYPE_NOT_IN_BLOCK_FRAME);
        // throw exeption if key not in BlockFrame
        if (!has_block(key)) throw std::runtime_error("block " + key + " not found in data.");
        return std::get<index_of<T, types_>::index>(data_).at(key);
    }
    // non-const access (potential modifications are not recorded in dirty_bits_)
    template <typename T> DMatrix<T>& get(const std::string& key) {
        fdapde_static_assert(check_presence_of_type<T>::value, TYPE_NOT_IN_BLOCK_FRAME);
        // throw exeption if key not in BlockFrame
        if (!has_block(key)) throw std::runtime_error("block " + key + " not found in data.");
        return std::get<index_of<T, types_>::index>(data_).at(key);
    }
    // extract all unqique rows from a block
    template <typename T> DMatrix<T> extract_unique(const std::string& key) const {
        fdapde_static_assert(check_presence_of_type<T>::value, TYPE_NOT_IN_BLOCK_FRAME);
        // throw exeption if key not in BlockFrame
        if (!has_block(key)) throw std::runtime_error("block " + key + " not found in data.");
        DMatrix<T> result;
        // get unique values from block with given key
        const DMatrix<T>& block = std::get<index_of<T, types_>::index>(data_).at(key);
        std::unordered_map<DMatrix<T>, int, fdapde::matrix_hash> row_map;   // preserve row ordering
        std::unordered_set<DMatrix<T>, fdapde::matrix_hash> uniques;        // stores unique rows
        int i = 0;                                                          // last inserted row index
        for (int r = 0; r < block.rows(); ++r) {
            // cycle row by row, duplicates are filtered by std::unordered_set
            auto result = uniques.insert(block.row(r));
            if (result.second) row_map[block.row(r)] = i++;
        }
        // copy unique rows in result
        result.resize(uniques.size(), block.cols());
        for (const DMatrix<T>& r : uniques) result.row(row_map.at(r)) = r;
        return result;
    }

    // return a single row of the dataframe
    BlockView<Row, Ts...> operator()(int idx) const { return BlockView<Row, Ts...>(*this, idx); }
    // subset the BlockFrame by rows in between begin and end
    BlockView<Range, Ts...> operator()(int begin, int end) const {
        return BlockView<Range, Ts...>(*this, begin, end);
    }
    // subset the BlockFrame by a vector of indices
    BlockView<Sparse, Ts...> operator()(const std::vector<int>& idxs) const {
        return BlockView<Sparse, Ts...>(*this, idxs);
    }
    // return column block
    template <typename T> auto col(const std::string& key, int idx) const {
        fdapde_static_assert(check_presence_of_type<T>::value, TYPE_NOT_IN_BLOCK_FRAME);
        // this will throw an exeption if key cannot be found
        const DMatrix<T>& block = get<T>(key);
        // throw exeption if block has more than one column
	fdapde_assert(block.cols() < idx);
        return block.col(idx);
    }
    // return view to last n rows of the blockframe
    BlockView<Range, Ts...> tail(int begin) const { return BlockView<Range, Ts...>(*this, begin, rows_ - 1); }
    // return view to first n rows of the blockframe
    BlockView<Range, Ts...> head(int end) const { return BlockView<Range, Ts...>(*this, 0, end); }

    // perform a shuffling of the BlockFrame rows returning a new BlockFrame
    BlockFrame<Ts...> shuffle(int seed) const {
        std::vector<int> random_idxs;
        random_idxs.resize(rows_);
        // fill vector with integers from 0 to model.obs() - 1
        for (int i = 0; i < rows_; ++i) random_idxs[i] = i;
        // shuffle vector of indexes
        std::default_random_engine rng(seed);
        std::shuffle(random_idxs.begin(), random_idxs.end(), rng);
        // extract the blockframe from the view obtained by using random_idxs as set of indexes
        return BlockView<Sparse, Ts...>(*this, random_idxs).extract();
    }
    BlockFrame<Ts...> shuffle() const { return shuffle(std::random_device()()); }   // uses random seed

    // remove block
    template <typename T> void remove(const std::string& key) {
        fdapde_static_assert(check_presence_of_type<T>::value, TYPE_NOT_IN_BLOCK_FRAME);
        std::get<index_of<T, types_>::index>(data_).erase(key);   // remove block
        auto key_position = std::find(columns_.begin(), columns_.end(), key);
        columns_.erase(key_position);
        dirty_bits_.erase(dirty_bits_.begin() + std::distance(columns_.begin(), key_position));
        return;
    }
};

// a view of a BlockFrame. No operation is made until an operation is requested
template <ViewType S, typename... Ts> class BlockView {
   private:
    const BlockFrame<Ts...>& frame_;
    using types_ = std::tuple<Ts...>;   // list of types
    template <typename T> using check_presence_of_type = has_type<T, types_>;
    std::vector<int> idx_;
    // extract all blocks of type T from this view and store it in BlockFrame frame.
    template <typename T> void extract_(BlockFrame<Ts...>& frame) const {
        // cycle on all (key, blocks) pair for this type T
        for (const auto& v : std::get<index_of<T, types_>::index>(frame_.data())) {
            DMatrix<T> block = get<T>(v.first);   // extract block from view
            frame.insert(v.first, block);         // move block to frame with given key
        }
        return;
    }
   public:
    // constructor for row access
    BlockView(const BlockFrame<Ts...>& frame, int row) : frame_(frame) { idx_.push_back(row); };
    // constructor for range access
    BlockView(const BlockFrame<Ts...>& frame, int begin, int end) : frame_(frame) {
        idx_.push_back(begin);
        idx_.push_back(end);
    };
    // constructor for sparse access
    BlockView(const BlockFrame<Ts...>& frame, const std::vector<int>& vec) : frame_(frame) { idx_ = vec; };
    // returns a copy of the values stored under block with ID key along row row_
    template <typename T> DMatrix<T> get(const std::string& key) const {
        fdapde_static_assert(check_presence_of_type<T>::value, TYPE_NOT_IN_BLOCK_FRAME);
        // throw exeption if key not in BlockFrame
        if (!frame_.has_block(key)) throw std::runtime_error("block " + key + " not found in data.");
        if constexpr (S == ViewType::Row)
            // return single row
            return std::get<index_of<T, types_>::index>(frame_.data()).at(key).row(idx_[0]);
        if constexpr (S == ViewType::Range)
            // return all rows in between begin and end
            return std::get<index_of<T, types_>::index>(frame_.data())
              .at(key)
              .middleRows(idx_[0], idx_[1] - idx_[0] + 1);
        else {
            // reserve space for result matrix
            DMatrix<T> result;
            const DMatrix<T>& data = std::get<index_of<T, types_>::index>(frame_.data()).at(key);
            int rows = idx_.size();
            int cols = data.cols();
            result.resize(rows, cols);
            // copy rows from BlockFrame to output matrix
            for (int i = 0; i < rows; i++) { result.row(i) = data.row(idx_[i]); }
            // return matrix
            return result;
        }
    }
    // returns an eigen block for read-write access to portions of this view (only range views)
    template <typename T> auto block(const std::string& key) const {
        fdapde_static_assert(check_presence_of_type<T>::value, TYPE_NOT_IN_BLOCK_FRAME);
	fdapde_static_assert(S == ViewType::Range, BLOCK_OPERATIONS_ALLOWED_ONLY_ON_RANGE_VIEWS);
        // throw exeption if key not in BlockFrame
        if (!frame_.has_block(key)) throw std::runtime_error("block " + key + " not found in data.");
        // return block of this view
        return std::get<index_of<T, types_>::index>(frame_.data()).at(key).middleRows(idx_[0], idx_[1] - idx_[0] + 1);
    }

    // returns a block given by the copy of all blocks identified by "keys"
    template <typename T, typename... Args>
    typename std::enable_if<sizeof...(Args) >= 2, DMatrix<T>>::type get(const Args&... keys) {
        fdapde_static_assert(check_presence_of_type<T>::value, TYPE_NOT_IN_BLOCK_FRAME);
        // compute size of resulting matrix
        DMatrix<T> result;
        int cols = 0;
        int rows;
        // number of rows is given by type of view
        if constexpr (S == ViewType::Row) rows = 1;
        if constexpr (S == ViewType::Range) rows = idx_[1] - idx_[0] + 1;
        if constexpr (S == ViewType::Sparse) rows = idx_.size();
        // number of columns is extracted by fold expresions
        ([&] { cols += frame_.template get<T>(keys).cols(); }(), ...);
        result.resize(rows, cols);
        // fill result matrix by copy (slow operation)
        int i = 0;
        (
          [&] {
              int col = frame_.template get<T>(keys).cols();
              result.middleCols(i, col) = get<T>(keys);
              i += col;
          }(),
          ...);
        return result;
    }
    // convert this BlockView into an independent BlockFrame (requires copy data into a new BlockFrame)
    BlockFrame<Ts...> extract() const {
        BlockFrame<Ts...> result;
        std::apply(
          [&]([[maybe_unused]] Ts... args) {   // cycle on types
              ((extract_<Ts>(result)), ...);
          },
          types_());
        return result;   // let NRVO
    }
    // return stored vector of indexes
    const std::vector<int> idx() const { return idx_; }
};

}   // namespace core
}   // namespace fdapde

#endif   // __BLOCK_FRAME_H__
