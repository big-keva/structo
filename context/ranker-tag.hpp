# if !defined( __structo_context_ranker_tag_hpp__ )
# define __structo_context_ranker_tag_hpp__
# include <mtc/serialize.h>
# include <cstdint>

namespace structo {
namespace context {

  struct RankerTag
  {
    unsigned    format;
    uint32_t    uLower;       // start offset, bytes
    uint32_t    uUpper;       // end offset, bytes
    unsigned    length = 0;   // count of covered tags, unused by default

  public:
    bool  operator==( const RankerTag& to ) const
      {  return format == to.format && uLower == to.uLower && uUpper == to.uUpper;  }
    bool  operator!=( const RankerTag& to ) const
      {  return !(*this == to);  }
    bool  Covers( uint32_t pos ) const
      {  return pos >= uLower && pos <= uUpper;  }
    bool  Covers( const std::pair<uint32_t, uint32_t>& span ) const
      {  return uLower <= span.first && uUpper >= span.second;  }
  };

}}

# endif // !__structo_context_ranker_tag_hpp__
