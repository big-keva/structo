# if !defined( __structo_src_queries_decompressor_hpp__ )
# define __structo_src_queries_decompressor_hpp__
# include "../../queries.hpp"

namespace structo {
namespace queries {

  struct Limits
  {
    unsigned  uLower = 0;
    unsigned  uUpper = (unsigned)-1;
  };

  struct Pos final
  {
    struct Any
    {
      bool operator()( unsigned ) const {  return true;  }
    };

    struct Min
    {
      unsigned  min;

      bool operator()( unsigned pos ) const {  return pos >= min;  }
    };

    struct Max
    {
      unsigned  max;

      bool operator()( unsigned pos ) const {  return pos <= max;  }
    };
  };

  struct PosFid
  {
    unsigned  pos;
    uint8_t   fid;
  };

 /*
  * Специализация ::FetchFrom( S*, T& )
  */
  inline const char* Fetch( const char* s, unsigned& t ) noexcept
  {
    auto  bvalue = uint8_t(*s++);
    auto  result = unsigned(bvalue & 0x7f);

    if ( (bvalue & 0x80) == 0 )
      return t = result, s;

    result |= unsigned((bvalue = uint8_t(*s++)) & 0x7f) << 7;

    if ( (bvalue & 0x80) == 0 )
      return t = result, s;

    result |= unsigned((bvalue = uint8_t(*s++)) & 0x7f) << 14;

    if ( (bvalue & 0x80) == 0 )
      return t = result, s;

    result |= unsigned((bvalue = uint8_t(*s++)) & 0x7f) << 21;

    if ( (bvalue & 0x80) == 0 )
      return t = result, s;

    result |= unsigned((bvalue = uint8_t(*s++)) & 0x7f) << 28;

    return t = result, s;
  }

  template <class MinPos, class MaxPos>
  auto  UnpackWordPos(
    unsigned* output,
    size_t    maxLen, const std::string_view& source, MinPos minpos, MaxPos maxpos ) -> size_t
  {
    auto  srcPtr( source.data() );
    auto  srcEnd( source.data() + source.size() );
    auto  uEntry = unsigned(0);
    auto  outPtr = output;
    auto  outEnd = outPtr + maxLen;

    for ( ; srcPtr < srcEnd && outPtr != outEnd && maxpos( uEntry ); ++uEntry )
    {
      unsigned  uOrder;

      srcPtr = Fetch( srcPtr, uOrder );

      if ( minpos( (uEntry = (uOrder += uEntry) ) ) )
        *outPtr++ = uEntry;
    }

    return unsigned(outPtr - output);
  }

  template <class MinPos, class MaxPos>
  auto  UnpackWordPos(
    PosFid*   output,
    size_t    maxLen, const std::string_view& source, MinPos minpos, MaxPos maxpos ) -> size_t
  {
    auto  srcPtr( source.data() );
    auto  srcEnd( source.data() + source.size() );
    auto  uEntry = 0U;
    auto  outPtr = output;
    auto  outEnd = outPtr + maxLen;

    for ( ; srcPtr < srcEnd && outPtr != outEnd && maxpos( uEntry ); ++uEntry )
    {
      unsigned  uOrder;

      srcPtr = Fetch( srcPtr, uOrder );

      if ( minpos( (uEntry = (uOrder += uEntry) ) ) )
        *outPtr++ = { uEntry, 0xff };
    }

    return unsigned(outPtr - output);
  }

  template <class RankEntry, class MinPos, class MaxPos>
  auto  UnpackWordPos(
    Abstract::EntrySet* output,
    size_t              maxLen, const std::string_view& source, const RankEntry& ranker,
    MinPos              minpos,
    MaxPos              maxpos, unsigned id ) -> unsigned
  {
    auto  srcPtr( source.data() );
    auto  srcEnd( source.data() + source.size() );
    auto  uEntry = unsigned(0);
    auto  outPtr = output;
    auto  outEnd = outPtr + maxLen;

    for ( ; srcPtr < srcEnd && outPtr != outEnd && maxpos( uEntry ); ++uEntry )
    {
      unsigned  uOrder;
      double    weight;

      srcPtr = Fetch( srcPtr, uOrder );

      if ( minpos( (uEntry = (uOrder += uEntry) ) ) && (weight = ranker( uEntry, 0xff )) > 0 )
        MakeEntrySet( *outPtr++, { uEntry, id }, weight );
    }

    return unsigned(outPtr - output);
  }

  template <class MinPos, class MaxPos>
  auto  UnpackWordFid(
    PosFid* output,
    size_t  maxLen, const std::string_view& source, MinPos minpos, MaxPos maxpos ) -> unsigned
  {
    auto    srcPtr( source.data() );
    auto    srcEnd( source.data() + source.size() );
    auto    uEntry = unsigned(0);
    auto    outPtr = output;
    auto    outEnd = outPtr + maxLen;
    uint8_t getFid;

    if ( (*srcPtr & 0x01) != 0 )
    {
      unsigned ctlFid;

      srcPtr = Fetch( Fetch( srcPtr, ctlFid ), uEntry );
      getFid = ctlFid >> 2;

      for ( ; outPtr != outEnd && maxpos( uEntry ); ++uEntry )
      {
        if ( minpos( uEntry ) )
          *outPtr++ = { uEntry, getFid };

        if ( srcPtr == srcEnd )
          break;

        srcPtr = Fetch( srcPtr, ctlFid );
          uEntry += ctlFid;
      }
    }
      else
    {
      srcPtr = Fetch( srcPtr, uEntry );
      getFid = *srcPtr++;

      for ( uEntry >>= 2; outPtr != outEnd && maxpos( uEntry ); ++uEntry )
      {
        unsigned  uOrder;

        if ( minpos( uEntry ) )
          *outPtr++ = { uEntry, getFid };

        if ( srcPtr == srcEnd )
          break;

        srcPtr = Fetch( srcPtr, uOrder );
          getFid = *srcPtr++;
          uEntry += uOrder;
      }
    }

    return unsigned(outPtr - output);
  }

  template <class RankEntry, class MinPos, class MaxPos>
  auto  UnpackWordFid(
    Abstract::EntrySet* output,
    size_t              maxLen, const std::string_view& source, const RankEntry& ranker,
    MinPos              minpos,
    MaxPos              maxpos, unsigned id ) -> unsigned
  {
    auto    srcPtr( source.data() );
    auto    srcEnd( source.data() + source.size() );
    auto    uEntry = unsigned(0);
    auto    outPtr = output;
    auto    outEnd = outPtr + maxLen;
    uint8_t getFid;
    double  weight;

    if ( (*srcPtr & 0x01) != 0 )
    {
      unsigned ctlFid;

      srcPtr = Fetch( Fetch( srcPtr, ctlFid ), uEntry );
        getFid = ctlFid >> 2;

      for ( ; outPtr != outEnd && maxpos( uEntry ); ++uEntry )
      {
        if ( minpos( uEntry ) && (weight = ranker( uEntry, getFid )) > 0 )
          MakeEntrySet( *outPtr++, { uEntry, id }, weight );

        if ( srcPtr == srcEnd )
          break;

        srcPtr = Fetch( srcPtr, ctlFid );
          uEntry += ctlFid;
      }
    }
      else
    {
      srcPtr = Fetch( srcPtr, uEntry );
        getFid = *srcPtr++;

      for ( uEntry >>= 2; outPtr != outEnd && maxpos( uEntry ); ++uEntry )
      {
        unsigned  uOrder;

        if ( minpos( uEntry ) && (weight = ranker( uEntry, getFid )) > 0 )
          MakeEntrySet( *outPtr++, { uEntry, id }, weight );

        if ( srcPtr == srcEnd )
          break;

        srcPtr = Fetch( srcPtr, uOrder );
        getFid = *srcPtr++;
        uEntry += uOrder;
      }
    }

    return unsigned(outPtr - output);
  }

  template <size_t N>
  auto  UnpackWordPos(
    unsigned  (&output)[N], const std::string_view& source, const Limits& limits ) -> unsigned
  {
    if ( limits.uLower != 0 )
    {
      return limits.uUpper != (unsigned)-1 ?
        UnpackWordPos( output, N, source, Pos::Min{ limits.uLower }, Pos::Max{ limits.uUpper } ) :
        UnpackWordPos( output, N, source, Pos::Min{ limits.uLower }, Pos::Any{} );
    }
    return limits.uUpper != (unsigned)-1 ?
      UnpackWordPos( output, N, source, Pos::Any{}, Pos::Max{ limits.uUpper } ) :
      UnpackWordPos( output, N, source, Pos::Any{}, Pos::Any{} );
  }

  template <size_t N>
  auto  UnpackWordPos(
    PosFid  (&output)[N], const std::string_view& source, const Limits& limits ) -> unsigned
  {
    if ( limits.uLower != 0 )
    {
      return limits.uUpper != (unsigned)-1 ?
        UnpackWordPos( output, N, source, Pos::Min{ limits.uLower }, Pos::Max{ limits.uUpper } ) :
        UnpackWordPos( output, N, source, Pos::Min{ limits.uLower }, Pos::Any{} );
    }
    return limits.uUpper != (unsigned)-1 ?
      UnpackWordPos( output, N, source, Pos::Any{}, Pos::Max{ limits.uUpper } ) :
      UnpackWordPos( output, N, source, Pos::Any{}, Pos::Any{} );
  }

  template <size_t N, class RankEntry>
  auto  UnpackWordPos(
    Abstract::EntrySet (&output)[N], const std::string_view& source, const RankEntry& ranker, const Limits& limits, unsigned id ) -> unsigned
  {
    if ( limits.uLower != 0 )
    {
      return limits.uUpper != (unsigned)-1 ?
        UnpackWordPos( output, N, source, ranker, Pos::Min{ limits.uLower }, Pos::Max{ limits.uUpper }, id ) :
        UnpackWordPos( output, N, source, ranker, Pos::Min{ limits.uLower }, Pos::Any{}, id );
    }
    return limits.uUpper != (unsigned)-1 ?
      UnpackWordPos( output, N, source, ranker, Pos::Any{}, Pos::Max{ limits.uUpper }, id ) :
      UnpackWordPos( output, N, source, ranker, Pos::Any{}, Pos::Any{}, id );
  }

  inline
  auto  UnpackWordFid(
    PosFid* output, size_t  maxlen, const std::string_view& source, const Limits& limits ) -> unsigned
  {
    if ( limits.uLower != 0 )
    {
      return limits.uUpper != (unsigned)-1 ?
        UnpackWordFid( output, maxlen, source, Pos::Min{ limits.uLower }, Pos::Max{ limits.uUpper } ) :
        UnpackWordFid( output, maxlen, source, Pos::Min{ limits.uLower }, Pos::Any{} );
    }
    return limits.uUpper != (unsigned)-1 ?
      UnpackWordFid( output, maxlen, source, Pos::Any{}, Pos::Max{ limits.uUpper } ) :
      UnpackWordFid( output, maxlen, source, Pos::Any{}, Pos::Any{} );
  }

  template <size_t N>
  auto  UnpackWordFid(
    PosFid  (&output)[N], const std::string_view& source, const Limits& limits ) -> unsigned
  {
    return UnpackWordFid( output, N, source, limits );
  }

  template <class RankEntry>
  auto  UnpackWordFid(
    Abstract::EntrySet* output,
    size_t              maxlen, const std::string_view& source, const RankEntry& ranker, const Limits& limits, unsigned id ) -> unsigned
  {
    if ( limits.uLower != 0 )
    {
      return limits.uUpper != (unsigned)-1 ?
        UnpackWordFid( output, maxlen, source, ranker, Pos::Min{ limits.uLower }, Pos::Max{ limits.uUpper }, id ) :
        UnpackWordFid( output, maxlen, source, ranker, Pos::Min{ limits.uLower }, Pos::Any{}, id );
    }
    return limits.uUpper != (unsigned)-1 ?
      UnpackWordFid( output, maxlen, source, ranker, Pos::Any{}, Pos::Max{ limits.uUpper }, id ) :
      UnpackWordFid( output, maxlen, source, ranker, Pos::Any{}, Pos::Any{}, id );
  }

  template <size_t N, class RankEntry>
  auto  UnpackWordFid(
    Abstract::EntrySet (&output)[N], const std::string_view& source, const RankEntry& ranker, const Limits& limits, unsigned id ) -> unsigned
  {
    return UnpackWordFid( output, N, source, ranker, limits, id );
  }

}}

# endif   // !__structo_src_queries_decompressor_hpp__
