# if !defined( STRUCTO_SRC_INDEXER_SERIALIZE_CACHE_HPP_ )
# define STRUCTO_SRC_INDEXER_SERIALIZE_CACHE_HPP_
# include "mtc/serialize.h"
# include <type_traits>
# include <vector>

namespace structo {
namespace indexer {

  template <class O, size_t CacheSize>
  class SerializeCache: protected std::vector<char>
  {
    SerializeCache( const SerializeCache& ) = delete;
    SerializeCache& operator = ( const SerializeCache& ) = delete;

  public:
    SerializeCache( O* o ):
      std::vector<char>( (CacheSize + 4095) & ~4095 ), bufptr( data() ), output( o ) {}
    auto  ptr() const -> SerializeCache*
    {
      return const_cast<SerializeCache*>( this );
    }
    auto  put( const void* p, size_t l ) -> SerializeCache*
    {
      auto  srcptr = static_cast<const char*>( p );
      auto  srcend = srcptr + l;
      auto  bufend = data() + size();

      while ( srcptr != srcend )
      {
        while ( srcptr != srcend && bufptr != bufend )
          *bufptr++ = *srcptr++;

        if ( bufptr == bufend )
          if ( (output = ::Serialize( output, bufptr = data(), size() )) == nullptr )
            return nullptr;
      }
      return this;
    }
    auto  end() -> O*
    {
      if ( bufptr != data() )
        output = ::Serialize( output, data(), bufptr - data() );
      return bufptr = data(), output;
    }

  protected:
    char* bufptr;
    O*    output;

  };

}}

# endif   // !STRUCTO_SRC_INDEXER_SERIALIZE_CACHE_HPP_
