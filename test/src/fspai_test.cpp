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

#include "../../fdaPDE/linear_algebra/fspai.h"
#include <fdaPDE/utils.h>
#include <unsupported/Eigen/SparseExtra>
#include <fstream>
using fdapde::core::FSPAI;


TEST(FspaiTestSuite, FspaiTest) {

    // Define a sparse matrix for input in the FSPAI algorithm
    SpMatrix<double> E_; 
    Eigen::loadMarket(E_, "../matrix_to_be_inverted.mtx"); // Load the matrix to be inverted from file

    // Define a sparse matrix for the expected result
    SpMatrix<double> expected_precondE;
    Eigen::loadMarket(expected_precondE, "../expected_inverted_matrix.mtx"); // Load the expected inverted matrix from file

    // Compute the inverted matrix E using the FSPAI algorithm
    int alpha = 10;    // Number of updates to the sparsity pattern for each column of A (perform alpha steps of approximate inverse update along column k)
    int beta = 10;     // Number of indices to add to the sparsity pattern of Lk for each update step
    double epsilon = 0.005; // Tolerance threshold for sparsity pattern update (the improvement must be higher than the acceptable threshold)
      
    FSPAI fspai_E(E_,alpha,beta,epsilon); // Initialize FSPAI with the input matrix
    //fspai_E.compute(E_,alpha, beta, epsilon); // Compute the approximate inverse
    SpMatrix<double> precondE_ = fspai_E.getL(); // Get the result matrix from FSPAI

    Eigen::saveMarket(precondE_, "precondsoluzione.mtx"); 
    // Convert the sparse matrix result to a dense matrix
    DMatrix<double> denseMatrix = (precondE_ - expected_precondE).toDense();
    // Compute the infinity norm of the difference
    double normInf = denseMatrix.lpNorm<Eigen::Infinity>();


/*

    using namespace std::chrono;
    
    int n_it = 20; // Numero di iterazioni
    std::vector<std::string> matrixFiles = {
        "../build/matrix10nonvuota.mtx",
        "../build/matrix9nonvuota.mtx",
        "../build/matrix8nonvuota.mtx",
        "../build/matrix7nonvuota.mtx",
        "../build/matrix6nonvuota.mtx",
        "../build/matrix5nonvuota.mtx",
        "../build/matrix4nonvuota.mtx"
    };
    std::vector<std::string> precondFiles = {
        "precondsoluzione10nonvuota.mtx",
        "precondsoluzione9nonvuota.mtx",
        "precondsoluzione8nonvuota.mtx",
        "precondsoluzione7nonvuota.mtx",
        "precondsoluzione6nonvuota.mtx",
        "precondsoluzione5nonvuota.mtx",
        "precondsoluzione4nonvuota.mtx"

    };

    for (size_t j = 0; j < matrixFiles.size(); ++j) {
        // Definisci una matrice sparsa per l'input nell'algoritmo FSPAI
        SpMatrix<double> matrix; 
        Eigen::loadMarket(matrix, matrixFiles[j]); // Carica la matrice dal file

        //FSPAI fspai(matrix); // Inizializza FSPAI con la matrice di input
        //fspai.compute(matrix, alpha, beta, epsilon); // Calcola l'inverso approssimato
        //SpMatrix<double> precond = fspai.getL(); // Ottieni la matrice risultante da FSPAI

        //Eigen::saveMarket(precond, precondFiles[j]); // Salva la matrice precondizionata

        // Cronometra l'operazione
        std::chrono::microseconds total_duration(0);

        for (int i = 0; i < n_it; ++i) {
            auto start = high_resolution_clock::now();

            FSPAI fspai(matrix,alpha,beta,epsilon); // Inizializza FSPAI con la matrice di input

            auto end = high_resolution_clock::now();
            auto duration = duration_cast<std::chrono::microseconds>(end - start);
            total_duration += duration;
        }

        auto average_duration = total_duration / n_it;
        std::cout << "Mean time for matrix " << (10 - j) << " is: "
                  << average_duration.count() * 1e-3 << " milliseconds" << std::endl; // Converti in millisecondi
    }
    */

    // Check if the computed norm is within the acceptable tolerance
    EXPECT_TRUE(normInf < fdapde::core::DOUBLE_TOLERANCE); 
}
