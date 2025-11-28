# if !defined( __structo_exceptions_hpp__ )
# define __structo_exceptions_hpp__
# include <stdexcept>

namespace structo {

  class index_overflow: public std::runtime_error {  using runtime_error::runtime_error;  };

}

# endif   // __structo_exceptions_hpp__
