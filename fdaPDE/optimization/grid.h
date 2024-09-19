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

#ifndef __GRID_H__
#define __GRID_H__

#include "../fields.h"
#include "../utils/symbols.h"
#include "callbacks/callbacks.h"

namespace fdapde {

// searches for the point in a given grid minimizing a given nonlinear objective
template <int N, typename... Args> class Grid {
   private:
    using VectorType = typename std::conditional_t<N == Dynamic, DVector<double>, SVector<N>>;
    using GridType = Eigen::Matrix<double, Eigen::Dynamic, N>;   // equivalent to DMatrix<double> for N == Dynamic
    std::tuple<Args...> callbacks_ {};
    VectorType optimum_;
    double value_;   // objective value at optimum
   public:
    VectorType x_current;
    // constructor
    Grid() requires(sizeof...(Args) != 0) { }
    Grid(Args&&... callbacks) : callbacks_(std::make_tuple(std::forward<Args>(callbacks)...)) { }
    // copy semantic
    Grid(const Grid& other) : callbacks_(other.callbacks_) { }
    Grid& operator=(const Grid& other) {
        callbacks_ = other.callbacks_;
        return *this;
    }
    template <typename F> VectorType optimize(F& objective, const GridType& grid) {
        fdapde_static_assert(
          std::is_same<decltype(std::declval<F>().operator()(VectorType())) FDAPDE_COMMA double>::value,
          INVALID_CALL_TO_OPTIMIZE_OBJECTIVE_FUNCTOR_NOT_ACCEPTING_VECTORTYPE);
        bool stop = false;   // asserted true in case of forced stop
        // algorithm initialization
        x_current = grid.row(0);
        value_ = objective(x_current);
        optimum_ = x_current;
        // optimize field over supplied grid
        for (int i = 1; i < grid.rows() && !stop; ++i) {
            x_current = grid.row(i);
            double x = objective(x_current);
            stop |= execute_post_update_step(*this, objective, callbacks_);
            // update minimum if better optimum found
            if (x < value_) {
                value_ = x;
                optimum_ = x_current;
            }
        }
        return optimum_;
    }
    // getters
    VectorType optimum() const { return optimum_; }
    double value() const { return value_; }
};

}   // namespace fdapde

#endif   // __GRID_H__
