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

#ifndef __TRIANGULATION_H__
#define __TRIANGULATION_H__

#include <array>
#include <unordered_map>
#include <vector>

#include "../linear_algebra/binary_matrix.h"
#include "../utils/constexpr.h"
#include "../utils/symbols.h"
#include "triangle.h"
#include "tetrahedron.h"
#include "tree_search.h"
#include "utils.h"

namespace fdapde {

// public iterator types
template <typename Triangulation> struct CellIterator : public Triangulation::cell_iterator {
    CellIterator(int index, const Triangulation* mesh) : Triangulation::cell_iterator(index, mesh) { }
    CellIterator(int index, const Triangulation* mesh, int marker) :
        Triangulation::cell_iterator(index, mesh, marker) { }
};
template <typename Triangulation> struct BoundaryIterator : public Triangulation::boundary_iterator {
    BoundaryIterator(int index, const Triangulation* mesh) : Triangulation::boundary_iterator(index, mesh) { }
    BoundaryIterator(int index, const Triangulation* mesh, int marker) :
        Triangulation::boundary_iterator(index, mesh, marker) { }
};
  
template <int M, int N> class Triangulation;
template <int M, int N, typename Derived> class TriangulationBase {
   public:
    // can speek only about cells and vertices here and neighbors
    static constexpr int local_dim = M;
    static constexpr int embed_dim = N;
    static constexpr int n_nodes_per_cell = local_dim + 1;
    static constexpr int n_neighbors_per_cell = local_dim + 1;
    static constexpr bool is_manifold = !(local_dim == embed_dim);
    using CellType = std::conditional_t<M == 2, Triangle<Derived>, Tetrahedron<Derived>>;
    using TriangulationType = Derived;
    class NodeType {   // triangulation node abstraction
        int id_;
        const Derived* mesh_;
       public:
        NodeType() = default;
        NodeType(int id, const Derived* mesh) : id_(id), mesh_(mesh) { }
        int id() const { return id_; }
        SVector<embed_dim> coords() const { return mesh_->node(id_); }
        std::vector<int> patch() const { return mesh_->node_patch(id_); }
    };

    TriangulationBase() = default;
    TriangulationBase(
      const DMatrix<double>& nodes, const DMatrix<int>& cells, const DMatrix<int>& boundary, int flags) :
        nodes_(nodes), cells_(cells), nodes_markers_(boundary), flags_(flags) {
        // store number of nodes and number of cells
        n_nodes_ = nodes_.rows();
        n_cells_ = cells_.rows();
        // compute mesh limits
        range_.row(0) = nodes_.colwise().minCoeff();
        range_.row(1) = nodes_.colwise().maxCoeff();
        // -1 in neighbors_'s column i implies no neighbor adjacent to the edge opposite to vertex i
        neighbors_ = DMatrix<int>::Constant(n_cells_, n_neighbors_per_cell, -1);
    }
    TriangulationBase(const DMatrix<double>& nodes, const DMatrix<int>& cells, const DMatrix<int>& boundary) :
        TriangulationBase(nodes, cells, boundary, /*flags = */ 0) { }
    // getters
    SVector<embed_dim> node(int id) const { return nodes_.row(id); }
    bool is_node_on_boundary(int id) const { return nodes_markers_[id]; }
    const DMatrix<double>& nodes() const { return nodes_; }
    const DMatrix<int, Eigen::RowMajor>& cells() const { return cells_; }
    const DMatrix<int, Eigen::RowMajor>& neighbors() const { return neighbors_; }
    const BinaryVector<Dynamic>& boundary_nodes() const { return nodes_markers_; }
    int n_cells() const { return n_cells_; }
    int n_nodes() const { return n_nodes_; }
    int n_boundary_nodes() const { return nodes_markers_.count(); }
    SMatrix<2, N> range() const { return range_; }

    // iterators over cells (possibly filtered by marker)
    class cell_iterator : public internals::filtering_iterator<cell_iterator, const CellType*> {
        using Base = internals::filtering_iterator<cell_iterator, const CellType*>;
        using Base::index_;
        friend Base;
        const Derived* mesh_;
        int marker_;
        cell_iterator& operator()(int i) {
            Base::val_ = &(mesh_->cell(i));
            return *this;
        }
       public:
        using TriangulationType = Derived;
        cell_iterator() = default;
        cell_iterator(int index, const Derived* mesh, const BinaryVector<fdapde::Dynamic>& filter, int marker) :
            Base(index, 0, mesh->n_cells_, filter), mesh_(mesh), marker_(marker) {
            for (; index_ < Base::end_ && !filter[index_]; ++index_);
            if (index_ != Base::end_) { operator()(index_); }
        }
        cell_iterator(int index, const Derived* mesh, int marker) :
            cell_iterator(
              index, mesh,
              marker == TriangulationAll ? BinaryVector<fdapde::Dynamic>::Ones(mesh->n_cells()) :   // apply no filter
                fdapde::make_binary_vector(mesh->cells_markers().begin(), mesh->cells_markers().end(), marker),
              marker) { }
        int marker() const { return marker_; }
    };
    CellIterator<Derived> cells_begin(int marker = TriangulationAll) const {
        fdapde_assert(marker == TriangulationAll || (marker >= 0 && cells_markers_.size() != 0));
        return CellIterator<Derived>(0, static_cast<const Derived*>(this), marker);
    }
    CellIterator<Derived> cells_end(int marker = TriangulationAll) const {
        fdapde_assert(marker == TriangulationAll || (marker >= 0 && cells_markers_.size() != 0));
        return CellIterator<Derived>(n_cells_, static_cast<const Derived*>(this), marker);
    }
    // set cells markers
    template <typename Lambda> void mark_cells(int marker, Lambda&& lambda)
        requires(requires(Lambda lambda, CellType c) {
        { lambda(c) } -> std::same_as<bool>; }) {
        fdapde_assert(marker >= 0);
	cells_markers_.resize(n_cells_, Unmarked);
        for (cell_iterator it = cells_begin(); it != cells_end(); ++it) {
            if (lambda(*it)) {
                // give priority to highly marked cells
                if (cells_markers_[it->id()] < marker) { cells_markers_[it->id()] = marker; }
            }
        }
	return;
    }
    template <typename Iterator> void mark_cells(Iterator first, Iterator last) {
        fdapde_static_assert(
          std::is_convertible_v<typename Iterator::value_type FDAPDE_COMMA int>, INVALID_ITERATOR_RANGE);
        int n_markers = std::distance(first, last);
        bool all_markers_positive = std::all_of(first, last, [](auto marker) { return marker >= 0; });
        fdapde_assert(n_markers == n_cells_ && all_markers_positive);
        cells_markers_.resize(n_cells_, Unmarked);
        for (int i = 0; i < n_cells_; ++i) {
            int marker = *(first + i);
            // give priority to highly marked cells
            if (cells_markers_[i] < marker) { cells_markers_[i] = marker; }
        }
        return;
    }
    void mark_cells(int marker) {   // marks all unmarked cells
        fdapde_assert(marker >= 0);
        cells_markers_.resize(n_cells_, Unmarked);
        for (auto it = cells_begin(); it != cells_end(); ++it) {
            if (cells_markers_[it->id()] < marker) cells_markers_[it->id()] = marker;
        }
        return;
    }
    const std::vector<int>& cells_markers() const { return cells_markers_; }
    // iterator over boundary nodes
    class boundary_node_iterator : public internals::filtering_iterator<boundary_node_iterator, NodeType> {
        using Base = internals::filtering_iterator<boundary_node_iterator, NodeType>;
        using Base::index_;
        friend Base;
        const Derived* mesh_;
        boundary_node_iterator& operator()(int i) {
            Base::val_ = NodeType(i, mesh_);
            return *this;
        }
       public:
        using TriangulationType = Derived;
        boundary_node_iterator(int index, const Derived* mesh) :
            Base(index, 0, mesh->n_nodes_, mesh_->nodes_markers_), mesh_(mesh) {
            for (; index_ < Base::end_ && !mesh_->nodes_markers_[index_]; ++index_);
            if (index_ != Base::end_) { operator()(index_); }
        }
    };
    boundary_node_iterator boundary_nodes_begin() const {
        return boundary_node_iterator(0, static_cast<const Derived*>(this));
    }
    boundary_node_iterator boundary_nodes_end() const {
        return boundary_node_iterator(n_nodes_, static_cast<const Derived*>(this));
    }
   protected:
    DMatrix<double> nodes_ {};                         // physical coordinates of mesh's vertices
    DMatrix<int, Eigen::RowMajor> cells_ {};           // nodes (as row indexes in nodes_ matrix) composing each cell
    DMatrix<int, Eigen::RowMajor> neighbors_ {};       // ids of cells adjacent to a given cell (-1 if no adjacent cell)
    BinaryVector<fdapde::Dynamic> nodes_markers_ {};   // j-th element is 1 \iff node j is on boundary
    SMatrix<2, embed_dim> range_ {};                   // mesh bounding box (column i maps to the i-th dimension)
    int n_nodes_ = 0, n_cells_ = 0;
    int flags_ = 0;
    std::vector<int> cells_markers_;   // marker associated to the i-th cells
};

// face-based storage
template <int N> class Triangulation<2, N> : public TriangulationBase<2, N, Triangulation<2, N>> {
    fdapde_static_assert(N == 2 || N == 3, THIS_CLASS_IS_FOR_2D_OR_3D_TRIANGULATIONS_ONLY);
   public:
    using Base = TriangulationBase<2, N, Triangulation<2, N>>;
    static constexpr int n_nodes_per_edge = 2;
    static constexpr int n_edges_per_cell = 3;
    static constexpr int n_faces_per_edge = 2;
    using EdgeType = typename Base::CellType::EdgeType;
    using LocationPolicy = TreeSearch<Triangulation<2, N>>;
    using Base::cells_;      // N \times 3 matrix of node identifiers for each triangle
    using Base::embed_dim;   // dimensionality of the ambient space
    using Base::local_dim;   // dimensionality of the tangent space
    using Base::n_cells_;    // N: number of triangles

    Triangulation() = default;
    Triangulation(
      const DMatrix<double>& nodes, const DMatrix<int>& faces, const DMatrix<int>& boundary, int flags = 0) :
        Base(nodes, faces, boundary, flags) {
        if (Base::flags_ & cache_cells) {   // populate cache if cell caching is active
            cell_cache_.reserve(n_cells_);
            for (int i = 0; i < n_cells_; ++i) { cell_cache_.emplace_back(i, this); }
        }
        using edge_t = std::array<int, n_nodes_per_edge>;
        using hash_t = fdapde::std_array_hash<int, n_nodes_per_edge>;
        struct edge_info {
            int edge_id, face_id;   // for each edge, its ID and the ID of one of the cells insisting on it
        };
        auto edge_pattern = cexpr::combinations<n_nodes_per_edge, Base::n_nodes_per_cell>();
        std::unordered_map<edge_t, edge_info, hash_t> edges_map;
	std::vector<bool> boundary_edges;
        edge_t edge;
        cell_to_edges_.resize(n_cells_, n_edges_per_cell);
        // search vertex of face f opposite to edge e (the j-th vertex of f which is not a node of e)
        auto node_opposite_to_edge = [this](int e, int f) -> int {
            int j = 0;
            for (; j < Base::n_nodes_per_cell; ++j) {
                bool found = false;
                for (int k = 0; k < n_nodes_per_edge; ++k) {
                    if (edges_[e * n_nodes_per_edge + k] == cells_(f, j)) { found = true; }
                }
                if (!found) break;
            }
            return j;
        };
        int edge_id = 0;
        for (int i = 0; i < n_cells_; ++i) {
            for (int j = 0; j < edge_pattern.rows(); ++j) {
                // construct edge
                for (int k = 0; k < n_nodes_per_edge; ++k) { edge[k] = cells_(i, edge_pattern(j, k)); }
                std::sort(edge.begin(), edge.end());   // normalize wrt node ordering
                auto it = edges_map.find(edge);
                if (it == edges_map.end()) {   // never processed edge
                    edges_.insert(edges_.end(), edge.begin(), edge.end());
                    edge_to_cells_.insert(edge_to_cells_.end(), {i, -1});
                    boundary_edges.push_back(true);
                    edges_map.emplace(edge, edge_info {edge_id, i});
                    cell_to_edges_(i, j) = edge_id;
                    edge_id++;
                } else {
                    const auto& [h, k] = it->second;
                    // elements k and i are neighgbors (they share an edge)
                    this->neighbors_(k, node_opposite_to_edge(h, k)) = i;
                    this->neighbors_(i, node_opposite_to_edge(h, i)) = k;
                    cell_to_edges_(i, j) = h;
                    boundary_edges[h] = false;   // edge_id-th edge cannot be on boundary
                    edge_to_cells_[2 * h + 1] = i;
                    edges_map.erase(it);
                }
            }
        }
        n_edges_ = edges_.size() / n_nodes_per_edge;
        boundary_edges_ = BinaryVector<fdapde::Dynamic>(boundary_edges.begin(), boundary_edges.end(), n_edges_);
        return;
    }
    // getters
    const typename Base::CellType& cell(int id) const {
        if (Base::flags_ & cache_cells) {   // cell caching enabled
            return cell_cache_[id];
        } else {
            cell_ = typename Base::CellType(id, this);
            return cell_;
        }
    }
    bool is_edge_on_boundary(int id) const { return boundary_edges_[id]; }
    Eigen::Map<const DMatrix<int, Eigen::RowMajor>> edges() const {
        return Eigen::Map<const DMatrix<int, Eigen::RowMajor>>(edges_.data(), n_edges_, n_nodes_per_edge);
    }
    Eigen::Map<const DMatrix<int, Eigen::RowMajor>> edge_to_cells() const {
        return Eigen::Map<const DMatrix<int, Eigen::RowMajor>>(edge_to_cells_.data(), n_edges_, 2);
    }
    const DMatrix<int, Eigen::RowMajor>& cell_to_edges() const { return cell_to_edges_; }
    const BinaryVector<Dynamic>& boundary_edges() const { return boundary_edges_; }
    int n_edges() const { return n_edges_; }
    int n_boundary_edges() const { return boundary_edges_.count(); }
    // iterators over edges
    class edge_iterator : public internals::filtering_iterator<edge_iterator, EdgeType> {
       protected:
        using Base = internals::filtering_iterator<edge_iterator, EdgeType>;
        using Base::index_;
        friend Base;
        const Triangulation* mesh_;
        int marker_;
        edge_iterator& operator()(int i) {
            Base::val_ = EdgeType(i, mesh_);
            return *this;
        }
       public:
        using TriangulationType = Triangulation<2, N>;
        edge_iterator(int index, const Triangulation* mesh, const BinaryVector<fdapde::Dynamic>& filter, int marker) :
            Base(index, 0, mesh->n_edges_, filter), mesh_(mesh), marker_(marker) {
            for (; index_ < Base::end_ && !filter[index_]; ++index_);
            if (index_ != Base::end_) { operator()(index_); }
        }
        edge_iterator(int index, const Triangulation* mesh) :   // apply no filter
            edge_iterator(index, mesh, BinaryVector<fdapde::Dynamic>::Ones(mesh->n_edges_), Unmarked) { }
        edge_iterator(int index, const Triangulation* mesh, int marker) :   // fast construction for end iterators
            Base(index, 0, mesh->n_edges_), marker_(marker) { }
        int marker() const { return marker_; }
    };
    edge_iterator edges_begin() const { return edge_iterator(0, this); }
    edge_iterator edges_end() const { return edge_iterator(n_edges_, this, Unmarked); }
    // iterator over boundary edges
    struct boundary_edge_iterator : public edge_iterator {
        boundary_edge_iterator(int index, const Triangulation* mesh) :
            edge_iterator(index, mesh, mesh->boundary_edges_, BoundaryAll) { }
        boundary_edge_iterator(int index, const Triangulation* mesh, int marker) :   // filter boundary edges by marker
            edge_iterator(
              index, mesh,
              marker == BoundaryAll ?
                mesh->boundary_edges_ :
                mesh->boundary_edges_ &
                  fdapde::make_binary_vector(mesh->edges_markers_.begin(), mesh->edges_markers_.end(), marker),
              marker) { }
    };
    boundary_edge_iterator boundary_edges_begin() const { return boundary_edge_iterator(0, this); }
    boundary_edge_iterator boundary_edges_end() const { return boundary_edge_iterator(n_edges_, this); }
    using boundary_iterator = boundary_edge_iterator;   // public view of 2D boundary
    BoundaryIterator<Triangulation<2, N>> boundary_begin(int marker = BoundaryAll) const {
        return BoundaryIterator<Triangulation<2, N>>(0, this, marker);
    }
    BoundaryIterator<Triangulation<2, N>> boundary_end(int marker = BoundaryAll) const {
        return BoundaryIterator<Triangulation<2, N>>(n_edges_, this, marker);
    }
    std::pair<BoundaryIterator<Triangulation<2, N>>, BoundaryIterator<Triangulation<2, N>>>
    boundary(int marker = BoundaryAll) const {
        return std::make_pair(boundary_begin(marker), boundary_end(marker));
    }
    const std::vector<int>& edges_markers() const { return edges_markers_; }
    // set boundary markers
    template <typename Lambda> void mark_boundary(int marker, Lambda&& lambda)
      requires(requires(Lambda lambda, EdgeType e) {
        { lambda(e) } -> std::same_as<bool>; }) {
        fdapde_assert(marker >= 0);
        edges_markers_.resize(n_edges_, Unmarked);
        for (boundary_edge_iterator it = boundary_edges_begin(); it != boundary_edges_end(); ++it) {
            if (lambda(*it)) {
                // give priority to highly marked edges
                if (edges_markers_[it->id()] < marker) { edges_markers_[it->id()] = marker; }
            }
        }
        return;
    }
    template <typename Iterator> void mark_boundary(Iterator first, Iterator last) {
        fdapde_static_assert(
          std::is_convertible_v<typename Iterator::value_type FDAPDE_COMMA int>, INVALID_ITERATOR_RANGE);
        int n_markers = std::distance(first, last);
	bool all_markers_positive = std::all_of(first, last, [](auto marker) { return marker >= 0; });
        fdapde_assert(n_markers == n_edges() && all_markers_positive);
	edges_markers_.resize(n_edges_, Unmarked);
        for (int i = 0; i < n_edges_; ++i) {
            int marker = *(first + i);
            // give priority to highly marked edges
            if (edges_markers_[i] < marker) { edges_markers_[i] = marker; }
        }
        return;
    }
    // marks all boundary edges
    void mark_boundary(int marker) {
        fdapde_assert(marker >= 0);
	edges_markers_.resize(n_edges_, Unmarked);
        for (auto it = boundary_begin(); it != boundary_end(); ++it) {
            // give priority to highly marked edges
            if (edges_markers_[it->id()] < marker) edges_markers_[it->id()] = marker;
        }
        return;
    }
    // point location
    template <int Rows, int Cols>
    std::conditional_t<Rows == Dynamic || Cols == Dynamic, DVector<int>, int>
    locate(const Eigen::Matrix<double, Rows, Cols>& p) const {
        fdapde_static_assert(
          (Cols == 1 && Rows == embed_dim) || (Cols == Dynamic && Rows == Dynamic),
          YOU_PASSED_A_MATRIX_OF_POINTS_TO_LOCATE_OF_THE_WRONG_DIMENSION);
        if (!location_policy_.has_value()) location_policy_ = LocationPolicy(this);
        return location_policy_->locate(p);
    }
    // the set of cells which have node id as vertex
    std::vector<int> node_patch(int id) const {
        if (!location_policy_.has_value()) location_policy_ = LocationPolicy(this);
        return location_policy_->all_locate(Base::node(id));
    }
   protected:
    std::vector<int> edges_ {};                        // nodes (as row indexes in nodes_ matrix) composing each edge
    std::vector<int> edge_to_cells_ {};                // for each edge, the ids of adjacent cells
    DMatrix<int, Eigen::RowMajor> cell_to_edges_ {};   // ids of edges composing each cell
    BinaryVector<fdapde::Dynamic> boundary_edges_ {};   // j-th element is 1 \iff edge j is on boundary
    std::vector<int> edges_markers_ {};
    int n_edges_ = 0;
    mutable std::optional<LocationPolicy> location_policy_ {};
    // cell caching
    std::vector<typename Base::CellType> cell_cache_;
    mutable typename Base::CellType cell_;   // used in case cell caching is off
};

// face-based storage
template <> class Triangulation<3, 3> : public TriangulationBase<3, 3, Triangulation<3, 3>> {
   public:
    using Base = TriangulationBase<3, 3, Triangulation<3, 3>>;
    static constexpr int n_nodes_per_face = 3;
    static constexpr int n_nodes_per_edge = 2;
    static constexpr int n_edges_per_face = 3;
    static constexpr int n_faces_per_cell = 4;
    static constexpr int n_edges_per_cell = 6;
    using FaceType = typename Base::CellType::FaceType;
    using EdgeType = typename Base::CellType::EdgeType;
    using LocationPolicy = TreeSearch<Triangulation<3, 3>>;
    using Base::embed_dim;
    using Base::local_dim;

    Triangulation() = default;
    Triangulation(
      const DMatrix<double>& nodes, const DMatrix<int>& cells, const DMatrix<int>& boundary, int flags = 0) :
        Base(nodes, cells, boundary, flags) {
        if (Base::flags_ & cache_cells) {   // populate cache if cell caching is active
            cell_cache_.reserve(n_cells_);
            for (int i = 0; i < n_cells_; ++i) { cell_cache_.emplace_back(i, this); }
        }
        using face_t = std::array<int, n_nodes_per_face>;
        using edge_t = std::array<int, n_nodes_per_edge>;
        struct face_info {
            int face_id, cell_id;   // for each face, its ID and the ID of one of the faces insisting on it
        };
	typedef int edge_info;
        auto face_pattern = cexpr::combinations<n_nodes_per_face, n_nodes_per_cell>();
        auto edge_pattern = cexpr::combinations<n_nodes_per_edge, n_nodes_per_face>();
        std::unordered_map<edge_t, edge_info, fdapde::std_array_hash<int, n_nodes_per_edge>> edges_map;
        std::unordered_map<face_t, face_info, fdapde::std_array_hash<int, n_nodes_per_face>> faces_map;
	std::vector<bool> boundary_faces, boundary_edges;
        face_t face;
        edge_t edge;
        cell_to_faces_.resize(n_cells_, n_faces_per_cell);
        // search vertex of face f opposite to edge e (the j-th vertex of f which is not a node of e)
        auto node_opposite_to_face = [this](int e, int f) -> int {
            int j = 0;
            for (; j < Base::n_nodes_per_cell; ++j) {
                bool found = false;
                for (int k = 0; k < n_nodes_per_face; ++k) {
                    if (faces_[e * n_nodes_per_face + k] == cells_(f, j)) { found = true; }
                }
                if (!found) break;
            }
            return j;
        };
        int face_id = 0, edge_id = 0;
        for (int i = 0; i < n_cells_; ++i) {
            for (int j = 0; j < face_pattern.rows(); ++j) {
                // construct face
                for (int k = 0; k < n_nodes_per_face; ++k) { face[k] = cells_(i, face_pattern(j, k)); }
                std::sort(face.begin(), face.end());   // normalize wrt node ordering
                auto it = faces_map.find(face);
                if (it == faces_map.end()) {   // never processed face
                    faces_.insert(faces_.end(), face.begin(), face.end());
                    face_to_cells_.insert(face_to_cells_.end(), {i, -1});
                    boundary_faces.push_back(true);
                    faces_map.emplace(face, face_info {face_id, i});
                    cell_to_faces_(i, j) = face_id;
                    face_id++;
                    // compute for each face the ids of its edges
                    for (int k = 0; k < n_edges_per_face; ++k) {
                        for (int h = 0; h < n_nodes_per_edge; ++h) { edge[h] = face[edge_pattern(k, h)]; }
                        std::sort(edge.begin(), edge.end());
                        auto it = edges_map.find(edge);
                        if (it == edges_map.end()) {
                            edges_.insert(edges_.end(), edge.begin(), edge.end());
                            face_to_edges_.push_back(edge_id);
                            edge_to_cells_[edge_id].insert(i);   // store (edge, cell) binding
                            edges_map.emplace(edge, edge_id);
			    boundary_edges.push_back(nodes_markers_[edge[0]] && nodes_markers_[edge[1]]);
                            edge_id++;
                        } else {
                            face_to_edges_.push_back(edges_map.at(edge));
			    edge_to_cells_[edges_map.at(edge)].insert(i);   // store (edge, cell) binding
                        }
                    }
                } else {
                    const auto& [h, k] = it->second;
                    // elements k and i are neighgbors (they share face with id h)
                    neighbors_(k, node_opposite_to_face(h, k)) = i;
                    neighbors_(i, node_opposite_to_face(h, i)) = k;
		    // store (edge, cell) binding for each edge of this face
                    for (int edge = 0; edge < n_edges_per_face; edge++) {
                        edge_to_cells_.at(face_to_edges_[h * n_edges_per_face + edge]).insert(i);
                    }
                    cell_to_faces_(i, j) = h;
		    face_to_cells_[2 * h + 1] = i;
		    boundary_faces[h] = false;
                    faces_map.erase(it);
                }
            }
        }
        n_faces_ = faces_.size() / n_nodes_per_face;
        n_edges_ = edges_.size() / n_nodes_per_edge;
        boundary_faces_ = BinaryVector<fdapde::Dynamic>(boundary_faces.begin(), boundary_faces.end(), n_faces_);
	boundary_edges_ = BinaryVector<fdapde::Dynamic>(boundary_edges.begin(), boundary_edges.end(), n_edges_);
        return;
    }
    // getters
    const typename Base::CellType& cell(int id) const {
        if (Base::flags_ & cache_cells) {   // cell caching enabled
            return cell_cache_[id];
        } else {
            cell_ = typename Base::CellType(id, this);
            return cell_;
        }
    }
    bool is_face_on_boundary(int id) const { return boundary_faces_[id]; }
    const DMatrix<int, Eigen::RowMajor>& neighbors() const { return neighbors_; }
    Eigen::Map<const DMatrix<int, Eigen::RowMajor>> faces() const {
        return Eigen::Map<const DMatrix<int, Eigen::RowMajor>>(faces_.data(), n_faces_, n_nodes_per_face);
    }
    Eigen::Map<const DMatrix<int, Eigen::RowMajor>> edges() const {
        return Eigen::Map<const DMatrix<int, Eigen::RowMajor>>(edges_.data(), n_edges_, n_nodes_per_edge);
    }
    const DMatrix<int, Eigen::RowMajor>& cell_to_faces() const { return cell_to_faces_; }
    Eigen::Map<const DMatrix<int, Eigen::RowMajor>> face_to_edges() const {
        return Eigen::Map<const DMatrix<int, Eigen::RowMajor>>(face_to_edges_.data(), n_faces_, n_edges_per_face);
    }
    Eigen::Map<const DMatrix<int, Eigen::RowMajor>> face_to_cells() const {
        return Eigen::Map<const DMatrix<int, Eigen::RowMajor>>(face_to_cells_.data(), n_faces_, 2);
    }
    const std::unordered_map<int, std::unordered_set<int>>& edge_to_cells() const { return edge_to_cells_; }
    const BinaryVector<Dynamic>& boundary_faces() const { return boundary_faces_; }
    int n_faces() const { return n_faces_; }
    int n_edges() const { return n_edges_; }
    int n_boundary_faces() const { return boundary_faces_.count(); }
    int n_boundary_edges() const { return boundary_edges_.count(); }
    // iterators over edges
    class edge_iterator : public internals::filtering_iterator<edge_iterator, EdgeType> {
       protected:
        using Base = internals::filtering_iterator<edge_iterator, EdgeType>;
        using Base::index_;
        friend Base;
        const Triangulation* mesh_;
        edge_iterator& operator()(int i) {
            Base::val_ = EdgeType(i, mesh_);
            return *this;
        }
       public:
        edge_iterator(int index, const Triangulation* mesh, const BinaryVector<fdapde::Dynamic>& filter) :
            Base(index, 0, mesh->n_edges_, filter), mesh_(mesh) {
            for (; index_ < Base::end_ && !filter[index_]; ++index_);
            if (index_ != Base::end_) { operator()(index_); }
        }
        edge_iterator(int index, const Triangulation* mesh) :   // apply no filter
            edge_iterator(index, mesh, BinaryVector<fdapde::Dynamic>::Ones(mesh->n_edges_)) { }
    };
    edge_iterator edges_begin() const { return edge_iterator(0, this); }
    edge_iterator edges_end() const { return edge_iterator(n_edges_, this); }
    // iterators over faces
    class face_iterator : public internals::filtering_iterator<face_iterator, FaceType> {
       protected:
        using Base = internals::filtering_iterator<face_iterator, FaceType>;
        using Base::index_;
        friend Base;
        const Triangulation* mesh_;
        int marker_;
        face_iterator& operator()(int i) {
            Base::val_ = FaceType(i, mesh_);
            return *this;
        }
       public:
        using TriangulationType = Triangulation<3, 3>;
        face_iterator(int index, const Triangulation* mesh, const BinaryVector<fdapde::Dynamic>& filter, int marker) :
            Base(index, 0, mesh->n_faces_, filter), mesh_(mesh), marker_(marker) {
            for (; index_ < Base::end_ && !filter[index_]; ++index_);
            if (index_ != Base::end_) { operator()(index_); }
        }
        face_iterator(int index, const Triangulation* mesh) :   // apply no filter
	  face_iterator(index, mesh, BinaryVector<fdapde::Dynamic>::Ones(mesh->n_edges_), Unmarked) { }
        face_iterator(int index, const Triangulation* mesh, int marker) :   // fast construction for end iterators
            Base(index, 0, mesh->n_faces_), marker_(marker) { }
        int marker() const { return marker_; }
    };
    face_iterator faces_begin() const { return face_iterator(0, this); }
    face_iterator faces_end() const { return face_iterator(n_faces_, this); }
    // iterator over boundary faces
    struct boundary_face_iterator : public face_iterator {
        boundary_face_iterator(int index, const Triangulation* mesh) :
            face_iterator(index, mesh, mesh->boundary_faces_, BoundaryAll) { }
        boundary_face_iterator(int index, const Triangulation* mesh, int marker) :   // filter boundary faces by marker
            face_iterator(
              index, mesh,
              marker == BoundaryAll ?
                mesh->boundary_faces_ :
                mesh->boundary_faces_ &
                  fdapde::make_binary_vector(mesh->faces_markers_.begin(), mesh->faces_markers_.end(), marker),
              marker) { }
    };
    boundary_face_iterator boundary_faces_begin() const { return boundary_face_iterator(0, this); }
    boundary_face_iterator boundary_faces_end() const { return boundary_face_iterator(n_faces_, this); }
    using boundary_iterator = boundary_face_iterator;   // public view of 3D boundary
    BoundaryIterator<Triangulation<3, 3>> boundary_begin(int marker = BoundaryAll) const {
        return BoundaryIterator<Triangulation<3, 3>>(0, this, marker);
    }
    BoundaryIterator<Triangulation<3, 3>> boundary_end(int marker = BoundaryAll) const {
        return BoundaryIterator<Triangulation<3, 3>>(n_faces_, this, marker);
    }
    std::pair<BoundaryIterator<Triangulation<3, 3>>, BoundaryIterator<Triangulation<3, 3>>>
    boundary(int marker = BoundaryAll) const {
        return std::make_pair(boundary_begin(marker), boundary_end(marker));
    }
    const std::vector<int>& faces_markers() const { return faces_markers_; }
    // set boundary markers
    template <typename Lambda> void mark_boundary(int marker, Lambda&& lambda)
      requires(requires(Lambda lambda, FaceType e) {
        { lambda(e) } -> std::same_as<bool>; }) {
        fdapde_assert(marker >= 0);
        faces_markers_.resize(n_faces_, Unmarked);
        for (boundary_face_iterator it = boundary_faces_begin(); it != boundary_faces_end(); ++it) {
            if (lambda(*it)) {
                // give priority to highly marked faces
                if (faces_markers_[it->id()] < marker) { faces_markers_[it->id()] = marker; }
            }
        }
        return;
    }
    template <typename Iterator> void mark_boundary(Iterator first, Iterator last) {
        fdapde_static_assert(
          std::is_convertible_v<typename Iterator::value_type FDAPDE_COMMA int>, INVALID_ITERATOR_RANGE);
        int n_markers = std::distance(first, last);
	bool all_markers_positive = std::all_of(first, last, [](auto marker) { return marker >= 0; });
        fdapde_assert(n_markers == n_faces() && all_markers_positive);
	faces_markers_.resize(n_faces_, Unmarked);
        for (int i = 0; i < n_faces_; ++i) {
            int marker = *(first + i);
            // give priority to highly marked faces
            if (faces_markers_[i] < marker) { faces_markers_[i] = marker; }
        }
        return;
    }
    // marks all boundary faces
    void mark_boundary(int marker) {
        fdapde_assert(marker >= 0);
	faces_markers_.resize(n_faces_, Unmarked);
        for (auto it = boundary_begin(); it != boundary_end(); ++it) {
            // give priority to highly marked faces
            if (faces_markers_[it->id()] < marker) faces_markers_[it->id()] = marker;
        }
        return;
    }
    // compute the surface triangular mesh of this 3D triangulation
    struct SurfaceReturnType {
        Triangulation<2, 3> triangulation;
        std::unordered_map<int, int> node_map, cell_map;
    };
    SurfaceReturnType surface() const {
        DMatrix<double> nodes(n_boundary_nodes(), FaceType::n_nodes);
        DMatrix<int> cells(n_boundary_faces(), FaceType::n_nodes), boundary(n_boundary_nodes(), 1);
        std::unordered_map<int, int> node_map;   // bounds node ids in the 3D mesh to rescaled ids on the surface mesh
        std::unordered_map<int, int> cell_map;   // bounds face ids in the 3D mesh to rescaled ids on the surface mesh
        // compute nodes, cells and boundary matrices
        int i = 0, j = 0;
        for (boundary_face_iterator it = boundary_faces_begin(); it != boundary_faces_end(); ++it) {
   	    int cell_id = it->adjacent_cells()[0] > -1 ? it->adjacent_cells()[0] : it->adjacent_cells()[1];
            cell_map[cell_id] = i;
            DVector<int> face_nodes = it->node_ids();
            for (int k = 0; k < FaceType::n_nodes; ++k) {
                int node_id = face_nodes[k];
                if (node_map.find(node_id) != node_map.end()) {
                    cells(i, k) = node_map.at(node_id);
                } else {   // never visited face
                    nodes.row(j) = nodes_.row(node_id);
		    boundary(j, 0) = is_node_on_boundary(node_id);
                    cells(i, k) = j;
		    node_map[node_id] = j;
		    j++;
                }
            }
	    i++;
        }
	return {Triangulation<2, 3>(nodes, cells, boundary), fdapde::reverse(node_map), fdapde::reverse(cell_map)};
    }

    // point location
    template <int Rows, int Cols>
    std::conditional_t<Rows == Dynamic || Cols == Dynamic, DVector<int>, int>
    locate(const Eigen::Matrix<double, Rows, Cols>& p) const {
        fdapde_static_assert(
          (Cols == 1 && Rows == embed_dim) || (Cols == Dynamic && Rows == Dynamic),
          YOU_PASSED_A_MATRIX_OF_POINTS_TO_LOCATE_OF_THE_WRONG_DIMENSION);
        if (!location_policy_.has_value()) location_policy_ = LocationPolicy(this);
        return location_policy_->locate(p);
    }
    // computes the set of elements which have node id as vertex
    std::vector<int> node_patch(int id) const {
        if (!location_policy_.has_value()) location_policy_ = LocationPolicy(this);
        return location_policy_->all_locate(Base::node(id));
    }
   protected:
    std::vector<int> faces_, edges_;   // nodes (as row indexes in nodes_ matrix) composing each face and edge
    std::vector<int> face_to_cells_;   // for each face, the ids of adjacent cells
    std::unordered_map<int, std::unordered_set<int>> edge_to_cells_;   // for each edge, the ids of insisting cells
    DMatrix<int, Eigen::RowMajor> cell_to_faces_ {};            // ids of faces composing each cell
    std::vector<int> face_to_edges_;                            // ids of edges composing each face
    BinaryVector<fdapde::Dynamic> boundary_faces_ {};           // j-th element is 1 \iff face j is on boundary
    BinaryVector<fdapde::Dynamic> boundary_edges_ {};           // j-th element is 1 \iff edge j is on boundary
    std::vector<int> faces_markers_;
    int n_faces_ = 0, n_edges_ = 0;
    mutable std::optional<LocationPolicy> location_policy_ {};
    // cell caching
    std::vector<typename Base::CellType> cell_cache_;
    mutable typename Base::CellType cell_;   // used in case cell caching is off
};

}   // namespace fdapde

#endif   // __TRIANGULATION_H__
