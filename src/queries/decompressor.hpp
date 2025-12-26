# if !defined( __structo_src_queries_decompressor_hpp__ )
# define __structo_src_queries_decompressor_hpp__
# include "../../queries.hpp"

namespace structo {
namespace queries {

  struct ZeroForm
    {  const char* operator()( const char* src, uint8_t& fid ) const {  return fid = 0xff, src;  }  };

  struct LoadForm
    {  const char* operator()( const char* src, uint8_t& fid ) const {  return ::FetchFrom( src, fid );  }  };

  template <class GetFormId, class RankEntry>
  auto  UnpackEntries(
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
      uint8_t   formid = 0;
      unsigned  uOrder;
      double    weight;

      srcPtr = GetFormId()( ::FetchFrom( srcPtr, uOrder ), formid );

      if ( (weight = ranker( uEntry = (uOrder += uEntry), formid )) < 0 )
        continue;

      *outPtr++ = { { uOrder, uOrder }, weight, double(uOrder), { appPtr, appPtr + 1 } };
      *appPtr++ = { id, uEntry };
    }

    return unsigned(outPtr - output);
  }

  template <class GetFormId, size_t N, size_t M, class RankEntry>
  auto  UnpackEntries(
    Abstract::EntrySet (&output)[N],
    Abstract::EntryPos (&appear)[M], const std::string_view& source, const RankEntry& ranker, unsigned id ) -> unsigned
  {
    auto  srcPtr( source.data() );
    auto  srcEnd( source.data() + source.size() );
    auto  uEntry = unsigned(0);
    auto  outPtr = output;
    auto  outEnd = output + std::min( N, M );
    auto  appPtr = appear;
    auto  appEnd = appear + M;

    for ( ; srcPtr < srcEnd && outPtr != outEnd && appPtr != appEnd; ++uEntry )
    {
      uint8_t   formid = 0;
      unsigned  uOrder;
      double    weight;

      srcPtr = GetFormId()( ::FetchFrom( srcPtr, uOrder ), formid );

      if ( (weight = ranker( uEntry = (uOrder += uEntry), formid )) < 0 )
        continue;

      *outPtr++ = { { uOrder, uOrder }, weight, double(uOrder), { appPtr, appPtr + 1 } };
      *appPtr++ = { id, uEntry };
    }

    return unsigned(outPtr - output);
  }

}}

# endif   // !__structo_src_queries_decompressor_hpp__
