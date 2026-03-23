# if !defined( __structo_bounds_hxx__ )
# define __structo_bounds_hxx__
# include <cstdint>

namespace structo
{
  struct Bounds
  {
    uint32_t  uLower;
    uint32_t  uUpper;

    Bounds(): uLower( 0 ), uUpper( -1 ) { }
    Bounds( uint32_t min, uint32_t max ): uLower( min ), uUpper( max ) { }
  };
}

# endif   // !defined( __structo_bounds_hxx__ )
