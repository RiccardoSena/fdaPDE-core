// integrate a BilinearFormExpr over mesh element e using basis B. The pair (i,j) indicates the element position of the produced
// value in the matrix discretizing the form. This method is used as part of the assembly loop in the computation of the
// discretization matrix of the differential operator L
template <unsigned int M, unsigned int K>
template <unsigned int N, unsigned int R, typename B, typename F>
double Integrator<M, K>::integrate(const B& basis, const Element<M, N, R>& e, int i , int j, const F& bilinearForm) const{
  // apply quadrature rule
  double value = 0;
  // builds the callable to integrate here from the bilinear form passed as argument
  ScalarField<M> f = bilinearForm.integrate(basis, e, i, j);
  for(size_t iq = 0; iq < integrationTable_.num_nodes; ++iq){
    // the field produced by a bilinear form is by construction evaluable at any spatial point
    SVector<M> p = SVector<M>(integrationTable_.nodes[iq].data());
    value += f(p) * integrationTable_.weights[iq];
  }
  // correct for measure of domain (element e)
  return value * e.measure();
}

// perform integration of the specific integral \int_e [f * \phi] using a basis system defined over the reference element 
// and the change of variables formula: \int_e [f(x) * \phi(x)] = \int_{E} [f(J(X)) * \Phi(X)] |detJ|
// where J is the affine mapping from the reference element E to the physical element e
// this overload is specialized for the assembly of the PDE's forcing term
template <unsigned int M, unsigned int K>
template <unsigned int N, unsigned int R, typename F>
double Integrator<M, K>::integrate(const Element<M, N, R>& e, const F& f, const typename LagrangianBasis<M, N, R>::element_type& Phi) const{
  double value = 0;
  // execute quadrature rule.
  for(size_t iq = 0; iq < integrationTable_.num_nodes; ++iq){
    SVector<M> p = SVector<M>(integrationTable_.nodes[iq].data());
    if constexpr(std::is_base_of<FieldExpr<F>, F>::value){
      // functor f is evaluable at any point. This is the case if the integrand f is given by
      // its analytical expression. Observe that all and only field expressions are accepted.
      SVector<N> Jp = e.barycentricMatrix()*p + e.coords()[0]; // map quadrature point on physical element e
      value += (f(Jp)*Phi(p))*integrationTable_.weights[iq];
    }else{
      // as a fallback we assume f given as vector of values with the assumption that f[e.ID()+iq] equals the value of the
      // discretized field at the iq-th quadrature node.
      value += (f[e.ID()+iq]*Phi(p))*integrationTable_.weights[iq];
    }
  }
  // correct for measure of domain (element e)
  return value * e.measure();
}

// integrate a callable F over a mesh element e. This quadrature rule is for fields f without any particular structure
template <unsigned int M, unsigned int K>
template <unsigned int N, unsigned int R, typename F>
double Integrator<M, K>::integrate(const Element<M, N, R>& e, const F &f) const {
  double value = 0;
  // execute quadrature rule
  for(size_t iq = 0; iq < integrationTable_.num_nodes; ++iq){
    if constexpr(std::is_invocable<F, SVector<N>>::value){
      // functor f is evaluable at any point. This is the case if the integrand f is given by
      // its analytical expression, i.e. can be invoked using an SVector<N> as argument
      SVector<N> p = e.barycentricMatrix()*SVector<M>(integrationTable_.nodes[iq].data()) + e.coords()[0]; // map quadrature point onto e
      value += f(p)*integrationTable_.weights[iq];
    }else{
      // as a fallback we assume f given as vector of values with the assumption that f[e->ID()+iq] equals the value of the
      // discretized field at the iq-th quadrature node.
      value += f[e->ID()+iq]*integrationTable_.weights[iq];
    }
  }
  // correct for measure of domain (element e)
  return value * e.measure();
}

// integrate a callable F over the entire mesh m.
// Just exploit linearity of the integral operation to sum the result of the integral of F over each mesh element
template <unsigned int M, unsigned int K>
template <unsigned int N, unsigned int R, typename F>
double Integrator<M, K>::integrate(const Mesh<M, N, R>& m, const F &f) const {
  double value = 0;
  // cycle over all mesh elements
  for(const auto& e : m)
    value += integrate(*e, f);
  return value;
}
