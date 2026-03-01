# if !defined( __structo_rankers_hpp__ )
# define __structo_rankers_hpp__
# include "queries.hpp"

namespace structo {
namespace rankers {

  auto  BM25( const queries::Abstract& ) -> double;
  auto  Rich( const queries::Abstract& ) -> double;

}}

# endif   // !__structo_rankers_hpp__
