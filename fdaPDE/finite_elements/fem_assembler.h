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

#ifndef __FEM_ASSEMBLER_H__
#define __FEM_ASSEMBLER_H__

#include <memory>

#include "../fields/field_ptrs.h"
#include "../fields/scalar_field.h"
#include "../fields/vector_field.h"
#include "../pde/assembler.h"
#include "../utils/compile_time.h"
#include "../utils/integration/integrator.h"
#include "../utils/symbols.h"
#include "basis/multivariate_polynomial.h"
#include "fem_symbols.h"

namespace fdapde {
namespace core {

// finite element method assembler
template <typename D, typename B, typename I> class Assembler<FEM, D, B, I> {
   private:
    static constexpr int n_basis = B::n_basis;
    const D& mesh_;          // triangulated problem domain
    const I& integrator_;    // quadrature rule
    B reference_basis_ {};   // functional basis over reference unit simplex
    int dof_;                // overall number of unknowns in FEM linear system
    const DMatrix<int>& dof_table_;
    DVector<double> f_;   // for non-linear operators, the estimate of the approximated solution
   public:
    Assembler(const D& mesh, const I& integrator, int n_dofs, const DMatrix<int>& dofs) :
        mesh_(mesh), integrator_(integrator), dof_(n_dofs), dof_table_(dofs) {};
    Assembler(const D& mesh, const I& integrator, int n_dofs, const DMatrix<int>& dofs, const DVector<double>& f) :
        mesh_(mesh), integrator_(integrator), dof_(n_dofs), dof_table_(dofs), f_(f) {};

    // discretization methods
    template <typename E> SpMatrix<double> discretize_operator(const E& op) {
        constexpr int M = D::local_dim;
        constexpr int N = D::embed_dim;
        std::vector<Eigen::Triplet<double>> triplet_list;   // store triplets (node_i, node_j, integral_value)
        SpMatrix<double> discretization_matrix;

        // properly preallocate memory to avoid reallocations
        triplet_list.reserve(n_basis * mesh_.n_cells());
        discretization_matrix.resize(dof_, dof_);

        // prepare space for bilinear form components
        using BasisType = typename B::ElementType;
        using NablaType = decltype(std::declval<BasisType>().derive());
        BasisType buff_psi_i, buff_psi_j;               // basis functions \psi_i, \psi_j
        NablaType buff_nabla_psi_i, buff_nabla_psi_j;   // gradient of basis functions \nabla \psi_i, \nabla \psi_j
        Matrix<M, N, M> buff_invJ;        // (J^{-1})^T, being J the inverse of the barycentric matrix of e
        DVector<double> f(n_basis);       // active solution coefficients on current element e
        // prepare buffer to be sent to bilinear form
        auto mem_buffer = std::make_tuple(
          ScalarPtr(&buff_psi_i), ScalarPtr(&buff_psi_j), VectorPtr(&buff_nabla_psi_i), VectorPtr(&buff_nabla_psi_j),
          MatrixPtr(&buff_invJ), &f);

        // develop bilinear form expression in an integrable field here once
        auto weak_form = op.integrate(mem_buffer);   // let the compiler deduce the type of the expression template!

        int current_id;
        // cycle over all mesh elements
        for (typename D::cell_iterator e = mesh_.cells_begin(); e != mesh_.cells_end(); ++e) {
            // update elements related informations
            buff_invJ = e->invJ().transpose();
            current_id = e->id();   // element ID

            if (!is_empty(f_))   // should be bypassed in case of linear operators via an if constexpr!!!
                for (int dof = 0; dof < n_basis; dof++) { f[dof] = f_[dof_table_(current_id, dof)]; }

            // consider all pair of nodes
            for (int i = 0; i < n_basis; ++i) {
                buff_psi_i = reference_basis_[i];
                buff_nabla_psi_i = buff_psi_i.derive();   // update buffers content
                for (int j = 0; j < n_basis; ++j) {
                    buff_psi_j = reference_basis_[j];
                    buff_nabla_psi_j = buff_psi_j.derive();   // update buffers content
                    if constexpr (is_symmetric<decltype(op)>::value) {
                        // compute only half of the discretization matrix if the operator is symmetric
                        if (dof_table_(current_id, i) >= dof_table_(current_id, j)) {
                            double value = integrator_.template integrate_weak_form<decltype(op)>(*e, weak_form);

                            // linearity of the integral is implicitly used during matrix construction, since duplicated
                            // triplets are summed up, see Eigen docs for more details
                            triplet_list.emplace_back(dof_table_(current_id, i), dof_table_(current_id, j), value);
                        }
                    } else {
                        // not any optimization to perform in the general case
                        double value = integrator_.template integrate_weak_form<decltype(op)>(*e, weak_form);
                        triplet_list.emplace_back(dof_table_(current_id, i), dof_table_(current_id, j), value);
                    }
                }
            }
        }
        // matrix assembled
        discretization_matrix.setFromTriplets(triplet_list.begin(), triplet_list.end());
        discretization_matrix.makeCompressed();

        // return just half of the discretization matrix if the form is symmetric (lower triangular part)
        if constexpr (is_symmetric<decltype(op)>::value) {
            return discretization_matrix.selfadjointView<Eigen::Lower>();
        } else {
            return discretization_matrix;
        }
    }
    template <typename F> DVector<double> discretize_forcing(const F& f) {
        // allocate space for result vector
        DVector<double> discretization_vector {};
        discretization_vector.resize(dof_, 1);   // there are as many basis functions as degrees of freedom on the mesh
        discretization_vector.fill(0);           // init result vector to zero

        // build forcing vector
        for (typename D::cell_iterator e = mesh_.cells_begin(); e != mesh_.cells_end(); ++e) {
            for (int i = 0; i < n_basis; ++i) {
                // integrate \int_e [f*\psi], exploit integral linearity
                discretization_vector[dof_table_(e->id(), i)] += integrator_.integrate(*e, f, reference_basis_[i]);
            }
        }
        return discretization_vector;
    }
};

}   // namespace core
}   // namespace fdapde

#endif   // __FEM_ASSEMBLER_H__
