# if !defined( __structo_src_queries_decompressor_hpp__ )
# define __structo_src_queries_decompressor_hpp__
# include "../../queries.hpp"

namespace structo {
namespace queries {

  template <class RankEntry>
  auto  UnpackWordPos(
    Abstract::EntrySet* output,
    Abstract::EntryPos* appPtr,
    size_t              maxLen, const std::string_view& source, const RankEntry& ranker, unsigned id ) -> unsigned
  {
    auto  srcPtr( source.data() );
    auto  srcEnd( source.data() + source.size() );
    auto  uEntry = unsigned(0);
    auto  outPtr = output;
    auto  outEnd = outPtr + maxLen;

    for ( ; srcPtr < srcEnd && outPtr != outEnd; ++uEntry )
    {
      unsigned  uOrder;
      double    weight;

      srcPtr = ::FetchFrom( srcPtr, uOrder );

      if ( (weight = ranker( uEntry = (uOrder += uEntry), 0xff )) < 0 )
        continue;

      *outPtr++ = { { uOrder, uOrder }, weight, { appPtr, appPtr + 1 } };
      *appPtr++ = { id, uEntry };
    }

    return unsigned(outPtr - output);
  }

  template <class RankEntry>
  auto  UnpackWordFid(
    Abstract::EntrySet* output,
    Abstract::EntryPos* appPtr,
    size_t              maxLen, const std::string_view& source, const RankEntry& ranker, unsigned id ) -> unsigned
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

      srcPtr = ::FetchFrom( ::FetchFrom( srcPtr, ctlFid ), uEntry );

      for ( getFid = ctlFid >> 2; outPtr != outEnd; ++uEntry )
      {
        if ( (weight = ranker( uEntry, getFid )) > 0 )
        {
          *outPtr++ = { { uEntry, uEntry }, weight, { appPtr, appPtr + 1 } };
          *appPtr++ = { id, uEntry };
        }

        if ( srcPtr != srcEnd )
        {
          srcPtr = ::FetchFrom( srcPtr, ctlFid );
          uEntry += ctlFid;
        } else break;
      }
    }
      else
    {
      srcPtr = ::FetchFrom( ::FetchFrom( srcPtr, uEntry ), getFid );

      for ( uEntry >>= 2; outPtr != outEnd; ++uEntry )
      {
        unsigned  uOrder;

        if ( (weight = ranker( uEntry, getFid )) > 0 )
        {
          *outPtr++ = { { uEntry, uEntry }, weight, { appPtr, appPtr + 1 } };
          *appPtr++ = { id, uEntry };
        }
        if ( srcPtr != srcEnd )
        {
          srcPtr = ::FetchFrom( ::FetchFrom( srcPtr, uOrder ), getFid );
          uEntry += uOrder;
        } else break;
      }
    }

    return unsigned(outPtr - output);
  }

  template <size_t N, size_t M, class RankEntry>
  auto  UnpackWordPos(
    Abstract::EntrySet (&output)[N],
    Abstract::EntryPos (&appear)[M], const std::string_view& source, const RankEntry& ranker, unsigned id ) -> unsigned
  {
    return UnpackWordPos( output, appear, N, source, ranker, id );
  }

  template <size_t N, size_t M, class RankEntry>
  auto  UnpackWordFid(
    Abstract::EntrySet (&output)[N],
    Abstract::EntryPos (&appear)[M], const std::string_view& source, const RankEntry& ranker, unsigned id ) -> unsigned
  {
    return UnpackWordFid( output, appear, N, source, ranker, id );
  }

}}

# endif   // !__structo_src_queries_decompressor_hpp__
